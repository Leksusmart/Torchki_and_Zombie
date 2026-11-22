#include "ai/ZombieAI.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include "MapGenerator.h"
#include "ai/CitizenAI.h"
#include "ai/UnitMovement.h"
#include "ai/UnitSpatialGrid.h"
#include "core/IDestructible.h"
#include "core/NavGrid.h"
#include "core/WorldState.h"
#include "systems/NavigationService.h"

int ZombieAI::findBestTarget(const Unit &u,
                             const WorldState &state,
                             const UnitSpatialGrid &grid,
                             const NavigationService &nav)
{
    // Задачи:
    // 1. Найти ближайшую видимую жертву в радиусе 75 м (включая находящихся за временными стенами)

    int bestTarget = -1;
    float bestDistSq = 75.0F * 75.0F;

    const int zCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int zCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    for (int dy = -1; dy <= 1; ++dy) {
        const int ny = zCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;

        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = zCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (size_t tIdx : grid.targetGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const auto &t = state.units[tIdx];
                if (t.hp <= 0)
                    continue;

                const bool sameBuilding = (u.insideBuildingId != -1
                                           && u.insideBuildingId == t.insideBuildingId);
                if (!sameBuilding && (u.insideBuildingId != -1 || t.insideBuildingId != -1))
                    continue;

                const float ddx = t.x - u.x;
                const float ddy = t.y - u.y;
                const float dSq = ddx * ddx + ddy * ddy;
                if (dSq >= bestDistSq)
                    continue;

                if (sameBuilding
                    || nav.hasLineOfSightIgnoringWalls(u.x, u.y, t.x, t.y, state.navGrid)) {
                    bestDistSq = dSq;
                    bestTarget = static_cast<int>(tIdx);
                }
            }
        }
    }

    return bestTarget;
}

bool ZombieAI::attackBlockingObstacleAhead(Unit &u, IDestructibleProvider *obstacles)
{
    // Задачи:
    // 1. Запросить у IDestructibleProvider ближайшее разрушаемое препятствие по курсу движения
    // 2. Остановить зомби и нанести 1 ед. урона по истечении attackCooldownSec

    if (u.insideBuildingId != -1 || !obstacles)
        return false;

    const float spd = std::hypot(u.vx, u.vy);
    if (spd < 0.1F)
        return false;

    DestructibleHit hit;
    if (!obstacles->findBlockingObstacle(u.x, u.y, u.vx / spd, u.vy / spd, 3.0F, hit))
        return false;

    u.vx = 0.0F;
    u.vy = 0.0F;
    if (u.attackCooldownSec <= 0.0F) {
        u.attackCooldownSec = 0.5F;
        obstacles->applyDamage(hit.obstacleId, 1, hit.hitPoint);
    }
    return true;
}

bool ZombieAI::tryAssaultBuilding(Unit &u,
                                  WorldState &state,
                                  std::vector<BuildingObject> &buildings,
                                  const UnitSpatialGrid &grid,
                                  const NavigationService &nav,
                                  IDestructibleProvider *obstacles,
                                  bool &buildingsVisualDirty,
                                  bool &workplaceLabelsDirty)
{
    // Задачи:
    // 1. Найти ближайшее занятое и не горящее здание в радиусе 75 м
    // 2. Подойти к двери (круша препятствия на пути) и атаковать её
    // 3. При падении HP здания до 0 — поджечь его и вывести всех людей в панике

    const int zCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int zCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    int bestBldg = -1;
    float bestBldgDistSq = 75.0F * 75.0F;

    for (int dy = -1; dy <= 1; ++dy) {
        const int ny = zCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = zCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (int bId : grid.buildingGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const auto &b = buildings[bId];
                if (b.currentOccupants <= 0 || b.health <= 0 || b.isBurning || b.isBurnedOut)
                    continue;
                const QPointF targetPt = !b.pathFromRoad.empty() ? b.doorCenter
                                                                 : QPointF(b.cx, b.cy);
                const float bx = static_cast<float>(targetPt.x());
                const float by = static_cast<float>(targetPt.y());
                const float ddx = bx - u.x;
                const float ddy = by - u.y;
                const float dSq = ddx * ddx + ddy * ddy;
                if (dSq < bestBldgDistSq
                    && nav.hasLineOfSightIgnoringWalls(u.x, u.y, bx, by, state.navGrid)) {
                    bestBldgDistSq = dSq;
                    bestBldg = b.id;
                }
            }
        }
    }

    if (bestBldg == -1)
        return false;

    auto &b = buildings[bestBldg];
    const QPointF targetPt = !b.pathFromRoad.empty() ? b.doorCenter : QPointF(b.cx, b.cy);
    const float bx = static_cast<float>(targetPt.x());
    const float by = static_cast<float>(targetPt.y());
    const float dx = bx - u.x;
    const float dy = by - u.y;
    const float d = std::sqrt(bestBldgDistSq);

    if (d <= 5.0F) {
        u.vx = 0.0F;
        u.vy = 0.0F;
        if (u.attackCooldownSec <= 0.0F) {
            u.attackCooldownSec = 0.5F;
            b.health = std::max(0, b.health - 1);
            b.underAttackTimerSec = 3.0F;
            if (b.health == 0 && !b.isBurning && !b.isBurnedOut) {
                b.isBurning = true;
                b.burnTimerSec = b.area / 15.0F;
                buildingsVisualDirty = true;
                workplaceLabelsDirty = true;
                for (auto &occupant : state.units) {
                    if (occupant.insideBuildingId == b.id) {
                        occupant.panicTimerSec = 3.0F;
                        occupant.state = UnitState::Walking;
                        occupant.stateTimerSec = 8.0F;
                        occupant.waypoints.clear();
                        for (int pIdx = static_cast<int>(b.pathFromRoad.size()) - 1; pIdx >= 0;
                             --pIdx) {
                            occupant.waypoints.push_back(b.pathFromRoad[pIdx]);
                        }
                    }
                }
            }
        }
    } else {
        u.vx = (dx / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
        u.vy = (dy / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
        attackBlockingObstacleAhead(u, obstacles);
    }
    return true;
}

void ZombieAI::updateBehavior(Unit &u,
                              float dt,
                              WorldState &state,
                              std::vector<BuildingObject> &buildings,
                              const UnitSpatialGrid &grid,
                              const MapGenerator &generator,
                              const NavigationService &nav,
                              IDestructibleProvider *obstacles,
                              bool &buildingsVisualDirty,
                              bool &workplaceLabelsDirty)
{
    // Задачи:
    // 1. Найти видимую жертву и преследовать напролом (атакуя или круша препятствия на пути)
    // 2. Двигаться по текущим waypoints или по последнему известному следу цели
    // 3. Выйти из пустого здания, штурмовать занятое здание или бродяжничать

    (void) dt;
    auto *rng = QRandomGenerator::global();

    // 1. Преследование видимой жертвы
    const int bestTarget = findBestTarget(u, state, grid, nav);
    if (bestTarget != -1) {
        const float tx = state.units[bestTarget].x;
        const float ty = state.units[bestTarget].y;

        if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())
            && state.units[bestTarget].insideBuildingId != u.insideBuildingId) {
            if (u.waypoints.empty()) {
                const auto &b = buildings[u.insideBuildingId];
                for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                    u.waypoints.push_back(b.pathFromRoad[p]);
            }
            const QPointF wp = u.waypoints.front();
            const float wdx = static_cast<float>(wp.x()) - u.x;
            const float wdy = static_cast<float>(wp.y()) - u.y;
            const float wdist = std::hypot(wdx, wdy);
            if (wdist < 1.5F) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (wdx / wdist) * UnitMovement::ZOMBIE_CHASE_SPEED;
                u.vy = (wdy / wdist) * UnitMovement::ZOMBIE_CHASE_SPEED;
            }
            return;
        }

        u.targetUnitId = state.units[bestTarget].id;
        u.hasLastKnownTarget = true;
        u.lastKnownTargetPos = QPointF(tx, ty);

        const float dx = tx - u.x;
        const float dy = ty - u.y;
        const float d = std::hypot(dx, dy);

        if (d <= 3.5F && nav.hasWalkableLineOfSight(u.x, u.y, tx, ty, state.navGrid)) {
            u.waypoints.clear();
            if (u.attackCooldownSec <= 0.0F) {
                u.attackCooldownSec = 0.5F;
                Unit &victim = state.units[bestTarget];
                if (victim.type == UnitType::Soldier || victim.type == UnitType::Vaccinated) {
                    victim.hp -= 1;
                } else if (victim.type == UnitType::Citizen) {
                    CitizenAI::infectUnit(victim, state.units, false);
                    u.targetUnitId = -1;
                    u.hasLastKnownTarget = false;
                }
            }
            return;
        }
        if (nav.hasWalkableLineOfSight(u.x, u.y, tx, ty, state.navGrid)) {
            u.waypoints.clear();
            u.vx = (dx / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
            u.vy = (dy / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
            return;
        }

        if (u.waypoints.empty()
            || std::hypot(u.waypoints.back().x() - tx, u.waypoints.back().y() - ty) > 25.0F) {
            u.waypoints = nav.buildPathBetween(QPointF(u.x, u.y),
                                               QPointF(tx, ty),
                                               generator,
                                               state.navGrid,
                                               true);
            if (u.waypoints.size() >= 2) {
                const float segLen = static_cast<float>(
                    std::hypot(u.waypoints[1].x() - u.waypoints[0].x(),
                               u.waypoints[1].y() - u.waypoints[0].y()));
                const float distToSecond = static_cast<float>(
                    std::hypot(u.waypoints[1].x() - u.x, u.waypoints[1].y() - u.y));
                if (distToSecond < segLen
                    && nav.hasWalkableLineOfSight(u.x,
                                                  u.y,
                                                  static_cast<float>(u.waypoints[1].x()),
                                                  static_cast<float>(u.waypoints[1].y()),
                                                  state.navGrid)) {
                    u.waypoints.erase(u.waypoints.begin());
                }
            }
        }
        if (!u.waypoints.empty()) {
            const QPointF wp = u.waypoints.front();
            const float wdx = static_cast<float>(wp.x()) - u.x;
            const float wdy = static_cast<float>(wp.y()) - u.y;
            const float wdist = std::hypot(wdx, wdy);
            const float reachR = (u.waypoints.size() > 1) ? (generator.roadWidth() * 0.35F) : 2.0F;
            if (wdist <= reachR) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (wdx / wdist) * UnitMovement::ZOMBIE_CHASE_SPEED;
                u.vy = (wdy / wdist) * UnitMovement::ZOMBIE_CHASE_SPEED;
                attackBlockingObstacleAhead(u, obstacles);
            }
            return;
        }
    }

    // 2. Движение по маршруту
    if (!u.waypoints.empty()) {
        const QPointF target = u.waypoints.front();
        const float dx = static_cast<float>(target.x()) - u.x;
        const float dy = static_cast<float>(target.y()) - u.y;
        const float dist = std::hypot(dx, dy);
        if (dist < 1.5F) {
            u.waypoints.erase(u.waypoints.begin());
        } else {
            u.vx = (dx / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
            u.vy = (dy / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
            attackBlockingObstacleAhead(u, obstacles);
        }
        return;
    }

    // 3. Преследование по следу
    if (u.hasLastKnownTarget) {
        const float lkxF = static_cast<float>(u.lastKnownTargetPos.x());
        const float lkyF = static_cast<float>(u.lastKnownTargetPos.y());
        if (u.insideBuildingId == -1
            && state.navGrid.buildingIdAt(static_cast<int>(lkxF), static_cast<int>(lkyF)) != -1) {
            u.targetUnitId = -1;
            u.hasLastKnownTarget = false;
        } else {
            const float dx = lkxF - u.x;
            const float dy = lkyF - u.y;
            const float d = std::hypot(dx, dy);
            if (d < 4.0F) {
                u.targetUnitId = -1;
                u.hasLastKnownTarget = false;
            } else {
                u.vx = (dx / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
                u.vy = (dy / d) * UnitMovement::ZOMBIE_CHASE_SPEED;
                attackBlockingObstacleAhead(u, obstacles);
                return;
            }
        }
    }

    // 4. Выход из пустого здания
    if (u.insideBuildingId != -1 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        const auto &b = buildings[u.insideBuildingId];
        for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
            u.waypoints.push_back(b.pathFromRoad[p]);
        return;
    }

    // 5. Штурм занятого здания
    if (tryAssaultBuilding(u,
                           state,
                           buildings,
                           grid,
                           nav,
                           obstacles,
                           buildingsVisualDirty,
                           workplaceLabelsDirty)) {
        return;
    }

    // 6. Бродяжничество (без цели препятствия не атакуем)
    if (rng->bounded(100) < 4) {
        const float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        u.vx = std::cos(angle) * UnitMovement::ZOMBIE_WANDER_SPEED;
        u.vy = std::sin(angle) * UnitMovement::ZOMBIE_WANDER_SPEED;
    }
}

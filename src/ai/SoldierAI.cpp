#include "ai/SoldierAI.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include "MapGenerator.h"
#include "ai/UnitMovement.h"
#include "ai/UnitSpatialGrid.h"
#include "core/NavGrid.h"
#include "core/WorldState.h"
#include "systems/EffectsSystem.h"
#include "systems/NavigationService.h"

void SoldierAI::updateBehavior(Unit &u,
                               float dt,
                               WorldState &state,
                               const std::vector<BuildingObject> &buildings,
                               const UnitSpatialGrid &grid,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EffectsSystem &effects)
{
    // Задачи:
    // 1. Проверить таймер службы (90 с) и обработать одиночную эвакуацию по мосту
    // 2. Найти ближайшую цель в радиусе огня (зомби или инфицированного) и выстрелить
    // 3. Выдержать оптимальную боевую дистанцию при наличии видимой цели
    // 4. Иначе — идти по дорожным узлам к ближайшему зомби/инфицированному
    // 5. Если целей нет — двигаться по текущим waypoints или патрулировать

    if (u.repathCooldownSec > 0.0F)
        u.repathCooldownSec = std::max(0.0F, u.repathCooldownSec - dt);

    // 1. Таймер службы и эвакуация
    if (updateEvacuation(u, dt, state, buildings, grid, generator, nav, effects))
        return;

    // 2. Огонь по цели в радиусе 35 м (включая сквозь карантинные стены)
    const int fireTargetIdx = findFireTarget(u, state, grid, nav);
    if (fireTargetIdx != -1) {
        const Unit &target = state.units[fireTargetIdx];
        const float dist = std::hypot(target.x - u.x, target.y - u.y);

        u.targetUnitId = target.id;
        fireAtTarget(u, static_cast<size_t>(fireTargetIdx), state, effects);

        // 3. Корректировка боевой дистанции или огонь из-за укрытия
        const bool sameBuilding = (u.insideBuildingId != -1
                                   && u.insideBuildingId == target.insideBuildingId);
        u.waypoints.clear();
        if (sameBuilding || nav.hasWalkableLineOfSight(u.x, u.y, target.x, target.y, state.navGrid)) {
            adjustCombatPosition(u, target.x, target.y, dist);
        } else {
            u.vx = 0.0F;
            u.vy = 0.0F;
        }
        return;
    }

    // 4. Преследование глобальной цели
    const int huntTargetIdx = findHuntTarget(u, state, grid);
    if (huntTargetIdx != -1) {
        const Unit &huntTarget = state.units[huntTargetIdx];
        u.targetUnitId = huntTarget.id;
        routeToTarget(u, huntTarget, generator, nav, state.navGrid);
        if (followWaypoints(u, generator))
            return;
    } else {
        u.targetUnitId = -1;
    }

    // 5. Движение по маршруту или патруль
    if (followWaypoints(u, generator))
        return;

    patrol(u, buildings, generator, nav, state.navGrid);
}

bool SoldierAI::updateEvacuation(Unit &u,
                                 float dt,
                                 WorldState &state,
                                 const std::vector<BuildingObject> &buildings,
                                 const UnitSpatialGrid &grid,
                                 const MapGenerator &generator,
                                 const NavigationService &nav,
                                 EffectsSystem &effects)
{
    // Задачи:
    // 1. Уменьшить таймер службы (90 с) и перевести в режим отступления по истечении
    // 2. Провести солдата по мосту, если он уже достиг эвакуационного выхода
    // 3. Стрелять по врагам в радиусе огня на ходу
    // 4. Выбрать самый безопасный эвакуационный выход и построить маршрут
    // 5. Двигаться к выходу с уклонением от видимых зомби и перейти на мост

    if (!u.isRetreating) {
        u.lifeTimerSec -= dt;
        if (u.lifeTimerSec > 0.0F)
            return false;

        u.isRetreating = true;
        u.targetBuildingId = -1;
        u.waypoints.clear();
    }

    if (u.onEvacBridge)
        return moveOnEvacBridge(u, dt);

    const int fireTargetIdx = findFireTarget(u, state, grid, nav);
    if (fireTargetIdx != -1) {
        u.targetUnitId = state.units[fireTargetIdx].id;
        fireAtTarget(u, static_cast<size_t>(fireTargetIdx), state, effects);
    } else {
        u.targetUnitId = -1;
    }

    const auto &nodes = generator.nodes();
    const int bestEvacNode = findSafestEvacExit(u, state, grid, generator, nav);
    if (bestEvacNode == -1) {
        u.hp = 0;
        return true;
    }

    const QPointF evacPos = nodes[bestEvacNode].pos;
    if (u.waypoints.empty() || u.targetBuildingId != bestEvacNode) {
        u.targetBuildingId = bestEvacNode;
        u.waypoints = nav.buildPathBetween(QPointF(u.x, u.y), evacPos, generator, state.navGrid);

        if (u.waypoints.size() >= 2) {
            const float d01 = static_cast<float>(
                std::hypot(u.waypoints[1].x() - u.waypoints[0].x(),
                           u.waypoints[1].y() - u.waypoints[0].y()));
            const float d1 = static_cast<float>(
                std::hypot(u.waypoints[1].x() - u.x, u.waypoints[1].y() - u.y));
            if (d1 < d01
                && nav.hasWalkableLineOfSight(u.x,
                                              u.y,
                                              static_cast<float>(u.waypoints[1].x()),
                                              static_cast<float>(u.waypoints[1].y()),
                                              state.navGrid)) {
                u.waypoints.erase(u.waypoints.begin());
            }
        }

        if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
            const auto &b = buildings[u.insideBuildingId];
            std::vector<QPointF> exitPts;
            for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                exitPts.push_back(b.pathFromRoad[p]);
            u.waypoints.insert(u.waypoints.begin(), exitPts.begin(), exitPts.end());
        }
    }

    const float distToExit = static_cast<float>(std::hypot(evacPos.x() - u.x, evacPos.y() - u.y));
    if (distToExit <= 5.0F) {
        const auto &routes = generator.evacRoutes();
        if (!routes.empty()) {
            const EvacRoute *bestRoute = &routes[0];
            float bestRouteDist = 1e9F;
            for (const auto &r : routes) {
                const float d = static_cast<float>(
                    std::hypot(r.startPos.x() - evacPos.x(), r.startPos.y() - evacPos.y()));
                if (d < bestRouteDist) {
                    bestRouteDist = d;
                    bestRoute = &r;
                }
            }
            u.onEvacBridge = true;
            u.x = static_cast<float>(bestRoute->startPos.x());
            u.y = static_cast<float>(bestRoute->startPos.y());
            u.evacBridgeEnd = bestRoute->endPos;
            u.waypoints.clear();
        } else {
            u.hp = 0;
        }
        return true;
    }

    followWaypoints(u, generator);
    applyEvacAvoidance(u, state, grid, nav);
    return true;
}

bool SoldierAI::moveOnEvacBridge(Unit &u, float dt)
{
    // Задачи:
    // 1. Вести солдата по прямой от начала моста до конечной точки за картой
    // 2. Удалить юнит при достижении конца моста

    const float dx = static_cast<float>(u.evacBridgeEnd.x()) - u.x;
    const float dy = static_cast<float>(u.evacBridgeEnd.y()) - u.y;
    const float dist = std::hypot(dx, dy);

    if (dist < 4.0F) {
        u.hp = 0;
    } else {
        const float bridgeSpeed = 32.0F;
        u.vx = (dx / dist) * bridgeSpeed;
        u.vy = (dy / dist) * bridgeSpeed;
        u.x += u.vx * dt;
        u.y += u.vy * dt;
    }
    return true;
}

int SoldierAI::findSafestEvacExit(const Unit &u,
                                    const WorldState &state,
                                    const UnitSpatialGrid &grid,
                                    const MapGenerator &generator,
                                    const NavigationService &nav)
{
    // Задачи:
    // 1. Оценить каждый эвакуационный выход по дистанции и угрозе зомби в коридоре к нему
    // 2. Вернуть ID самого безопасного выхода (с гистерезисом -25 м для текущей цели)

    const auto &nodes = generator.nodes();
    int bestNode = -1;
    float bestScore = 1e9F;

    for (const auto &node : nodes) {
        if (!node.isEvacExit)
            continue;

        const float ex = static_cast<float>(node.pos.x());
        const float ey = static_cast<float>(node.pos.y());
        const float distToExit = std::hypot(ex - u.x, ey - u.y);
        float threatPenalty = 0.0F;

        for (size_t zIdx : grid.activeZombies()) {
            const Unit &z = state.units[zIdx];
            if (z.hp <= 0)
                continue;

            const float dZombieExit = std::hypot(z.x - ex, z.y - ey);
            if (dZombieExit < 45.0F)
                threatPenalty += (45.0F - dZombieExit) * 6.0F;

            const float dZombieSoldier = std::hypot(z.x - u.x, z.y - u.y);
            if (dZombieSoldier < 55.0F && distToExit > 1.0F && dZombieSoldier > 0.1F
                && nav.hasLineOfSight(u.x, u.y, z.x, z.y, state.navGrid)) {
                const float dot = ((ex - u.x) / distToExit) * ((z.x - u.x) / dZombieSoldier)
                                  + ((ey - u.y) / distToExit) * ((z.y - u.y) / dZombieSoldier);
                if (dot > 0.5F)
                    threatPenalty += dot * (55.0F - dZombieSoldier) * 4.5F;
            }
        }

        const float hysteresis = (u.targetBuildingId == node.id) ? -25.0F : 0.0F;
        const float score = distToExit + threatPenalty + hysteresis;

        if (score < bestScore) {
            bestScore = score;
            bestNode = node.id;
        }
    }

    return bestNode;
}

void SoldierAI::applyEvacAvoidance(Unit &u,
                                   const WorldState &state,
                                   const UnitSpatialGrid &grid,
                                   const NavigationService &nav)
{
    // Задачи:
    // 1. Вычислить вектор отталкивания и бокового обхода от видимых зомби на улице
    // 2. Скорректировать скорость солдата, сохраняя приоритет движения к эвакуации

    if (u.insideBuildingId != -1)
        return;

    const float speed = std::hypot(u.vx, u.vy);
    if (speed < 0.1F)
        return;

    const float dirX = u.vx / speed;
    const float dirY = u.vy / speed;
    float avoidX = 0.0F;
    float avoidY = 0.0F;
    bool hasThreat = false;

    for (size_t zIdx : grid.activeZombies()) {
        const Unit &z = state.units[zIdx];
        if (z.hp <= 0 || z.insideBuildingId != -1)
            continue;

        const float dx = u.x - z.x;
        const float dy = u.y - z.y;
        const float dist = std::hypot(dx, dy);

        if (dist <= 38.0F && dist > 0.1F && nav.hasLineOfSight(u.x, u.y, z.x, z.y, state.navGrid)) {
            hasThreat = true;
            const float weight = (38.0F - dist) / 38.0F;
            const float awayX = dx / dist;
            const float awayY = dy / dist;

            float tangX = -awayY;
            float tangY = awayX;
            if (tangX * dirX + tangY * dirY < 0.0F) {
                tangX = -tangX;
                tangY = -tangY;
            }

            avoidX += (awayX * 1.1F + tangX * 0.9F) * weight;
            avoidY += (awayY * 1.1F + tangY * 0.9F) * weight;
        }
    }

    if (hasThreat) {
        const float finalX = dirX * 1.15F + avoidX;
        const float finalY = dirY * 1.15F + avoidY;
        const float len = std::hypot(finalX, finalY);
        if (len > 0.01F) {
            u.vx = (finalX / len) * UnitMovement::ZOMBIE_CHASE_SPEED;
            u.vy = (finalY / len) * UnitMovement::ZOMBIE_CHASE_SPEED;
        }
    }
}

int SoldierAI::findFireTarget(const Unit &u,
                              const WorldState &state,
                              const UnitSpatialGrid &grid,
                              const NavigationService &nav)
{
    // Задачи:
    // 1. Найти ближайшего видимого зомби в радиусе 35 м (сквозь карантинные стены, но не дома)
    // 2. Если зомби нет — найти ближайшего инфицированного жителя в радиусе 35 м

    const float shootRadiusSq = 35.0F * 35.0F;

    auto canShootTarget = [&](const Unit &target) -> bool {
        if (u.insideBuildingId != target.insideBuildingId)
            return false;
        if (u.insideBuildingId != -1)
            return true;
        return nav.hasLineOfSightIgnoringWalls(u.x, u.y, target.x, target.y, state.navGrid);
    };

    int bestZombie = -1;
    float bestZombieDistSq = shootRadiusSq;

    const int sCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int sCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    for (int dy = -1; dy <= 1; ++dy) {
        const int ny = sCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;
        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = sCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (size_t zIdx : grid.zombieGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const Unit &z = state.units[zIdx];
                if (z.hp <= 0)
                    continue;

                const float ddx = z.x - u.x;
                const float ddy = z.y - u.y;
                const float dSq = ddx * ddx + ddy * ddy;
                if (dSq >= bestZombieDistSq)
                    continue;

                if (canShootTarget(z)) {
                    bestZombieDistSq = dSq;
                    bestZombie = static_cast<int>(zIdx);
                }
            }
        }
    }

    if (bestZombie != -1)
        return bestZombie;

    int bestInfected = -1;
    float bestInfectedDistSq = shootRadiusSq;

    for (size_t i = 0; i < state.units.size(); ++i) {
        const Unit &cand = state.units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;

        const float dx = cand.x - u.x;
        const float dy = cand.y - u.y;
        const float dSq = dx * dx + dy * dy;
        if (dSq >= bestInfectedDistSq)
            continue;

        if (canShootTarget(cand)) {
            bestInfectedDistSq = dSq;
            bestInfected = static_cast<int>(i);
        }
    }

    return bestInfected;
}

void SoldierAI::fireAtTarget(Unit &soldier,
                             size_t targetIdx,
                             WorldState &state,
                             EffectsSystem &effects)
{
    // Задачи:
    // 1. Проверить дистанцию (35 м) и перезарядку (1.0 с)
    // 2. Создать трассер пули и нанести 1 ед. урона цели

    Unit &target = state.units[targetIdx];
    const float dist = std::hypot(target.x - soldier.x, target.y - soldier.y);

    if (dist > 35.0F || soldier.attackCooldownSec > 0.0F)
        return;

    soldier.attackCooldownSec = 1.0F;
    effects.spawnBulletTracer(soldier.x, soldier.y, target.x, target.y);
    target.hp -= 1;

    if (target.hp <= 0)
        effects.spawnBurstParticles(target.x, target.y, 8, 255, 60, 60);
}

void SoldierAI::adjustCombatPosition(Unit &u, float targetX, float targetY, float dist)
{
    // Задачи:
    // 1. Подойти ближе, если цель дальше оптимальной дальности огня
    // 2. Отойти назад, если враг подошёл вплотную (внутри здания дистанция короче)

    const float maxRange = (u.insideBuildingId != -1) ? 6.0F : 20.0F;
    const float minRange = (u.insideBuildingId != -1) ? 2.5F : 15.0F;
    const float dx = targetX - u.x;
    const float dy = targetY - u.y;

    if (dist > maxRange && dist > 0.1F) {
        u.vx = (dx / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
        u.vy = (dy / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
    } else if (dist < minRange && dist > 0.1F) {
        u.vx = -(dx / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
        u.vy = -(dy / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
    } else {
        u.vx = 0.0F;
        u.vy = 0.0F;
    }
}

int SoldierAI::findHuntTarget(const Unit &u, const WorldState &state, const UnitSpatialGrid &grid)
{
    // Задачи:
    // 1. Найти ближайшего живого зомби в соседних ячейках сетки (радиус до 300 м)
    // 2. Если зомби нет — найти ближайшего инфицированного жителя

    int bestIdx = -1;
    float bestDistSq = 300.0F * 300.0F;

    const int sCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int sCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    for (int dy = -2; dy <= 2; ++dy) {
        const int ny = sCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;
        for (int dx = -2; dx <= 2; ++dx) {
            const int nx = sCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (size_t zIdx : grid.zombieGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const Unit &z = state.units[zIdx];
                if (z.hp <= 0)
                    continue;

                const float ddx = z.x - u.x;
                const float ddy = z.y - u.y;
                const float dSq = ddx * ddx + ddy * ddy;
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    bestIdx = static_cast<int>(zIdx);
                }
            }
        }
    }

    if (bestIdx != -1)
        return bestIdx;

    float bestInfDistSq = 1e18F;
    for (size_t i = 0; i < state.units.size(); ++i) {
        const Unit &cand = state.units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;
        const float dx = cand.x - u.x;
        const float dy = cand.y - u.y;
        const float dSq = dx * dx + dy * dy;
        if (dSq < bestInfDistSq) {
            bestInfDistSq = dSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

void SoldierAI::routeToTarget(Unit &u,
                              const Unit &target,
                              const MapGenerator &generator,
                              const NavigationService &nav,
                              const NavGrid &navGrid)
{
    // Задачи:
    // 1. Обновить конечную точку, если цель сместилась незначительно или активен кулдаун
    // 2. Построить маршрут с обходом карантинных стен
    // 3. Если цель изолирована за стенами — выбрать ближайшую дорогу с прострелом через стену

    if (!u.waypoints.empty()) {
        const QPointF lastPt = u.waypoints.back();
        const float targetShift = std::hypot(static_cast<float>(lastPt.x()) - target.x,
                                             static_cast<float>(lastPt.y()) - target.y);
        if (targetShift <= 20.0F || u.repathCooldownSec > 0.0F) {
            if (target.insideBuildingId == -1
                && nav.hasWalkableLineOfSight(static_cast<float>(lastPt.x()),
                                              static_cast<float>(lastPt.y()),
                                              target.x,
                                              target.y,
                                              navGrid)) {
                u.waypoints.back() = QPointF(target.x, target.y);
            }
            return;
        }
    }

    u.repathCooldownSec = 0.35F;
    std::vector<QPointF> newPath = nav.buildPathBetween(u.x,
                                                        u.y,
                                                        target.x,
                                                        target.y,
                                                        generator,
                                                        navGrid,
                                                        false);

    if (newPath.empty()) {
        const auto &nodes = generator.nodes();
        float bestScore = 1e9F;

        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].isAlleyNode)
                continue;

            const float nx = static_cast<float>(nodes[i].pos.x());
            const float ny = static_cast<float>(nodes[i].pos.y());
            const float dToTarget = std::hypot(nx - target.x, ny - target.y);
            if (dToTarget > 90.0F)
                continue;

            const bool canShootFromNode
                = (dToTarget <= 34.0F
                   && nav.hasLineOfSightIgnoringWalls(nx, ny, target.x, target.y, navGrid));
            const float score = dToTarget + (canShootFromNode ? 0.0F : 40.0F);
            if (score < bestScore) {
                auto candidatePath = nav.buildPathBetween(u.x,
                                                          u.y,
                                                          nx,
                                                          ny,
                                                          generator,
                                                          navGrid,
                                                          false);
                if (!candidatePath.empty()) {
                    bestScore = score;
                    newPath = std::move(candidatePath);
                }
            }
        }
    }

    if (u.insideBuildingId == -1 && newPath.size() >= 2) {
        const float segLen = static_cast<float>(
            std::hypot(newPath.at(1).x() - newPath.at(0).x(),
                       newPath.at(1).y() - newPath.at(0).y()));
        const float distToSecond = static_cast<float>(
            std::hypot(newPath.at(1).x() - u.x, newPath.at(1).y() - u.y));
        if (distToSecond < segLen
            && nav.hasWalkableLineOfSight(u.x,
                                          u.y,
                                          static_cast<float>(newPath.at(1).x()),
                                          static_cast<float>(newPath.at(1).y()),
                                          navGrid)) {
            newPath.erase(newPath.begin());
        }
    }

    if (!newPath.empty())
        u.waypoints = std::move(newPath);
}

bool SoldierAI::followWaypoints(Unit &u, const MapGenerator &generator)
{
    // Задачи:
    // 1. Определить радиус достижения узла (увеличен на широкой дороге)
    // 2. Удалять пройденные точки и задавать вектор скорости к следующей

    if (u.waypoints.empty())
        return false;

    const auto &walkGrid = generator.walkGrid();
    const int mapW = generator.width();
    const int mapH = generator.height();
    const int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
    const int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);

    const bool onMainRoad = (walkGrid[static_cast<size_t>(gy) * mapW + gx] == 1);
    const float reachRadius = (onMainRoad && u.waypoints.size() > 1)
                                  ? (generator.roadWidth() * 0.35F)
                                  : 2.5F;

    QPointF target = u.waypoints.front();
    float dx = static_cast<float>(target.x()) - u.x;
    float dy = static_cast<float>(target.y()) - u.y;
    float dist = std::hypot(dx, dy);

    if (dist <= reachRadius) {
        u.waypoints.erase(u.waypoints.begin());
        if (!u.waypoints.empty()) {
            target = u.waypoints.front();
            dx = static_cast<float>(target.x()) - u.x;
            dy = static_cast<float>(target.y()) - u.y;
            dist = std::hypot(dx, dy);
        }
    }
    if (dist > 0.1F) {
        u.vx = (dx / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
        u.vy = (dy / dist) * UnitMovement::ZOMBIE_CHASE_SPEED;
    }
    return true;
}

void SoldierAI::patrol(Unit &u,
                       const std::vector<BuildingObject> &buildings,
                       const MapGenerator &generator,
                       const NavigationService &nav,
                       const NavGrid &navGrid)
{
    // Задачи:
    // 1. Вывести солдата из пустого здания на дорогу
    // 2. Выбрать случайный достижимый дорожный узел (!isAlleyNode) для патруля

    if (u.insideBuildingId != -1 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        const auto &b = buildings[u.insideBuildingId];
        for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
            u.waypoints.push_back(b.pathFromRoad[p]);
        return;
    }

    if (u.stateTimerSec > 0.0F && std::hypot(u.vx, u.vy) > 1.0F)
        return;

    const auto &nodes = generator.nodes();
    if (nodes.empty())
        return;

    auto *rng = QRandomGenerator::global();

    for (int attempt = 0; attempt < 6; ++attempt) {
        const int randNode = rng->bounded(static_cast<int>(nodes.size()));
        if (nodes[randNode].isAlleyNode)
            continue;
        if (std::hypot(static_cast<float>(nodes[randNode].pos.x()) - u.x,
                       static_cast<float>(nodes[randNode].pos.y()) - u.y)
            < 10.0F)
            continue;

        auto path = nav.buildPathBetween(QPointF(u.x, u.y),
                                         nodes[randNode].pos,
                                         generator,
                                         navGrid,
                                         false);
        if (!path.empty()) {
            u.waypoints = std::move(path);
            return;
        }
    }

    std::vector<int> reachableRoadNodes;
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isAlleyNode)
            continue;
        const float d = std::hypot(static_cast<float>(nodes[i].pos.x()) - u.x,
                                   static_cast<float>(nodes[i].pos.y()) - u.y);
        if (d < 8.0F)
            continue;

        auto path = nav.buildPathBetween(QPointF(u.x, u.y),
                                         nodes[i].pos,
                                         generator,
                                         navGrid,
                                         false);
        if (!path.empty())
            reachableRoadNodes.push_back(static_cast<int>(i));
    }

    if (!reachableRoadNodes.empty()) {
        const int chosen = reachableRoadNodes[rng->bounded(
            static_cast<int>(reachableRoadNodes.size()))];
        u.waypoints = nav.buildPathBetween(QPointF(u.x, u.y),
                                           nodes[chosen].pos,
                                           generator,
                                           navGrid,
                                           false);
        return;
    }

    float angle = static_cast<float>(rng->generateDouble() * 6.28318);
    for (int attempt = 0; attempt < 12; ++attempt) {
        const float testAngle = static_cast<float>(rng->generateDouble() * 6.28318);
        if (navGrid.isWalkable(u.x + std::cos(testAngle) * 10.0F,
                               u.y + std::sin(testAngle) * 10.0F)) {
            angle = testAngle;
            break;
        }
    }
    u.vx = std::cos(angle) * UnitMovement::BASE_CITIZEN_SPEED;
    u.vy = std::sin(angle) * UnitMovement::BASE_CITIZEN_SPEED;
    u.stateTimerSec = 2.0F;
}

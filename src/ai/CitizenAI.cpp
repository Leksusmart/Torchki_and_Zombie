#include "ai/CitizenAI.h"
#include <QRandomGenerator>
#include <algorithm>
#include <cmath>
#include "MapGenerator.h"
#include "ai/UnitMovement.h"
#include "ai/UnitSpatialGrid.h"
#include "core/NavGrid.h"
#include "core/WorldState.h"
#include "systems/EvacuationSystem.h"
#include "systems/NavigationService.h"

void CitizenAI::infectUnit(Unit &u, std::vector<Unit> &allUnits, bool immediateZombie)
{
    // Задачи:
    // 1. Игнорировать уже обращённых, вакцинированных и солдат
    // 2. При immediateZombie=true сразу превратить в зомби и сбросить след у преследователей
    // 3. Иначе — запустить таймер инкубации на 3–10 игровых часов

    if (u.type == UnitType::Zombie || u.type == UnitType::Vaccinated || u.type == UnitType::Soldier)
        return;

    if (immediateZombie) {
        u.type = UnitType::Zombie;
        u.hp = 5;
        u.isInfected = false;
        u.panicTimerSec = 0.0F;
        u.hasLastKnownTarget = false;
        u.targetBuildingId = -1;
        u.waypoints.clear();

        // Сбрасываем старый след у зомби, которые преследовали этого жителя
        for (auto &other : allUnits) {
            if (other.type == UnitType::Zombie && other.hasLastKnownTarget) {
                const float d = static_cast<float>(
                    std::hypot(other.lastKnownTargetPos.x() - u.x,
                               other.lastKnownTargetPos.y() - u.y));
                if (d < 15.0F) {
                    other.hasLastKnownTarget = false;
                    other.waypoints.clear();
                }
            }
        }
    } else if (!u.isInfected) {
        auto *rng = QRandomGenerator::global();
        u.isInfected = true;
        u.infectionHoursLeft = 3.0F + static_cast<float>(rng->generateDouble() * 7.0);
    }
}

void CitizenAI::updateBehavior(Unit &u,
                               size_t unitIndex,
                               float dt,
                               WorldState &state,
                               std::vector<BuildingObject> &buildings,
                               const UnitSpatialGrid &grid,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EvacuationSystem &evacSys,
                               bool &statsDirty)
{
    // Задачи:
    // 1. Приоритетно обработать активную эвакуацию
    // 2. Разделить логику на нахождение внутри здания и на улице

    if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
        updateSchedule(u, state, buildings, generator, nav, evacSys, statsDirty);
        return;
    }

    const bool isInsideBuilding = (u.insideBuildingId >= 0
                                   && u.insideBuildingId < static_cast<int>(buildings.size()));
    if (isInsideBuilding) {
        updateIndoorBehavior(u, dt, state, buildings, grid, generator, nav, evacSys, statsDirty);
    } else {
        updateOutdoorBehavior(u,
                              unitIndex,
                              dt,
                              state,
                              buildings,
                              grid,
                              generator,
                              nav,
                              evacSys,
                              statsDirty);
    }
}

void CitizenAI::updateIndoorBehavior(Unit &u,
                                     float dt,
                                     WorldState &state,
                                     std::vector<BuildingObject> &buildings,
                                     const UnitSpatialGrid &grid,
                                     const MapGenerator &generator,
                                     const NavigationService &nav,
                                     EvacuationSystem &evacSys,
                                     bool &statsDirty)
{
    // Задачи:
    // 1. При пожаре или зомби внутри комнаты — паниковать и бежать наружу по pathFromRoad
    // 2. При атаке двери или видимом снаружи зомби — отбегать вглубь здания
    // 3. В спокойной обстановке следовать обычному расписанию

    auto *rng = QRandomGenerator::global();
    const auto &b = buildings[u.insideBuildingId];

    bool zombieInsideRoom = false;
    for (size_t zIdx : grid.activeZombies()) {
        if (state.units[zIdx].insideBuildingId == u.insideBuildingId) {
            zombieInsideRoom = true;
            break;
        }
    }

    // 1. Здание горит / обуглено / зомби внутри -> бежим к выходу
    if (b.isBurning || b.isBurnedOut || zombieInsideRoom) {
        u.panicTimerSec = 3.0F;
        u.state = UnitState::Walking;
        u.stateTimerSec = 6.0F;

        if (u.waypoints.empty()) {
            for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                u.waypoints.push_back(b.pathFromRoad[p]);
        }

        float routeDirX = 0.0F;
        float routeDirY = 0.0F;
        if (!u.waypoints.empty()) {
            const QPointF target = u.waypoints.front();
            const float dx = static_cast<float>(target.x()) - u.x;
            const float dy = static_cast<float>(target.y()) - u.y;
            const float dist = std::hypot(dx, dy);
            if (dist < 1.5F) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                routeDirX = dx / dist;
                routeDirY = dy / dist;
            }
        }

        float avoidX = 0.0F;
        float avoidY = 0.0F;
        if (zombieInsideRoom) {
            for (size_t zIdx : grid.activeZombies()) {
                if (state.units[zIdx].insideBuildingId != u.insideBuildingId)
                    continue;
                const float zx = u.x - state.units[zIdx].x;
                const float zy = u.y - state.units[zIdx].y;
                const float zDist = std::hypot(zx, zy);
                if (zDist <= 16.0F && zDist > 0.1F) {
                    const float weight = (16.0F - zDist) / 16.0F;
                    avoidX += (zx / zDist) * weight * 1.3F;
                    avoidY += (zy / zDist) * weight * 1.3F;
                }
            }
        }

        const float finalX = routeDirX * 1.3F + avoidX;
        const float finalY = routeDirY * 1.3F + avoidY;
        const float finalLen = std::hypot(finalX, finalY);
        if (finalLen > 0.01F) {
            u.vx = (finalX / finalLen) * UnitMovement::RUN_SPEED;
            u.vy = (finalY / finalLen) * UnitMovement::RUN_SPEED;
        }
        return;
    }

    // 2. Зомби ломает дверь или виден через проём -> отходим к центру комнаты
    bool seesZombieOutside = false;
    float outThreatX = 0.0F;
    float outThreatY = 0.0F;

    const int cCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int cCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    for (int dy = -1; dy <= 1 && !seesZombieOutside; ++dy) {
        const int ny = cCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;
        for (int dx = -1; dx <= 1 && !seesZombieOutside; ++dx) {
            const int nx = cCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (size_t zIdx : grid.zombieGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const Unit &z = state.units[zIdx];
                if (z.insideBuildingId != -1)
                    continue;
                const float ddx = z.x - u.x;
                const float ddy = z.y - u.y;
                if (ddx * ddx + ddy * ddy <= 45.0F * 45.0F
                    && nav.hasLineOfSight(u.x, u.y, z.x, z.y, state.navGrid)) {
                    seesZombieOutside = true;
                    outThreatX = z.x;
                    outThreatY = z.y;
                    break;
                }
            }
        }
    }

    if (b.underAttackTimerSec > 0.0F || seesZombieOutside) {
        u.panicTimerSec = 2.0F;
        u.waypoints.clear();

        const float panicSpeed = UnitMovement::RUN_SPEED;
        if (seesZombieOutside) {
            const float awayX = (u.x - outThreatX) + (b.cx - u.x) * 0.8F;
            const float awayY = (u.y - outThreatY) + (b.cy - u.y) * 0.8F;
            const float len = std::hypot(awayX, awayY);
            if (len > 0.01F) {
                u.vx = (awayX / len) * panicSpeed;
                u.vy = (awayY / len) * panicSpeed;
            }
        } else {
            const float curSpd = std::hypot(u.vx, u.vy);
            if (curSpd < 1.0F || rng->bounded(100) < 12) {
                const float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                u.vx = std::cos(angle) * panicSpeed;
                u.vy = std::sin(angle) * panicSpeed;
            }
        }

        // Удерживаем жителя внутри помещения, чтобы не выбежал в дверной проём
        const float lookAheadX = u.x + (u.vx / panicSpeed) * 2.5F;
        const float lookAheadY = u.y + (u.vy / panicSpeed) * 2.5F;
        if (state.navGrid.buildingIdAt(static_cast<int>(lookAheadX), static_cast<int>(lookAheadY))
            != u.insideBuildingId) {
            const float toCenter = std::atan2(b.cy - u.y, b.cx - u.x)
                                   + static_cast<float>((rng->generateDouble() - 0.5) * 1.2);
            u.vx = std::cos(toCenter) * panicSpeed;
            u.vy = std::sin(toCenter) * panicSpeed;
        }
        return;
    }

    // 3. Спокойная обстановка -> расписание
    if (u.panicTimerSec > 0.0F)
        u.panicTimerSec = std::max(0.0F, u.panicTimerSec - dt);

    updateSchedule(u, state, buildings, generator, nav, evacSys, statsDirty);
}

void CitizenAI::updateOutdoorBehavior(Unit &u,
                                      size_t unitIndex,
                                      float dt,
                                      WorldState &state,
                                      std::vector<BuildingObject> &buildings,
                                      const UnitSpatialGrid &grid,
                                      const MapGenerator &generator,
                                      const NavigationService &nav,
                                      EvacuationSystem &evacSys,
                                      bool &statsDirty)
{
    // Задачи:
    // 1. Проверить наличие видимых зомби на улице в радиусе 45 м
    // 2. При отсутствии прямого зомби — проверить заразительность паники толпы
    // 3. При угрозе убегать от источника опасности, иначе следовать расписанию

    auto *rng = QRandomGenerator::global();
    bool hasThreat = false;
    float threatX = 0.0F;
    float threatY = 0.0F;

    const int cCellX = std::clamp(static_cast<int>(u.x) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.cols() - 1);
    const int cCellY = std::clamp(static_cast<int>(u.y) / UnitSpatialGrid::CELL_SIZE,
                                  0,
                                  grid.rows() - 1);

    // 1. Прямая видимость зомби
    for (int dy = -1; dy <= 1 && !hasThreat; ++dy) {
        const int ny = cCellY + dy;
        if (ny < 0 || ny >= grid.rows())
            continue;
        for (int dx = -1; dx <= 1 && !hasThreat; ++dx) {
            const int nx = cCellX + dx;
            if (nx < 0 || nx >= grid.cols())
                continue;

            for (size_t zIdx : grid.zombieGrid()[static_cast<size_t>(ny * grid.cols() + nx)]) {
                const Unit &z = state.units[zIdx];
                if (z.insideBuildingId != -1)
                    continue;
                const float ddx = z.x - u.x;
                const float ddy = z.y - u.y;
                if (ddx * ddx + ddy * ddy <= 45.0F * 45.0F
                    && nav.hasLineOfSight(u.x, u.y, z.x, z.y, state.navGrid)) {
                    hasThreat = true;
                    threatX = z.x;
                    threatY = z.y;
                    break;
                }
            }
        }
    }

    // 2. Паника толпы
    if (!hasThreat) {
        int nearbyPanic = 0;
        float avgPx = 0.0F;
        float avgPy = 0.0F;
        for (size_t pIdx : grid.panickingOutdoors()) {
            if (pIdx == unitIndex)
                continue;
            const Unit &other = state.units[pIdx];
            const float dx = other.x - u.x;
            const float dy = other.y - u.y;
            if (dx * dx + dy * dy <= 35.0F * 35.0F
                && nav.hasLineOfSight(u.x, u.y, other.x, other.y, state.navGrid)) {
                nearbyPanic++;
                avgPx += other.x;
                avgPy += other.y;
            }
        }
        if (nearbyPanic > 10 || (nearbyPanic >= 5 && rng->bounded(100) < 50)) {
            hasThreat = true;
            threatX = avgPx / nearbyPanic;
            threatY = avgPy / nearbyPanic;
        }
    }

    // 3. Бегство от угрозы
    if (hasThreat) {
        u.panicTimerSec = 3.0F;
        u.waypoints.clear();
        const float baseAway = std::atan2(u.y - threatY, u.x - threatX);
        float bestAngle = baseAway;
        for (int attempt = 0; attempt < 5; ++attempt) {
            const float testAngle = baseAway
                                    + static_cast<float>((rng->generateDouble() - 0.5) * 1.4);
            const float testX = u.x + std::cos(testAngle) * 6.0F;
            const float testY = u.y + std::sin(testAngle) * 6.0F;
            if (state.navGrid.isWalkable(testX, testY)) {
                bestAngle = testAngle;
                break;
            }
        }
        u.vx = std::cos(bestAngle) * UnitMovement::RUN_SPEED;
        u.vy = std::sin(bestAngle) * UnitMovement::RUN_SPEED;
        return;
    }

    if (u.panicTimerSec > 0.0F) {
        u.panicTimerSec -= dt;
        if (u.panicTimerSec <= 0.0F) {
            u.panicTimerSec = 0.0F;
            u.state = UnitState::Walking;
            u.stateTimerSec = 2.0F;
        }
        return;
    }

    updateSchedule(u, state, buildings, generator, nav, evacSys, statsDirty);
}

void CitizenAI::updateSchedule(Unit &u,
                               WorldState &state,
                               std::vector<BuildingObject> &buildings,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EvacuationSystem &evacSys,
                               bool &statsDirty)
{
    // Задачи:
    // 1. Обработать мгновенные перемещения (бег в панике, эвакуация, дорога на работу/домой, прогулка)
    // 2. По истечении stateTimerSec переключить фазу распорядка дня

    auto *rng = QRandomGenerator::global();
    const bool isInsideBuilding = (u.insideBuildingId >= 0
                                   && u.insideBuildingId < static_cast<int>(buildings.size()));
    const bool isHomeBuilding = isInsideBuilding
                                && (buildings[u.insideBuildingId].type == BuildingType::Residential);

    const bool shouldBeAtWork = u.isNightWorker
                                    ? (state.timeOfDayHours >= 20.0F || state.timeOfDayHours < 6.0F)
                                    : (state.timeOfDayHours >= 8.0F && state.timeOfDayHours < 18.0F);

    // Бег от зомби по маршруту паники
    if (!u.waypoints.empty() && u.panicTimerSec > 0.0F && u.state != UnitState::PanicEvacuating) {
        const QPointF target = u.waypoints.front();
        const float dx = static_cast<float>(target.x()) - u.x;
        const float dy = static_cast<float>(target.y()) - u.y;
        const float dist = std::hypot(dx, dy);
        if (dist < 1.5F) {
            u.waypoints.erase(u.waypoints.begin());
        } else {
            u.vx = (dx / dist) * UnitMovement::RUN_SPEED;
            u.vy = (dy / dist) * UnitMovement::RUN_SPEED;
        }
        return;
    }

    // Движение к эвакуационному выходу
    if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
        handleEvacuationMove(u, state, generator, nav, evacSys, statsDirty);
    }

    // Движение на работу / домой
    if ((u.state == UnitState::GoingToWork && !shouldBeAtWork)
        || (u.state == UnitState::GoingHome && shouldBeAtWork)) {
        u.stateTimerSec = 0.0F;
    } else if (u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome) {
        if (!u.waypoints.empty()) {
            const QPointF baseTarget = u.waypoints.front();
            const float dx = static_cast<float>(baseTarget.x()) - u.x;
            const float dy = static_cast<float>(baseTarget.y()) - u.y;
            const float dist = std::hypot(dx, dy);

            const bool onMainRoad = state.navGrid.hasFlag(static_cast<int>(u.x),
                                                          static_cast<int>(u.y),
                                                          NavCellFlag::Road);
            const float reachR = onMainRoad ? (generator.roadWidth() * 0.35F) : 1.5F;

            if (dist < reachR) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (dx / dist) * UnitMovement::BASE_CITIZEN_SPEED;
                u.vy = (dy / dist) * UnitMovement::BASE_CITIZEN_SPEED;
            }
        }
        if (isInsideBuilding && u.insideBuildingId == u.targetBuildingId) {
            u.state = (u.state == UnitState::GoingToWork) ? UnitState::Working : UnitState::AtHome;
            u.stateTimerSec = static_cast<float>(15 + rng->bounded(21));
            u.waypoints = {QPointF(buildings[u.targetBuildingId].cx,
                                   buildings[u.targetBuildingId].cy)};
        }
    }

    // Движение внутри здания (на работе или дома)
    if (isInsideBuilding
        && ((u.state == UnitState::Working && !isHomeBuilding)
            || (u.state == UnitState::AtHome && isHomeBuilding))) {
        const float speed = std::hypot(u.vx, u.vy);
        if (!u.waypoints.empty()) {
            const QPointF target = u.waypoints.front();
            const float dx = static_cast<float>(target.x()) - u.x;
            const float dy = static_cast<float>(target.y()) - u.y;
            const float dist = std::hypot(dx, dy);
            if (dist < 3.0F) {
                u.waypoints.clear();
            } else if (dist > 0.01F) {
                u.vx = (dx / dist) * UnitMovement::BASE_CITIZEN_SPEED;
                u.vy = (dy / dist) * UnitMovement::BASE_CITIZEN_SPEED;
            }
        } else if (speed < 0.5F || rng->bounded(100) < 5) {
            const float angle = static_cast<float>(rng->generateDouble() * 6.28318);
            u.vx = std::cos(angle) * UnitMovement::BASE_CITIZEN_SPEED;
            u.vy = std::sin(angle) * UnitMovement::BASE_CITIZEN_SPEED;
        } else {
            u.vx = (u.vx / speed) * UnitMovement::BASE_CITIZEN_SPEED;
            u.vy = (u.vy / speed) * UnitMovement::BASE_CITIZEN_SPEED;
        }

        // Не даём жителю случайно выйти в дверной проём
        const float lookAheadX = u.x + (u.vx / UnitMovement::BASE_CITIZEN_SPEED) * 2.0F;
        const float lookAheadY = u.y + (u.vy / UnitMovement::BASE_CITIZEN_SPEED) * 2.0F;
        if (state.navGrid.buildingIdAt(static_cast<int>(lookAheadX), static_cast<int>(lookAheadY))
            != u.insideBuildingId) {
            const auto &b = buildings[u.insideBuildingId];
            const float toCenterAngle = std::atan2(b.cy - u.y, b.cx - u.x)
                                        + static_cast<float>((rng->generateDouble() - 0.5) * 1.2);
            u.vx = std::cos(toCenterAngle) * UnitMovement::BASE_CITIZEN_SPEED;
            u.vy = std::sin(toCenterAngle) * UnitMovement::BASE_CITIZEN_SPEED;
        }
    }

    // Движение на прогулке
    if (u.state == UnitState::Walking && u.panicTimerSec <= 0.0F) {
        if (!u.waypoints.empty()) {
            const QPointF target = u.waypoints.front();
            const float dx = static_cast<float>(target.x()) - u.x;
            const float dy = static_cast<float>(target.y()) - u.y;
            const float dist = std::hypot(dx, dy);

            const bool onMainRoad = state.navGrid.hasFlag(static_cast<int>(u.x),
                                                          static_cast<int>(u.y),
                                                          NavCellFlag::Road);
            const float reachR = (onMainRoad && u.waypoints.size() > 1)
                                     ? (generator.roadWidth() * 0.35F)
                                     : 1.5F;

            if (dist < reachR) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (dx / dist) * UnitMovement::BASE_CITIZEN_SPEED;
                u.vy = (dy / dist) * UnitMovement::BASE_CITIZEN_SPEED;
            }
        } else if (u.stateTimerSec <= 0.0F || std::hypot(u.vx, u.vy) < 1.0F) {
            const auto &nodes = generator.nodes();
            if (!nodes.empty()) {
                int randNode = rng->bounded(static_cast<int>(nodes.size()));
                for (int attempt = 0; attempt < 10 && nodes[randNode].isAlleyNode; ++attempt) {
                    randNode = rng->bounded(static_cast<int>(nodes.size()));
                }

                u.waypoints = nav.buildPathBetween(QPointF(u.x, u.y),
                                                   nodes[randNode].pos,
                                                   generator,
                                                   state.navGrid);
                if (u.waypoints.empty()) {
                    float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                    for (int attempt = 0; attempt < 12; ++attempt) {
                        const float testAngle = static_cast<float>(rng->generateDouble() * 6.28318);
                        if (state.navGrid.isWalkable(u.x + std::cos(testAngle) * 10.0F,
                                                     u.y + std::sin(testAngle) * 10.0F)) {
                            angle = testAngle;
                            break;
                        }
                    }
                    u.vx = std::cos(angle) * UnitMovement::BASE_CITIZEN_SPEED;
                    u.vy = std::sin(angle) * UnitMovement::BASE_CITIZEN_SPEED;
                    u.stateTimerSec = 2.5F;
                }
            }
        }
    }

    // 2. Действия по истечении таймера состояния
    if (u.stateTimerSec <= 0.0F) {
        handleScheduleTransition(u,
                                 shouldBeAtWork,
                                 isInsideBuilding,
                                 buildings,
                                 generator,
                                 nav,
                                 state.navGrid);
    }
}

void CitizenAI::handleEvacuationMove(Unit &u,
                                     WorldState &state,
                                     const MapGenerator &generator,
                                     const NavigationService &nav,
                                     EvacuationSystem &evacSys,
                                     bool &statsDirty)
{
    // Задачи:
    // 1. Двигаться по маршруту к точке эвакуации (с уклонением от зомби в режиме PanicEvacuating)
    // 2. При достижении выхода в радиусе 5 м посадить неинфицированного жителя в конвой

    auto *rng = QRandomGenerator::global();
    ActiveEvacuation *targetEvac = evacSys.findEvacuation(u.targetBuildingId);
    if (!targetEvac) {
        u.state = UnitState::Walking;
        u.stateTimerSec = 2.0F;
        return;
    }

    float directionX = 0.0F;
    float directionY = 0.0F;

    if (!u.waypoints.empty()) {
        const QPointF baseTarget = u.waypoints.front();
        const float dx = static_cast<float>(baseTarget.x()) - u.x;
        const float dy = static_cast<float>(baseTarget.y()) - u.y;
        const float dist = std::hypot(dx, dy);

        const bool onMainRoad = state.navGrid.hasFlag(static_cast<int>(u.x),
                                                      static_cast<int>(u.y),
                                                      NavCellFlag::Road);
        const float reachR = onMainRoad ? (generator.roadWidth() * 0.35F) : 1.5F;

        if (dist < reachR) {
            u.waypoints.erase(u.waypoints.begin());
        } else {
            directionX = dx / dist;
            directionY = dy / dist;
        }
    } else {
        const float dx = static_cast<float>(targetEvac->evacPos.x()) - u.x;
        const float dy = static_cast<float>(targetEvac->evacPos.y()) - u.y;
        const float dist = std::hypot(dx, dy);
        if (dist > 0.1F) {
            directionX = dx / dist;
            directionY = dy / dist;
        }
    }

    if (u.state == UnitState::PanicEvacuating) {
        float avoidX = 0.0F;
        float avoidY = 0.0F;
        bool closeZombie = false;

        for (const auto &other : state.units) {
            if (other.type != UnitType::Zombie)
                continue;
            const float zx = u.x - other.x;
            const float zy = u.y - other.y;
            const float zDist = std::hypot(zx, zy);

            if (zDist <= 45.0F && zDist > 0.1F
                && nav.hasLineOfSight(u.x, u.y, other.x, other.y, state.navGrid)) {
                const float weight = (45.0F - zDist) / 45.0F;
                avoidX += (zx / zDist) * weight * 1.6F;
                avoidY += (zy / zDist) * weight * 1.6F;
                if (zDist < 14.0F)
                    closeZombie = true;
            }
        }

        const float finalX = directionX * (closeZombie ? 0.6F : 1.2F) + avoidX;
        const float finalY = directionY * (closeZombie ? 0.6F : 1.2F) + avoidY;
        const float finalLen = std::hypot(finalX, finalY);
        if (finalLen > 0.01F) {
            u.vx = (finalX / finalLen) * UnitMovement::RUN_SPEED;
            u.vy = (finalY / finalLen) * UnitMovement::RUN_SPEED;
        }
    } else if (std::hypot(directionX, directionY) > 0.01F) {
        u.vx = directionX * UnitMovement::RUN_SPEED;
        u.vy = directionY * UnitMovement::RUN_SPEED;
    }

    const float distToExit = static_cast<float>(
        std::hypot(u.x - targetEvac->evacPos.x(), u.y - targetEvac->evacPos.y()));
    if (distToExit <= 5.0F) {
        if (!targetEvac->completed && !u.isInfected
            && targetEvac->boardedCount < generator.level.evac_capacity) {
            targetEvac->boardedCount++;
            state.evacuatedTotalCount++;
            state.budget += 5.0;

            const float rAngle = static_cast<float>(rng->generateDouble() * 6.28318);
            const float rDist = static_cast<float>(std::sqrt(rng->generateDouble()) * 7.5);
            targetEvac->passengerOffsets
                .push_back(QPointF(std::cos(rAngle) * rDist, std::sin(rAngle) * rDist));

            u.isEvacuated = true;
            u.hp = 0;

            if (targetEvac->boardedCount >= generator.level.evac_capacity)
                targetEvac->completed = true;
            statsDirty = true;
        } else {
            u.waypoints.clear();
            u.state = UnitState::Walking;
            u.stateTimerSec = 2.0F;
        }
    }
}

void CitizenAI::handleScheduleTransition(Unit &u,
                                         bool shouldBeAtWork,
                                         bool isInsideBuilding,
                                         std::vector<BuildingObject> &buildings,
                                         const MapGenerator &generator,
                                         const NavigationService &nav,
                                         const NavGrid &navGrid)
{
    auto *rng = QRandomGenerator::global();

    if (((u.state == UnitState::Working || u.state == UnitState::GoingToWork) && !shouldBeAtWork)
        || ((u.state == UnitState::AtHome || u.state == UnitState::GoingHome) && shouldBeAtWork)
        || u.state == UnitState::Walking) {
        const int bId = nav.findNearestAvailableBuilding(u.x,
                                                         u.y,
                                                         shouldBeAtWork,
                                                         generator,
                                                         navGrid);
        if (bId != -1) {
            auto path = nav.buildPathBetween(u.x,
                                             u.y,
                                             buildings[bId].cx,
                                             buildings[bId].cy,
                                             generator,
                                             navGrid);
            if (!path.empty()) {
                buildings[bId].reservedOccupants++;
                u.targetBuildingId = bId;
                u.state = shouldBeAtWork ? UnitState::GoingToWork : UnitState::GoingHome;
                u.waypoints = std::move(path);
                return;
            }
        }

        u.targetBuildingId = -1;
        u.waypoints.clear();
        u.state = UnitState::Walking;
        u.stateTimerSec = 5.0F + rng->bounded(21);
        return;
    }

    if (isInsideBuilding) {
        const bool isHomeBuilding = buildings[u.insideBuildingId].type == BuildingType::Residential;
        if ((u.state == UnitState::Working && !isHomeBuilding)
            || (u.state == UnitState::AtHome && isHomeBuilding)) {
            u.waypoints.clear();
            u.state = UnitState::Walking;
            u.stateTimerSec = static_cast<float>(4 + rng->bounded(9));
        }
    }
}

#pragma once
#include <QRandomGenerator>
#include "GameWorld.h"

void GameWorld::updateCitizenSchedule(Unit &u, float dt, std::vector<BuildingObject> &buildings)
{
    auto *rng = QRandomGenerator::global();
    const auto &walkGrid = g_generator.walkGrid();
    int mapW = g_generator.width();
    int mapH = g_generator.height();

    // Во время паники распорядок дня отключается (кроме панической эвакуации)
    if (u.panicTimerSec > 0.0F && u.state != UnitState::PanicEvacuating) {
        // Если житель в панике находится внутри здания и у него есть маршрут выхода — бежим по нему к двери
        if (!u.waypoints.empty()) {
            QPointF target = u.waypoints.front();
            float dx = static_cast<float>(target.x()) - u.x;
            float dy = static_cast<float>(target.y()) - u.y;
            float dist = std::hypot(dx, dy);
            if (dist < 1.5F) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (dx / dist) * RUN_SPEED;
                u.vy = (dy / dist) * RUN_SPEED;
            }
        }
        return;
    }

    bool shouldBeAtWork = false;
    if (u.isNightWorker) {
        shouldBeAtWork = (g_timeOfDayHours >= 20.0F || g_timeOfDayHours < 6.0F);
    } else {
        shouldBeAtWork = (g_timeOfDayHours >= 8.0F && g_timeOfDayHours < 18.0F);
    }

    // --- 1. ЭВАКУАЦИЯ ---
    if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
        ActiveEvacuation *targetEvac = nullptr;
        for (auto &ev : g_evacs) {
            if (ev.evacNodeId == u.targetBuildingId) {
                targetEvac = &ev;
                break;
            }
        }

        if (!targetEvac) {
            u.state = UnitState::Walking;
            u.stateTimerSec = 3.0F;
            return;
        }

        float rw = roadWidth();
        float routeDirX = 0.0F, routeDirY = 0.0F;

        if (!u.waypoints.empty()) {
            QPointF baseTarget = u.waypoints.front();
            float dx = static_cast<float>(baseTarget.x()) - u.x;
            float dy = static_cast<float>(baseTarget.y()) - u.y;
            float dist = std::hypot(dx, dy);

            int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
            int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
            bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
            float reachR = onMainRoad ? (rw * 0.35F) : 1.5F;

            if (dist < reachR) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                routeDirX = dx / dist;
                routeDirY = dy / dist;
            }
        } else {
            float dx = static_cast<float>(targetEvac->evacPos.x()) - u.x;
            float dy = static_cast<float>(targetEvac->evacPos.y()) - u.y;
            float dist = std::hypot(dx, dy);
            if (dist > 0.1F) {
                routeDirX = dx / dist;
                routeDirY = dy / dist;
            }
        }

        if (u.state == UnitState::PanicEvacuating) {
            float avoidX = 0.0F, avoidY = 0.0F;
            bool closeZombie = false;

            for (const auto &other : g_units) {
                if (other.type != UnitType::Zombie)
                    continue;
                float zx = u.x - other.x;
                float zy = u.y - other.y;
                float zDist = std::hypot(zx, zy);

                if (zDist <= 45.0F && zDist > 0.1F && hasLineOfSight(u.x, u.y, other.x, other.y)) {
                    float weight = (45.0F - zDist) / 45.0F;
                    avoidX += (zx / zDist) * weight * 1.6F;
                    avoidY += (zy / zDist) * weight * 1.6F;
                    if (zDist < 14.0F)
                        closeZombie = true;
                }
            }

            float finalX = routeDirX * (closeZombie ? 0.6F : 1.2F) + avoidX;
            float finalY = routeDirY * (closeZombie ? 0.6F : 1.2F) + avoidY;
            float finalLen = std::hypot(finalX, finalY);

            if (finalLen > 0.01F) {
                u.vx = (finalX / finalLen) * RUN_SPEED;
                u.vy = (finalY / finalLen) * RUN_SPEED;
            }
        } else if (std::hypot(routeDirX, routeDirY) > 0.01F) {
            u.vx = routeDirX * RUN_SPEED;
            u.vy = routeDirY * RUN_SPEED;
        }

        float distToExit = static_cast<float>(
            std::hypot(u.x - targetEvac->evacPos.x(), u.y - targetEvac->evacPos.y()));
        if (distToExit <= 5.0F) {
            if (!targetEvac->completed && !u.isInfected
                && targetEvac->boardedCount < ActiveEvacuation::MAX_CAPACITY) {
                targetEvac->boardedCount++;
                g_evacuatedTotalCount++;
                g_budget += 3.0;

                float rAngle = static_cast<float>(rng->generateDouble() * 6.28318);
                float rDist = static_cast<float>(std::sqrt(rng->generateDouble()) * 7.5);
                targetEvac->passengerOffsets
                    .push_back(QPointF(std::cos(rAngle) * rDist, std::sin(rAngle) * rDist));

                u.hp = 0;
                if (targetEvac->boardedCount >= ActiveEvacuation::MAX_CAPACITY) {
                    targetEvac->completed = true;
                }
                emit statsChanged();
            } else {
                u.state = UnitState::Walking;
                u.stateTimerSec = 4.0F;
                u.waypoints.clear();
            }
        }
        return;
    }

    // Вспомогательная лямбда: если юнит сейчас в здании, добавляем путь выхода по тропинке перед основным маршрутом
    auto prependExitPathIfInside = [&](std::vector<QPointF> &path) {
        if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
            const auto &currB = buildings[u.insideBuildingId];
            std::vector<QPointF> exitPts;
            for (int i = static_cast<int>(currB.pathFromRoad.size()) - 1; i >= 0; --i) {
                exitPts.push_back(currB.pathFromRoad[i]);
            }
            path.insert(path.begin(), exitPts.begin(), exitPts.end());
        }
    };

    // --- 2. СМЕНА РАСПОРЯДКА (ПОРА НА РАБОТУ / ПОРА ДОМОЙ) ---
    // Если юнит гуляет (Walking), переключаем его на работу/дом только когда он уже вышел на дорогу (waypoints пусты)
    bool canSwitchFromWalk = (u.state == UnitState::Walking && u.waypoints.empty());

    if (shouldBeAtWork
        && (u.state == UnitState::AtHome || u.state == UnitState::GoingHome || canSwitchFromWalk)) {
        int workId = findNearestAvailableBuilding(u.x, u.y, true);
        if (workId != -1) {
            buildings[workId].reservedOccupants++; // Мгновенная бронь места!
            u.targetBuildingId = workId;
            u.state = UnitState::GoingToWork;
            u.waypoints = buildPathToBuilding(QPointF(u.x, u.y), buildings[workId]);
            prependExitPathIfInside(u.waypoints);
            return;
        } else if (u.state != UnitState::Walking) {
            u.state = UnitState::Walking;
            u.stateTimerSec = 5.0F;
            u.waypoints.clear();
            prependExitPathIfInside(u.waypoints);
            return;
        }
    } else if (!shouldBeAtWork
               && (u.state == UnitState::Working || u.state == UnitState::GoingToWork
                   || canSwitchFromWalk)) {
        int homeId = findNearestAvailableBuilding(u.x, u.y, false);
        if (homeId != -1) {
            buildings[homeId].reservedOccupants++; // Мгновенная бронь места!
            u.targetBuildingId = homeId;
            u.state = UnitState::GoingHome;
            u.waypoints = buildPathToBuilding(QPointF(u.x, u.y), buildings[homeId]);
            prependExitPathIfInside(u.waypoints);
            return;
        } else if (u.state != UnitState::Walking) {
            u.state = UnitState::Walking;
            u.stateTimerSec = 5.0F;
            u.waypoints.clear();
            prependExitPathIfInside(u.waypoints);
            return;
        }
    }

    // --- 3. БЛУЖДАНИЕ ВНУТРИ ЗДАНИЯ (КАК В WALKING, НО СО СНИЖЕННОЙ СКОРОСТЬЮ) ---
    if (u.state == UnitState::Working || u.state == UnitState::AtHome) {
        u.stateTimerSec -= dt;
        if (u.stateTimerSec <= 0.0F) {
            u.state = UnitState::Walking;
            u.stateTimerSec = static_cast<float>(6 + rng->bounded(9));
            u.waypoints.clear();
            prependExitPathIfInside(u.waypoints);
            return;
        }

        if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
            float indoorSpeed = BASE_CITIZEN_SPEED;

            // Если скорость упала или сработал случайный поворот (как в Walking) — задаём новое случайное направление
            float curSpd = std::hypot(u.vx, u.vy);
            if (curSpd < 0.5F || rng->bounded(100) < 5) {
                float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                u.vx = std::cos(angle) * indoorSpeed;
                u.vy = std::sin(angle) * indoorSpeed;
            } else {
                u.vx = (u.vx / curSpd) * indoorSpeed;
                u.vy = (u.vy / curSpd) * indoorSpeed;
            }

            // Не даём юниту случайно выбрести в дверной проём (walkGrid == 2) во время прогулки по комнате
            float lookAheadX = u.x + (u.vx / indoorSpeed) * 2.0F;
            float lookAheadY = u.y + (u.vy / indoorSpeed) * 2.0F;
            int lx = std::clamp(static_cast<int>(lookAheadX), 0, mapW - 1);
            int ly = std::clamp(static_cast<int>(lookAheadY), 0, mapH - 1);
            if (walkGrid[ly * mapW + lx] != 10 + u.insideBuildingId) {
                // Разворачиваемся к центру текущего здания
                const auto &b = buildings[u.insideBuildingId];
                float toCenterAngle = std::atan2(b.cy - u.y, b.cx - u.x)
                                      + static_cast<float>((rng->generateDouble() - 0.5) * 1.2);
                u.vx = std::cos(toCenterAngle) * indoorSpeed;
                u.vy = std::sin(toCenterAngle) * indoorSpeed;
            }
        } else {
            u.state = UnitState::Walking;
            u.stateTimerSec = 2.0F;
        }
        return;
    }

    // --- 4. СВОБОДНАЯ ПРОГУЛКА ---
    if (u.state == UnitState::Walking) {
        if (!u.waypoints.empty()) {
            QPointF target = u.waypoints.front();
            float dx = static_cast<float>(target.x()) - u.x;
            float dy = static_cast<float>(target.y()) - u.y;
            float dist = std::hypot(dx, dy);
            if (dist < 1.5F) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                u.vx = (dx / dist) * BASE_CITIZEN_SPEED;
                u.vy = (dy / dist) * BASE_CITIZEN_SPEED;
            }
            return;
        }

        u.stateTimerSec -= dt;
        if (u.stateTimerSec <= 0.0F) {
            int bId = findNearestAvailableBuilding(u.x, u.y, shouldBeAtWork);
            if (bId != -1) {
                u.targetBuildingId = bId;
                buildings[bId].reservedOccupants++; // Мгновенная бронь места!
                u.state = shouldBeAtWork ? UnitState::GoingToWork : UnitState::GoingHome;
                u.waypoints = buildPathToBuilding(QPointF(u.x, u.y), buildings[bId]);
                prependExitPathIfInside(u.waypoints);
            } else {
                u.stateTimerSec = 5.0F;
            }
        }
        return;
    }

    // --- 5. ДВИЖЕНИЕ К ЦЕЛЕВОМУ ЗДАНИЮ ---
    if (u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome) {
        bool wantWork = (u.state == UnitState::GoingToWork);

        // Как только юнит пересёк порог и ступил на пол целевого здания — он на месте!
        if (u.targetBuildingId >= 0 && u.insideBuildingId == u.targetBuildingId) {
            const auto &targetB = buildings[u.targetBuildingId];
            u.waypoints.clear();
            u.state = wantWork ? UnitState::Working : UnitState::AtHome;
            u.stateTimerSec = static_cast<float>(15 + rng->bounded(21));

            // Направляем юнита от двери вглубь комнаты
            float toCenter = std::atan2(targetB.cy - u.y, targetB.cx - u.x)
                             + static_cast<float>((rng->generateDouble() - 0.5) * 1.4);
            u.vx = std::cos(toCenter) * BASE_CITIZEN_SPEED;
            u.vy = std::sin(toCenter) * BASE_CITIZEN_SPEED;
            return;
        }

        float rw = roadWidth();
        if (!u.waypoints.empty()) {
            QPointF baseTarget = u.waypoints.front();
            float dx = static_cast<float>(baseTarget.x()) - u.x;
            float dy = static_cast<float>(baseTarget.y()) - u.y;
            float dist = std::hypot(dx, dy);

            int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
            int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
            bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
            float reachRadius = onMainRoad ? (rw * 0.35F) : 1.5F;

            if (dist < reachRadius) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                float targetX = static_cast<float>(baseTarget.x());
                float targetY = static_cast<float>(baseTarget.y());

                if (onMainRoad) {
                    float perpX = -dy / dist;
                    float perpY = dx / dist;
                    float shift = u.laneOffset * rw;
                    float shiftedX = targetX + perpX * shift;
                    float shiftedY = targetY + perpY * shift;
                    if (isWalkable(shiftedX, shiftedY)) {
                        targetX = shiftedX;
                        targetY = shiftedY;
                    }
                }

                float tdx = targetX - u.x;
                float tdy = targetY - u.y;
                float tdist = std::hypot(tdx, tdy);
                if (tdist > 0.1F) {
                    u.vx = (tdx / tdist) * BASE_CITIZEN_SPEED;
                    u.vy = (tdy / tdist) * BASE_CITIZEN_SPEED;
                }
            }
        } else {
            if (u.targetBuildingId >= 0 && u.targetBuildingId < static_cast<int>(buildings.size())) {
                const auto &targetB = buildings[u.targetBuildingId];
                if (targetB.currentOccupants < targetB.capacity && targetB.health > 0
                    && !targetB.isBurning && !targetB.isBurnedOut) {
                    float dx = targetB.cx - u.x;
                    float dy = targetB.cy - u.y;
                    float dist = std::hypot(dx, dy);
                    if (dist > 0.1F) {
                        u.vx = (dx / dist) * BASE_CITIZEN_SPEED;
                        u.vy = (dy / dist) * BASE_CITIZEN_SPEED;
                    }
                    return;
                }
            }

            int nextBId = findNearestAvailableBuilding(u.x, u.y, wantWork);
            if (nextBId != -1) {
                u.targetBuildingId = nextBId;
                u.waypoints = buildPathToBuilding(QPointF(u.x, u.y), buildings[nextBId]);
                prependExitPathIfInside(u.waypoints);
            } else {
                u.state = UnitState::Walking;
                u.stateTimerSec = 10.0F;
            }
        }
    }
}
void GameWorld::infectUnit(Unit &u, bool immediateZombie)
{
    if (u.type == UnitType::Zombie || u.type == UnitType::Vaccinated || u.type == UnitType::Soldier)
        return;
    if (immediateZombie) {
        u.type = UnitType::Zombie;
        u.hp = 5;
        u.isInfected = false;
        u.panicTimerSec = 0.0F;
        u.hasLastKnownTarget = false;
        u.waypoints.clear();
    } else if (!u.isInfected) {
        auto *rng = QRandomGenerator::global();
        u.isInfected = true;
        u.infectionHoursLeft = 3.0F + static_cast<float>(rng->generateDouble() * 7.0);
    }
}
void GameWorld::applyFireAvoidanceAndDamage(Unit &u,
                                            float dt,
                                            const std::vector<BuildingObject> &buildings)
{
    Q_UNUSED(u);
    Q_UNUSED(dt);
    Q_UNUSED(buildings);
    // Огонь служит только визуальным эффектом разрушенного здания и не наносит урон
}
//     if (u.fireDamageCooldownSec > 0.0F) {
//         u.fireDamageCooldownSec -= dt;
//     }
//     float rw = roadWidth();
//     bool isHumanOrSoldier = (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated
//                              || u.type == UnitType::Soldier);
//     for (const auto &b : buildings) {
//         if (!b.isBurning)
//             continue;
//         float lightRadius = 40.0F + b.area / 5.0F;
//         float fireRadius = lightRadius * 0.25F;
//         float avoidRadius = fireRadius + rw * 0.75F;
//         float dx = u.x - b.cx;
//         float dy = u.y - b.cy;
//         float distSq = dx * dx + dy * dy;
//         if (distSq <= fireRadius * fireRadius) {
//             if (u.fireDamageCooldownSec <= 0.0F) {
//                 u.fireDamageCooldownSec = 0.5F;
//                 u.hp -= 2;
//                 spawnBurstParticles(u.x, u.y, 4, 255, 120, 30);
//             }
//         }
//         if (isHumanOrSoldier && distSq <= avoidRadius * avoidRadius && distSq > 0.001F) {
//             float dist = std::sqrt(distSq);
//             float awayX = dx / dist;
//             float awayY = dy / dist;
//             float tangentX = -awayY;
//             float tangentY = awayX;
//             float speed = std::hypot(u.vx, u.vy);
//             if (speed < BASE_CITIZEN_SPEED)
//                 speed = RUN_SPEED;
//             float steerX = awayX * 1.4F + tangentX * 0.6F;
//             float steerY = awayY * 1.4F + tangentY * 0.6F;
//             float steerLen = std::hypot(steerX, steerY);
//             u.vx = (steerX / steerLen) * speed;
//             u.vy = (steerY / steerLen) * speed;
//         }
//     }
// }
void GameWorld::applyWallRepulsion(Unit &u, float dt)
{
    const float probeDist = (u.insideBuildingId != -1) ? 1.2F : (roadWidth() * 0.125F);
    const float pushStrength = (u.insideBuildingId != -1) ? 12.0F : (roadWidth() * 2.2F);
    float pushX = 0.0F;
    float pushY = 0.0F;
    if (!isWalkable(u.x + probeDist, u.y))
        pushX -= pushStrength;
    if (!isWalkable(u.x - probeDist, u.y))
        pushX += pushStrength;
    if (!isWalkable(u.x, u.y + probeDist))
        pushY -= pushStrength;
    if (!isWalkable(u.x, u.y - probeDist))
        pushY += pushStrength;
    if (pushX != 0.0F || pushY != 0.0F) {
        float targetSpeed = std::hypot(u.vx, u.vy);
        u.vx += pushX * dt;
        u.vy += pushY * dt;
        float newSpeed = std::hypot(u.vx, u.vy);
        if (newSpeed > 0.01F && targetSpeed > 0.01F) {
            u.vx = (u.vx / newSpeed) * targetSpeed;
            u.vy = (u.vy / newSpeed) * targetSpeed;
        }
    }
}
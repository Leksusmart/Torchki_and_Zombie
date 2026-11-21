#pragma once
#include <QRandomGenerator>
#include "GameWorld.h"

void GameWorld::updateCitizenSchedule(Unit &u, std::vector<BuildingObject> &buildings)
{
    auto *rng = QRandomGenerator::global();
    const auto &walkGrid = g_generator.walkGrid();
    int mapW = g_generator.width();
    int mapH = g_generator.height();
    bool isInsideBuilding = (u.insideBuildingId >= 0
                             && u.insideBuildingId < static_cast<int>(buildings.size()));
    bool isHomeBuilding = false;
    if (isInsideBuilding)
        isHomeBuilding = buildings[u.insideBuildingId].type == BuildingType::Residential;
    bool shouldBeAtWork = false;
    if (u.isNightWorker) {
        shouldBeAtWork = (g_timeOfDayHours >= 20.0F || g_timeOfDayHours < 6.0F);
    } else {
        shouldBeAtWork = (g_timeOfDayHours >= 8.0F && g_timeOfDayHours < 18.0F);
    }

    // --- МГНОВЕННЫЕ ДЕЙСТВИЯ ---
    {
        // ДВИЖЕНИЕ ОТ ЗОМБИ
        if (!u.waypoints.empty()) {
            if (u.panicTimerSec > 0.0F && u.state != UnitState::PanicEvacuating) {
                // Если житель в панике и у него есть маршрут бежим по нему
                QPointF target = u.waypoints.front();
                float dx = static_cast<float>(target.x()) - u.x;
                float dy = static_cast<float>(target.y()) - u.y;
                float dist = distanceBetween(dx, dy);
                if (dist < 1.5F) {
                    u.waypoints.erase(u.waypoints.begin());
                } else {
                    u.vx = (dx / dist) * RUN_SPEED;
                    u.vy = (dy / dist) * RUN_SPEED;
                }

                return;
            }
        }

        // ДВИЖЕНИЕ К ЭВАКУАЦИОННОМУ ВЫХОДУ
        if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
            ActiveEvacuation *targetEvac = nullptr;
            for (auto &ev : g_evacs) {
                if (ev.evacNodeId == u.targetBuildingId) {
                    targetEvac = &ev;
                    break;
                }
            }

            if (targetEvac) {
                float directionX = 0.0F,
                      directionY = 0.0F; // направление к ближайшей точке маршрута

                // Вычисляем направление к ближайшей точке маршрута, убираем пройденные точки
                if (!u.waypoints.empty()) {
                    QPointF baseTarget = u.waypoints.front();
                    float dx = static_cast<float>(baseTarget.x()) - u.x;
                    float dy = static_cast<float>(baseTarget.y()) - u.y;
                    float dist = distanceBetween(dx, dy);

                    int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
                    int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
                    bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
                    float reachR = onMainRoad ? (roadWidth() * 0.35F) : 1.5F;

                    if (dist < reachR) {
                        u.waypoints.erase(u.waypoints.begin());
                    } else {
                        directionX = dx / dist;
                        directionY = dy / dist;
                    }
                } else {
                    float dx = static_cast<float>(targetEvac->evacPos.x()) - u.x;
                    float dy = static_cast<float>(targetEvac->evacPos.y()) - u.y;
                    float dist = distanceBetween(dx, dy);
                    if (dist > 0.1F) {
                        directionX = dx / dist;
                        directionY = dy / dist;
                    }
                }

                // Рассчитываем направление движение к ближайшей точке маршрута с учётом уклонения от зомби
                if (u.state == UnitState::PanicEvacuating) {
                    float avoidX = 0.0F, avoidY = 0.0F;
                    bool closeZombie = false;

                    // Уклоняемся от зомби
                    for (const auto &other : g_units) {
                        if (other.type != UnitType::Zombie)
                            continue;
                        float zx = u.x - other.x;
                        float zy = u.y - other.y;
                        float zDist = distanceBetween(zx, zy);

                        if (zDist <= 45.0F && zDist > 0.1F
                            && hasLineOfSight(u.x, u.y, other.x, other.y)) {
                            float weight = (45.0F - zDist) / 45.0F;
                            avoidX += (zx / zDist) * weight * 1.6F;
                            avoidY += (zy / zDist) * weight * 1.6F;
                            if (zDist < 14.0F)
                                closeZombie = true;
                        }
                    }

                    float finalX = directionX * (closeZombie ? 0.6F : 1.2F) + avoidX;
                    float finalY = directionY * (closeZombie ? 0.6F : 1.2F) + avoidY;
                    float finalLen = distanceBetween(finalX, finalY);

                    if (finalLen > 0.01F) {
                        u.vx = (finalX / finalLen) * RUN_SPEED;
                        u.vy = (finalY / finalLen) * RUN_SPEED;
                    }
                } else if (distanceBetween(directionX, directionY) > 0.01F) {
                    u.vx = directionX * RUN_SPEED;
                    u.vy = directionY * RUN_SPEED;
                }

                // Эвакуируем не инфицированных
                float distToExit = static_cast<float>(
                    distanceBetween(u.x - targetEvac->evacPos.x(), u.y - targetEvac->evacPos.y()));
                if (distToExit <= 5.0F) {
                    if (!targetEvac->completed && !u.isInfected
                        && targetEvac->boardedCount < g_generator.level.evac_capacity) {
                        targetEvac->boardedCount++;
                        g_evacuatedTotalCount++;
                        g_budget += 5.0; // За каждого юнита 5 руб

                        float rAngle = static_cast<float>(rng->generateDouble() * 6.28318);
                        float rDist = static_cast<float>(std::sqrt(rng->generateDouble()) * 7.5);
                        targetEvac->passengerOffsets
                            .push_back(QPointF(std::cos(rAngle) * rDist, std::sin(rAngle) * rDist));

                        u.isEvacuated = true;
                        u.hp = 0;

                        if (targetEvac->boardedCount >= g_generator.level.evac_capacity) {
                            targetEvac->completed = true;
                        }
                        emit statsChanged();
                    } else {
                        u.waypoints.clear();
                        u.state = UnitState::Walking;
                        u.stateTimerSec = 2.0F;
                    }
                }
            } else { // Если не нашлась точка эвакуации к которой идёт юнит
                u.state = UnitState::Walking;
                u.stateTimerSec = 2.0F;
            }
        }

        // ДВИЖЕНИЕ НА РАБОТУ / ДОМОЙ
        // Если житель шёл на работу, но рабочее время уже закончилось (или наоборот) — сразу перестраиваемся
        if ((u.state == UnitState::GoingToWork && !shouldBeAtWork)
            || (u.state == UnitState::GoingHome && shouldBeAtWork)) {
            u.stateTimerSec = 0.0F;
        } else if (u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome) {
            if (!u.waypoints.empty()) {
                QPointF baseTarget = u.waypoints.front();
                float dx = static_cast<float>(baseTarget.x()) - u.x;
                float dy = static_cast<float>(baseTarget.y()) - u.y;
                float dist = distanceBetween(dx, dy);

                int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
                int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
                bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
                float reachR = onMainRoad ? (roadWidth() * 0.35F) : 1.5F;

                if (dist < reachR) {
                    u.waypoints.erase(u.waypoints.begin());
                } else {
                    u.vx = dx / dist * BASE_CITIZEN_SPEED;
                    u.vy = dy / dist * BASE_CITIZEN_SPEED;
                }
            }
            // Юнит пересёк порог нужного ему здания — он на месте!
            if (isInsideBuilding && u.insideBuildingId == u.targetBuildingId) {
                u.state = u.state == UnitState::GoingToWork ? UnitState::Working
                                                            : UnitState::AtHome;
                u.stateTimerSec = static_cast<float>(15 + rng->bounded(21));
                //  На свякий случай оставляем лишь одну точку в центре здания
                u.waypoints = {QPointF(buildings[u.targetBuildingId].cx,
                                       buildings[u.targetBuildingId].cy)};
            }
        }

        // ДВИЖЕНИЕ ПО ЗДАНИЮ НА РАБОТЕ ИЛИ ДОМА
        if (isInsideBuilding
            && ((u.state == UnitState::Working && !isHomeBuilding)
                || (u.state == UnitState::AtHome && isHomeBuilding))) {
            // Задаём новое случайное направление

            float speed = distanceBetween(u.vx, u.vy);
            if (!u.waypoints.empty()) {
                QPointF target = u.waypoints.front();
                float dx = static_cast<float>(target.x()) - u.x;
                float dy = static_cast<float>(target.y()) - u.y;
                float dist = distanceBetween(dx, dy);
                if (dist < 3.0F) { // дошёл до примерного центра комнаты
                    u.waypoints.clear();
                } else if (dist > 0.01F) {
                    u.vx = (dx / dist) * BASE_CITIZEN_SPEED;
                    u.vy = (dy / dist) * BASE_CITIZEN_SPEED;
                }

            } else if (speed < 0.5F || rng->bounded(100) < 5) {
                float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
                u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
            } else {
                u.vx = (u.vx / speed) * BASE_CITIZEN_SPEED;
                u.vy = (u.vy / speed) * BASE_CITIZEN_SPEED;
            }

            // Не даём юниту случайно выйти в дверной проём (walkGrid == 2) во время прогулки по комнате
            float lookAheadX = u.x + (u.vx / BASE_CITIZEN_SPEED) * 2.0F;
            float lookAheadY = u.y + (u.vy / BASE_CITIZEN_SPEED) * 2.0F;
            int lx = std::clamp(static_cast<int>(lookAheadX), 0, mapW - 1);
            int ly = std::clamp(static_cast<int>(lookAheadY), 0, mapH - 1);
            // Разворачиваемся к центру текущего здания если движется в дверь
            if (walkGrid[ly * mapW + lx] != 10 + u.insideBuildingId) {
                const auto &b = buildings[u.insideBuildingId];
                float toCenterAngle = std::atan2(b.cy - u.y, b.cx - u.x)
                                      + static_cast<float>((rng->generateDouble() - 0.5) * 1.2);
                u.vx = std::cos(toCenterAngle) * BASE_CITIZEN_SPEED;
                u.vy = std::sin(toCenterAngle) * BASE_CITIZEN_SPEED;
            }
        }

        // ДВИЖЕНИЕ НА ПРОГУЛКЕ
        if (u.state == UnitState::Walking && u.panicTimerSec <= 0.0F) {
            // Идём по текущему маршруту прогулки
            if (!u.waypoints.empty()) {
                QPointF target = u.waypoints.front();
                float dx = static_cast<float>(target.x()) - u.x;
                float dy = static_cast<float>(target.y()) - u.y;
                float dist = distanceBetween(dx, dy);

                int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
                int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
                bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
                float reachR = (onMainRoad && u.waypoints.size() > 1) ? (roadWidth() * 0.35F)
                                                                      : 1.5F;

                if (dist < reachR) {
                    u.waypoints.erase(u.waypoints.begin());
                } else {
                    u.vx = (dx / dist) * BASE_CITIZEN_SPEED;
                    u.vy = (dy / dist) * BASE_CITIZEN_SPEED;
                }
            } else if (u.stateTimerSec <= 0.0F || distanceBetween(u.vx, u.vy) < 1.0F) {
                // Строим новый маршрут только по истечении таймера свободной прогулки
                const auto &nodes = g_generator.nodes();
                if (!nodes.empty()) {
                    int randNode = rng->bounded(static_cast<int>(nodes.size()));
                    for (int attempt = 0; attempt < 10 && nodes[randNode].isAlleyNode; ++attempt) {
                        randNode = rng->bounded(static_cast<int>(nodes.size()));
                    }

                    u.waypoints = buildPathBetween(QPointF(u.x, u.y), nodes[randNode].pos);
                    if (u.waypoints.empty()) {
                        // Замурован: плавно идём в свободном направлении 2.5 секунды без дёрганий
                        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                        for (int attempt = 0; attempt < 12; ++attempt) {
                            float testAngle = static_cast<float>(rng->generateDouble() * 6.28318);
                            if (isWalkable(u.x + std::cos(testAngle) * 10.0F,
                                           u.y + std::sin(testAngle) * 10.0F)) {
                                angle = testAngle;
                                break;
                            }
                        }
                        u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
                        u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
                        u.stateTimerSec = 2.5F;
                    }
                }
            }
        }
    }

    // --- ДЕЙСТВИЯ ПО ИСТЕЧЕНИЮ ТАЙМЕРА СОСТОЯНИЙ stateTimerSec ---
    if (u.stateTimerSec <= 0.0F) { // Заканчивает свои дела

        // СМЕНА РАСПОРЯДКА (ПОРА НА РАБОТУ / ПОРА ДОМОЙ)
        if (((u.state == UnitState::Working || u.state == UnitState::GoingToWork) && !shouldBeAtWork)
            || ((u.state == UnitState::AtHome || u.state == UnitState::GoingHome) && shouldBeAtWork)
            || u.state == UnitState::Walking) {
            int bId = findNearestAvailableBuilding(u.x, u.y, shouldBeAtWork);
            if (bId != -1) {
                auto path = buildPathBetween(u.x, u.y, buildings[bId].cx, buildings[bId].cy);
                if (!path.empty()) {
                    buildings[bId].reservedOccupants++; // Бронь места!
                    u.targetBuildingId = bId;
                    u.state = shouldBeAtWork ? UnitState::GoingToWork : UnitState::GoingHome;
                    u.waypoints = std::move(path);
                    return;
                }
            }

            // Не нашёл доступного здания (или оно за стеной) — гуляет внутри своего сектора
            u.targetBuildingId = -1;
            u.waypoints.clear();
            u.state = UnitState::Walking;
            u.stateTimerSec = 5.0F + rng->bounded(21);
            return;
        }

        // НУЖНО ВЫЙТИ НА ПРОГУЛКУ
        if (isInsideBuilding) {
            bool isHomeBuilding = buildings[u.insideBuildingId].type == BuildingType::Residential;
            if ((u.state == UnitState::Working && !isHomeBuilding)
                || (u.state == UnitState::AtHome && isHomeBuilding)) {
                // Если он находится в нужном здании:

                u.waypoints.clear();

                // В следующий раз он выйдет на прогулку
                u.state = UnitState::Walking;
                u.stateTimerSec = static_cast<float>(4 + rng->bounded(9));
                return;
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
        u.targetBuildingId = -1;
        u.waypoints.clear();

        // Сбрасываем старый след и маршрут у зомби, преследовавших этого жителя
        for (auto &other : g_units) {
            if (other.type == UnitType::Zombie && other.hasLastKnownTarget) {
                float d = static_cast<float>(distanceBetween(other.lastKnownTargetPos.x() - u.x,
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
//             float speed = distanceBetween(u.vx, u.vy);
//             if (speed < BASE_CITIZEN_SPEED)
//                 speed = RUN_SPEED;
//             float steerX = awayX * 1.4F + tangentX * 0.6F;
//             float steerY = awayY * 1.4F + tangentY * 0.6F;
//             float steerLen = distanceBetween(steerX, steerY);
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
        float targetSpeed = distanceBetween(u.vx, u.vy);
        u.vx += pushX * dt;
        u.vy += pushY * dt;
        float newSpeed = distanceBetween(u.vx, u.vy);
        if (newSpeed > 0.01F && targetSpeed > 0.01F) {
            u.vx = (u.vx / newSpeed) * targetSpeed;
            u.vy = (u.vy / newSpeed) * targetSpeed;
        }
    }
}
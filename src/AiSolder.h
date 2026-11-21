#pragma once
#include <QRandomGenerator>
#include "GameWorld.h"
#include <cmath>

bool GameWorld::updateSoldierEvacuation(Unit &u,
                                        float dt,
                                        const std::vector<BuildingObject> &buildings,
                                        const std::vector<size_t> &activeZombies)
{
    // Оркестратор отступления и эвакуации десантника:
    // 1. Уменьшать таймер службы (90с) и перевести в режим отступления по его истечении
    // 2. Провести солдата по мосту, если он уже достиг эвакуационного выхода
    // 3. Стрелять по зомби и инфицированным, попавшим в радиус огня по пути
    // 4. Выбрать безопасный эвакуационный выход (смена точки, если путь перекрыт зомби) и построить маршрут
    // 5. Двигаться по маршруту с обходом видимых зомби и перейти на мост при достижении выхода

    // 1. Отсчёт времени службы
    if (!u.isRetreating) {
        u.lifeTimerSec -= dt;
        if (u.lifeTimerSec > 0.0F)
            return false;

        u.isRetreating = true;
        u.targetBuildingId = -1;
        u.waypoints.clear();
    }

    // 2. Движение по мосту (одиночный уход)
    if (u.onEvacBridge)
        return moveSoldierOnEvacBridge(u, dt);

    // 3. Стрельба по врагам в радиусе огня на ходу
    int fireTargetIdx = findSoldierFireTarget(u);
    if (fireTargetIdx != -1) {
        u.targetUnitId = g_units[fireTargetIdx].id;
        fireAtTarget(u, static_cast<size_t>(fireTargetIdx));
    } else {
        u.targetUnitId = -1;
    }

    // 4. Выбор безопасного выхода и построение / смена маршрута
    const auto &nodes = g_generator.nodes();
    int bestEvacNode = findSafestEvacExit(u, activeZombies);
    if (bestEvacNode == -1) {
        u.hp = 0;
        return true;
    }

    QPointF evacPos = nodes[bestEvacNode].pos;
    if (u.waypoints.empty() || u.targetBuildingId != bestEvacNode) {
        u.targetBuildingId = bestEvacNode;
        u.waypoints = buildPathBetween(QPointF(u.x, u.y), evacPos);

        if (u.waypoints.size() >= 2) {
            float d01 = static_cast<float>(distanceBetween(u.waypoints[1].x() - u.waypoints[0].x(),
                                                           u.waypoints[1].y() - u.waypoints[0].y()));
            float d1 = static_cast<float>(
                distanceBetween(u.waypoints[1].x() - u.x, u.waypoints[1].y() - u.y));
            if (d1 < d01
                && hasWalkableLineOfSight(u.x,
                                          u.y,
                                          static_cast<float>(u.waypoints[1].x()),
                                          static_cast<float>(u.waypoints[1].y()))) {
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

    // 5. Проверка достижения выхода -> переход на мост, иначе движение с уклонением от зомби
    float distToExit = static_cast<float>(distanceBetween(evacPos.x() - u.x, evacPos.y() - u.y));
    if (distToExit <= 5.0F) {
        const auto &routes = g_generator.evacRoutes();
        if (!routes.empty()) {
            const EvacRoute *bestRoute = &routes[0];
            float bestRouteDist = 1e9F;
            for (const auto &r : routes) {
                float d = static_cast<float>(
                    distanceBetween(r.startPos.x() - evacPos.x(), r.startPos.y() - evacPos.y()));
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

    followSoldierWaypoints(u);
    applySoldierEvacAvoidance(u, activeZombies);
    return true;
}

void GameWorld::updateSoldierBehavior(Unit &u,
                                      float dt,
                                      std::vector<BuildingObject> &buildings,
                                      const std::vector<size_t> &activeZombies)
{
    // Оркестратор поведения десантника:
    // 1. Проверить таймер службы (90с) и обработать одиночную эвакуацию по мосту
    // 2. Найти ближайшую цель в радиусе огня (зомби или инфицированного) и выстрелить
    // 3. Выдержать оптимальную боевую дистанцию при наличии видимой цели
    // 4. Иначе — идти по дорожным узлам (или по тропинке в здание) к ближайшему зомби/инфицированному
    // 5. Если целей на карте нет — двигаться по текущим waypoints или патрулировать

    // запрет на частую смену маршрута
    if (u.repathCooldownSec > 0.0F)
        u.repathCooldownSec = std::max(0.0F, u.repathCooldownSec - dt);

    // 1. Проверка 90 секунд службы и эвакуация
    if (updateSoldierEvacuation(u, dt, buildings, activeZombies))
        return;

    // 2. Поиск цели в радиусе выстрела (35м, сквозь тротуары, но не через стены зданий)
    int fireTargetIdx = findSoldierFireTarget(u);
    if (fireTargetIdx != -1) {
        const Unit &target = g_units[fireTargetIdx];
        float dist = distanceBetween(target.x - u.x, target.y - u.y);

        fireAtTarget(u, static_cast<size_t>(fireTargetIdx));

        // 3. Корректировка боевой дистанции (если в одном здании или путь по земле свободен)
        bool sameBuilding = (u.insideBuildingId != -1
                             && u.insideBuildingId == target.insideBuildingId);
        if (sameBuilding || hasWalkableLineOfSight(u.x, u.y, target.x, target.y)) {
            u.waypoints.clear();
            adjustSoldierCombatPosition(u, target.x, target.y, dist);
            return;
        }
    }

    // 4. Поиск глобальной цели для преследования (сначала зомби, если их нет — инфицированные)
    int huntTargetIdx = findSoldierHuntTarget(u);
    if (huntTargetIdx != -1) {
        const Unit &huntTarget = g_units[huntTargetIdx];
        routeSoldierToTarget(u, huntTarget);
        if (followSoldierWaypoints(u))
            return;
    }

    // 5. Движение по оставшемуся маршруту или патруль
    if (followSoldierWaypoints(u))
        return;

    patrolSoldier(u, buildings);
}

bool GameWorld::moveSoldierOnEvacBridge(Unit &u, float dt)
{
    // Задачи:
    // 1. Вести солдата по прямой от начала моста до конечной точки за картой
    // 2. Удалить юнит при достижении конца моста

    float dx = static_cast<float>(u.evacBridgeEnd.x()) - u.x;
    float dy = static_cast<float>(u.evacBridgeEnd.y()) - u.y;
    float dist = distanceBetween(dx, dy);

    // 1–2. Проверка конца пути и перемещение
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

int GameWorld::findSafestEvacExit(const Unit &u, const std::vector<size_t> &activeZombies) const
{
    // Задачи:
    // 1. Оценить каждый эвакуационный выход по дистанции и наличию зомби в опасном коридоре к нему
    // 2. Вернуть ID наиболее безопасного и близкого выхода (с гистерезисом для текущей точки)

    const auto &nodes = g_generator.nodes();
    int bestNode = -1;
    float bestScore = 1e9F;

    for (const auto &node : nodes) {
        if (!node.isEvacExit)
            continue;

        float ex = static_cast<float>(node.pos.x());
        float ey = static_cast<float>(node.pos.y());
        float distToExit = distanceBetween(ex - u.x, ey - u.y);
        float threatPenalty = 0.0F;

        // 1. Штрафуем выходы, у которых зомби стоят рядом с выходом или на пути к нему
        for (size_t zIdx : activeZombies) {
            const Unit &z = g_units[zIdx];
            if (z.hp <= 0)
                continue;

            float dZombieExit = distanceBetween(z.x - ex, z.y - ey);
            if (dZombieExit < 45.0F) {
                threatPenalty += (45.0F - dZombieExit) * 6.0F;
            }

            // Проверяем, не находится ли видимый зомби прямо в секторе движения к этому выходу
            float dZombieSoldier = distanceBetween(z.x - u.x, z.y - u.y);
            if (dZombieSoldier < 55.0F && distToExit > 1.0F && dZombieSoldier > 0.1F
                && hasLineOfSight(u.x, u.y, z.x, z.y)) {
                float dot = ((ex - u.x) / distToExit) * ((z.x - u.x) / dZombieSoldier)
                            + ((ey - u.y) / distToExit) * ((z.y - u.y) / dZombieSoldier);
                if (dot > 0.5F) {
                    threatPenalty += dot * (55.0F - dZombieSoldier) * 4.5F;
                }
            }
        }

        // 2. Небольшой бонус текущему выбранному выходу, чтобы солдат не метался при равных условиях
        float hysteresis = (u.targetBuildingId == node.id) ? -25.0F : 0.0F;
        float score = distToExit + threatPenalty + hysteresis;

        if (score < bestScore) {
            bestScore = score;
            bestNode = node.id;
        }
    }

    return bestNode;
}

void GameWorld::applySoldierEvacAvoidance(Unit &u, const std::vector<size_t> &activeZombies)
{
    // Задачи:
    // 1. Вычислить вектор отталкивания и бокового обхода от видимых зомби на улице
    // 2. Скорректировать скорость солдата, сохраняя приоритет движения к эвакуации

    if (u.insideBuildingId != -1)
        return;

    float speed = distanceBetween(u.vx, u.vy);
    if (speed < 0.1F)
        return;

    float dirX = u.vx / speed;
    float dirY = u.vy / speed;
    float avoidX = 0.0F;
    float avoidY = 0.0F;
    bool hasThreat = false;

    // 1. Суммируем отталкивание и боковой обход по касательной от близких зомби
    for (size_t zIdx : activeZombies) {
        const Unit &z = g_units[zIdx];
        if (z.hp <= 0 || z.insideBuildingId != -1)
            continue;

        float dx = u.x - z.x;
        float dy = u.y - z.y;
        float dist = distanceBetween(dx, dy);

        if (dist <= 38.0F && dist > 0.1F && hasLineOfSight(u.x, u.y, z.x, z.y)) {
            hasThreat = true;
            float weight = (38.0F - dist) / 38.0F;
            float awayX = dx / dist;
            float awayY = dy / dist;

            // Выбираем сторону обхода (влево или вправо от зомби), ближе к текущему курсу
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

    // 2. Смешиваем курс на эвакуацию с вектором обхода
    if (hasThreat) {
        float finalX = dirX * 1.15F + avoidX;
        float finalY = dirY * 1.15F + avoidY;
        float len = distanceBetween(finalX, finalY);
        if (len > 0.01F) {
            u.vx = (finalX / len) * ZOMBIE_CHASE_SPEED;
            u.vy = (finalY / len) * ZOMBIE_CHASE_SPEED;
        }
    }
}

int GameWorld::findSoldierFireTarget(const Unit &u) const
{
    // Задачи:
    // 1. Найти ближайшего видимого зомби в радиусе выстрела (35м)
    // 2. Проверить видимых инфицированных жителей в радиусе выстрела (35м)
    // Стрельба разрешена только если оба в одном помещении или оба на улице с прямой видимостью

    const float shootRadiusSq = 35.0F * 35.0F;

    auto canShootTarget = [&](const Unit &target) -> bool {
        if (u.insideBuildingId != target.insideBuildingId)
            return false; // Не стреляем сквозь стены между улицей и зданием
        if (u.insideBuildingId != -1)
            return true; // Внутри одной комнаты стенок нет
        return hasLineOfSight(u.x, u.y, target.x, target.y);
    };

    int bestZombie = -1;
    float bestZombieDistSq = shootRadiusSq;

    int sCellX = std::clamp(static_cast<int>(u.x) / UNIT_GRID_CELL, 0, g_unitGridCols - 1);
    int sCellY = std::clamp(static_cast<int>(u.y) / UNIT_GRID_CELL, 0, g_unitGridRows - 1);

    for (int dy = -1; dy <= 1; ++dy) {
        int ny = sCellY + dy;
        if (ny < 0 || ny >= g_unitGridRows)
            continue;
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = sCellX + dx;
            if (nx < 0 || nx >= g_unitGridCols)
                continue;

            for (size_t zIdx : g_zombieSpatialGrid[ny * g_unitGridCols + nx]) {
                const Unit &z = g_units[zIdx];
                if (z.hp <= 0)
                    continue;

                float ddx = z.x - u.x;
                float ddy = z.y - u.y;
                float dSq = ddx * ddx + ddy * ddy;
                if (dSq >= bestZombieDistSq)
                    continue;

                if (u.insideBuildingId != -1) {
                    bestZombieDistSq = dSq;
                    bestZombie = static_cast<int>(zIdx);
                }
            }
        }
    }

    if (bestZombie != -1)
        return bestZombie;

    // 2. Инфицированные жители в радиусе выстрела (35м)
    int bestInfected = -1;
    float bestInfectedDistSq = shootRadiusSq;

    for (size_t i = 0; i < g_units.size(); ++i) {
        const Unit &cand = g_units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;
        if (!canShootTarget(cand))
            continue;

        float dx = cand.x - u.x;
        float dy = cand.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestInfectedDistSq) {
            bestInfectedDistSq = dSq;
            bestInfected = static_cast<int>(i);
        }
    }

    return bestInfected;
}

void GameWorld::adjustSoldierCombatPosition(Unit &u, float targetX, float targetY, float dist)
{
    // Задачи:
    // 1. Подойти ближе, если цель дальше оптимальной дальности огня
    // 2. Отойти назад, если враг подошёл вплотную (внутри здания дистанция меньше, чтобы не выходить за дверь)

    float maxRange = (u.insideBuildingId != -1) ? 6.0F : 20.0F;
    float minRange = (u.insideBuildingId != -1) ? 2.5F : 15.0F;
    float dx = targetX - u.x;
    float dy = targetY - u.y;

    if (dist > maxRange && dist > 0.1F) {
        u.vx = (dx / dist) * ZOMBIE_CHASE_SPEED;
        u.vy = (dy / dist) * ZOMBIE_CHASE_SPEED;
    } else if (dist < minRange && dist > 0.1F) {
        u.vx = -(dx / dist) * ZOMBIE_CHASE_SPEED;
        u.vy = -(dy / dist) * ZOMBIE_CHASE_SPEED;
    } else {
        u.vx = 0.0F;
        u.vy = 0.0F;
    }
}

void GameWorld::routeSoldierToTarget(Unit &u, const Unit &target)
{
    // Задачи:
    // 1. Не перестраивать путь, если цель сместилась меньше чем на 20м от конца текущего маршрута
    // 2. Построить путь через buildPathBetween (учитывает двери и тропинки зданий)
    // 3. Отбросить первый узел на улице, если солдат уже продвинулся дальше него

    // 1. Проверяем смещение цели относительно конечной точки текущего маршрута
    if (!u.waypoints.empty()) {
        QPointF lastPt = u.waypoints.back();
        float targetShift = distanceBetween(static_cast<float>(lastPt.x()) - target.x,
                                            static_cast<float>(lastPt.y()) - target.y);
        if (targetShift <= 20.0F || u.repathCooldownSec > 0.0F) {
            if (target.insideBuildingId == -1) {
                u.waypoints.back() = QPointF(target.x, target.y);
            }
            return;
        }
    }

    // 2. Строим путь
    u.repathCooldownSec = 0.35F; // Запрещаем повторный вызов Дейкстры в течение 0.35с
    std::vector<QPointF> newPath = buildPathBetween(u.x, u.y, target.x, target.y);

    // 3. Если солдат на улице — отбрасываем первый узел, если он позади солдата
    if (u.insideBuildingId == -1 && newPath.size() >= 2) {
        float segLen = static_cast<float>(
            distanceBetween(newPath[1].x() - newPath[0].x(), newPath[1].y() - newPath[0].y()));
        float distToSecond = static_cast<float>(
            distanceBetween(newPath[1].x() - u.x, newPath[1].y() - u.y));
        if (distToSecond < segLen
            && hasWalkableLineOfSight(u.x,
                                      u.y,
                                      static_cast<float>(newPath[1].x()),
                                      static_cast<float>(newPath[1].y()))) {
            newPath.erase(newPath.begin());
        }
    }

    u.waypoints = std::move(newPath);
}

void GameWorld::fireAtTarget(Unit &soldier, size_t targetIdx)
{
    // Задачи:
    // 1. Проверить дистанцию выстрела (35м) и перезарядку (1.0с)
    // 2. Создать трассер пули, нанести урон и начислить награду при уничтожении

    Unit &target = g_units[targetIdx];
    float dist = distanceBetween(target.x - soldier.x, target.y - soldier.y);

    // 1. Проверка радиуса и кулдауна
    if (dist > 35.0F || soldier.attackCooldownSec > 0.0F)
        return;

    // 2. Выстрел и урон
    soldier.attackCooldownSec = 1.0F;
    spawnBulletTracer(soldier.x, soldier.y, target.x, target.y);
    target.hp -= 1;

    if (target.hp <= 0) {
        spawnBurstParticles(target.x, target.y, 8, 255, 60, 60);
    }
}

int GameWorld::findSoldierHuntTarget(const Unit &u) const
{
    // Задачи:
    // 1. Найти ближайшего живого зомби на карте
    // 2. Если зомби уничтожены — найти ближайшего инфицированного жителя

    int bestIdx = -1;
    float bestDistSq = 300.0F * 300.0F; // Ищем только в радиусе 300м

    int sCellX = std::clamp(static_cast<int>(u.x) / UNIT_GRID_CELL, 0, g_unitGridCols - 1);
    int sCellY = std::clamp(static_cast<int>(u.y) / UNIT_GRID_CELL, 0, g_unitGridRows - 1);

    // 1. Быстрый поиск ближайшего зомби в сетке 5x5 ячеек (~400м)
    for (int dy = -2; dy <= 2; ++dy) {
        int ny = sCellY + dy;
        if (ny < 0 || ny >= g_unitGridRows)
            continue;
        for (int dx = -2; dx <= 2; ++dx) {
            int nx = sCellX + dx;
            if (nx < 0 || nx >= g_unitGridCols)
                continue;

            for (size_t zIdx : g_zombieSpatialGrid[ny * g_unitGridCols + nx]) {
                const Unit &z = g_units[zIdx];
                if (z.hp <= 0)
                    continue;

                float ddx = z.x - u.x;
                float ddy = z.y - u.y;
                float dSq = ddx * ddx + ddy * ddy;
                if (dSq < bestDistSq) {
                    bestDistSq = dSq;
                    bestIdx = static_cast<int>(zIdx);
                }
            }
        }
    }

    if (bestIdx != -1)
        return bestIdx;

    // 2. Если зомби рядом нет — охота на инфицированных жителей
    float bestInfDistSq = 1e18F;
    for (size_t i = 0; i < g_units.size(); ++i) {
        const Unit &cand = g_units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;
        float dx = cand.x - u.x;
        float dy = cand.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestInfDistSq) {
            bestInfDistSq = dSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

bool GameWorld::followSoldierWaypoints(Unit &u)
{
    // Задачи:
    // 1. Определить радиус достижения узла (увеличен на дороге, чтобы солдат не крутился вокруг точки)
    // 2. Двигаться к следующей точке в waypoints и удалять пройденные
    if (u.waypoints.empty())
        return false;
    const auto &walkGrid = g_generator.walkGrid();
    int mapW = g_generator.width();
    int mapH = g_generator.height();
    int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
    int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);

    // 1. На широкой дороге перекрёсток считается пройденным в радиусе 0.35 * roadWidth (как у жителей)
    bool onMainRoad = (walkGrid[gy * mapW + gx] == 1);
    float reachRadius = (onMainRoad && u.waypoints.size() > 1) ? (roadWidth() * 0.35F) : 2.5F;
    QPointF target = u.waypoints.front();
    float dx = static_cast<float>(target.x()) - u.x;
    float dy = static_cast<float>(target.y()) - u.y;
    float dist = distanceBetween(dx, dy);
    // 2. Переход к следующей точке или движение
    if (dist <= reachRadius) {
        u.waypoints.erase(u.waypoints.begin());
        if (!u.waypoints.empty()) {
            target = u.waypoints.front();
            dx = static_cast<float>(target.x()) - u.x;
            dy = static_cast<float>(target.y()) - u.y;
            dist = distanceBetween(dx, dy);
        }
    }
    if (dist > 0.1F) {
        u.vx = (dx / dist) * ZOMBIE_CHASE_SPEED;
        u.vy = (dy / dist) * ZOMBIE_CHASE_SPEED;
    }
    return true;
}
void GameWorld::patrolSoldier(Unit &u, const std::vector<BuildingObject> &buildings)
{
    // Задачи:
    // 1. Выйти из пустого здания, если солдат внутри
    // 2. Иначе — выбрать случайный узел дороги для патрулирования
    // 1. Выход из здания
    if (u.insideBuildingId != -1 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        const auto &b = buildings[u.insideBuildingId];
        for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
            u.waypoints.push_back(b.pathFromRoad[p]);
        return;
    }
    // 2. Патруль к случайному перекрёстку
    const auto &nodes = g_generator.nodes();
    if (!nodes.empty()) {
        auto *rng = QRandomGenerator::global();
        int randNode = rng->bounded(static_cast<int>(nodes.size()));
        for (int attempt = 0; attempt < 10 && nodes[randNode].isAlleyNode; ++attempt) {
            randNode = rng->bounded(static_cast<int>(nodes.size()));
        }

        u.waypoints = buildPathBetween(QPointF(u.x, u.y), nodes[randNode].pos);
    }
}
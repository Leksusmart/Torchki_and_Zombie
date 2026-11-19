#pragma once
#include <QRandomGenerator>
#include "GameWorld.h"
#include <cmath>

void GameWorld::updateSoldierBehavior(Unit &u,
                                      float dt,
                                      std::vector<BuildingObject> &buildings,
                                      const std::vector<size_t> &activeZombies)
{
    // Оркестратор поведения десантника:
    // 1. Проверить таймер службы (90с) и обработать одиночную эвакуацию по мосту
    // 2. Найти ближайшую цель в радиусе огня (зомби или инфицированного) и выстрелить
    // 3. Выдержать оптимальную боевую дистанцию при наличии видимой цели
    // 4. Иначе — идти по дорожным узлам (nodes) к ближайшему зомби (или инфицированному, если зомби нет)
    // 5. Если целей на карте нет — двигаться по текущим waypoints или патрулировать

    // 1. Проверка 90 секунд службы и эвакуация
    if (updateSoldierEvacuation(u, dt, buildings))
        return;

    // 2. Поиск цели в зоне прямой видимости для стрельбы (зомби или инфицированные)
    int fireTargetIdx = findSoldierFireTarget(u, activeZombies);
    if (fireTargetIdx != -1) {
        u.waypoints.clear();
        const Unit &target = g_units[fireTargetIdx];
        float dx = target.x - u.x;
        float dy = target.y - u.y;
        float dist = std::hypot(dx, dy);

        fireAtTarget(u, static_cast<size_t>(fireTargetIdx));

        // 3. Корректировка боевой дистанции
        adjustSoldierCombatPosition(u, target.x, target.y, dist);
        return;
    }

    // 4. Поиск глобальной цели для преследования (сначала зомби, если их нет — инфицированные)
    int huntTargetIdx = findSoldierHuntTarget(u, activeZombies);
    if (huntTargetIdx != -1) {
        const Unit &huntTarget = g_units[huntTargetIdx];
        routeSoldierToTarget(u, huntTarget.x, huntTarget.y, buildings);
        if (followSoldierWaypoints(u))
            return;
    }

    // 5. Движение по оставшемуся маршруту или патруль
    if (followSoldierWaypoints(u))
        return;

    patrolSoldier(u, buildings);
}

bool GameWorld::updateSoldierEvacuation(Unit &u,
                                        float dt,
                                        const std::vector<BuildingObject> &buildings)
{
    // Задачи:
    // 1. Уменьшать таймер службы (90с) и перевести в режим отступления по его истечении
    // 2. Проложить маршрут к ближайшему эвакуационному выходу
    // 3. После достижения выхода — провести солдата по мосту в одиночку (без ожидания группы)

    // 1. Отсчёт времени службы
    if (!u.isRetreating) {
        u.lifeTimerSec -= dt;
        if (u.lifeTimerSec > 0.0F)
            return false;

        u.isRetreating = true;
        u.waypoints.clear();
    }

    // 2. Движение по мосту после выхода за пределы города (одиночный конвой)
    if (u.onEvacBridge) {
        float dx = static_cast<float>(u.evacBridgeEnd.x()) - u.x;
        float dy = static_cast<float>(u.evacBridgeEnd.y()) - u.y;
        float dist = std::hypot(dx, dy);

        if (dist < 4.0F) {
            u.hp = 0; // Солдат успешно эвакуировался
        } else {
            const float bridgeSpeed = 32.0F;
            u.vx = (dx / dist) * bridgeSpeed;
            u.vy = (dy / dist) * bridgeSpeed;
            u.x += u.vx * dt;
            u.y += u.vy * dt;
        }
        return true;
    }

    // 3. Поиск ближайшего эвакуационного выхода и построение пути
    const auto &nodes = g_generator.nodes();
    int bestEvacNode = -1;
    float bestDist = 1e9F;

    for (const auto &node : nodes) {
        if (!node.isEvacExit)
            continue;
        float d = static_cast<float>(std::hypot(node.pos.x() - u.x, node.pos.y() - u.y));
        if (d < bestDist) {
            bestDist = d;
            bestEvacNode = node.id;
        }
    }

    if (bestEvacNode == -1) {
        u.hp = 0;
        return true;
    }

    QPointF evacPos = nodes[bestEvacNode].pos;

    if (u.waypoints.empty()) {
        u.waypoints = buildPathBetween(QPointF(u.x, u.y), evacPos);
        if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
            const auto &b = buildings[u.insideBuildingId];
            std::vector<QPointF> exitPts;
            for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                exitPts.push_back(b.pathFromRoad[p]);
            u.waypoints.insert(u.waypoints.begin(), exitPts.begin(), exitPts.end());
        }
    }

    // Проверка достижения эвакуационного выхода -> переход на мост
    float distToExit = static_cast<float>(std::hypot(evacPos.x() - u.x, evacPos.y() - u.y));
    if (distToExit <= 5.0F) {
        const auto &routes = g_generator.evacRoutes();
        if (!routes.empty()) {
            const EvacRoute *bestRoute = &routes[0];
            float bestRouteDist = 1e9F;
            for (const auto &r : routes) {
                float d = static_cast<float>(
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

    followSoldierWaypoints(u);
    return true;
}

int GameWorld::findSoldierFireTarget(const Unit &u, const std::vector<size_t> &activeZombies) const
{
    // Задачи:
    // 1. Найти ближайшего видимого зомби в радиусе обзора/стрельбы (приоритет 1)
    // 2. Проверить видимых инфицированных жителей в радиусе выстрела (приоритет 2)

    const float visionRadiusSq = 65.0F * 65.0F;
    const float shootRadiusSq = 35.0F * 35.0F;

    int bestZ = -1;
    float bestZDistSq = visionRadiusSq;

    // 1. Видимые зомби
    for (size_t zIdx : activeZombies) {
        const Unit &z = g_units[zIdx];
        if (z.hp <= 0)
            continue;

        bool sameBuilding = (u.insideBuildingId != -1 && u.insideBuildingId == z.insideBuildingId);
        bool canSee = sameBuilding
                      || (u.insideBuildingId == -1 && z.insideBuildingId == -1
                          && hasLineOfSight(u.x, u.y, z.x, z.y));
        if (!canSee)
            continue;

        float dx = z.x - u.x;
        float dy = z.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestZDistSq) {
            bestZDistSq = dSq;
            bestZ = static_cast<int>(zIdx);
        }
    }

    if (bestZ != -1)
        return bestZ;

    // 2. Инфицированные жители, попавшие в радиус выстрела (35м)
    int bestInf = -1;
    float bestInfDistSq = shootRadiusSq;

    for (size_t i = 0; i < g_units.size(); ++i) {
        const Unit &cand = g_units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;

        bool sameBuilding = (u.insideBuildingId != -1
                             && u.insideBuildingId == cand.insideBuildingId);
        bool canSee = sameBuilding
                      || (u.insideBuildingId == -1 && cand.insideBuildingId == -1
                          && hasLineOfSight(u.x, u.y, cand.x, cand.y));
        if (!canSee)
            continue;

        float dx = cand.x - u.x;
        float dy = cand.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestInfDistSq) {
            bestInfDistSq = dSq;
            bestInf = static_cast<int>(i);
        }
    }

    return bestInf;
}

void GameWorld::fireAtTarget(Unit &soldier, size_t targetIdx)
{
    // Задачи:
    // 1. Проверить дистанцию выстрела (35м) и перезарядку (1.0с)
    // 2. Создать трассер пули, нанести урон и начислить награду при уничтожении

    Unit &target = g_units[targetIdx];
    float dist = std::hypot(target.x - soldier.x, target.y - soldier.y);

    // 1. Проверка радиуса и кулдауна
    if (dist > 35.0F || soldier.attackCooldownSec > 0.0F)
        return;

    // 2. Выстрел и урон
    soldier.attackCooldownSec = 1.0F;
    spawnBulletTracer(soldier.x, soldier.y, target.x, target.y);
    target.hp -= 1;

    if (target.hp <= 0) {
        spawnBurstParticles(target.x, target.y, 8, 255, 60, 60);
        if (target.type == UnitType::Zombie) {
            g_eliminatedCount++;
            g_budget += 5;
        }
    }
}

void GameWorld::adjustSoldierCombatPosition(Unit &u, float targetX, float targetY, float dist)
{
    // Задачи:
    // 1. Подойти ближе, если цель дальше оптимальной дальности огня
    // 2. Отойти назад, если враг подошёл вплотную

    float maxRange = (u.insideBuildingId != -1) ? 5.0F : 14.0F;
    float minRange = (u.insideBuildingId != -1) ? 2.5F : 9.0F;
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

int GameWorld::findSoldierHuntTarget(const Unit &u, const std::vector<size_t> &activeZombies) const
{
    // Задачи:
    // 1. Найти ближайшего живого зомби на карте
    // 2. Если зомби уничтожены — найти ближайшего инфицированного жителя

    int bestIdx = -1;
    float bestDistSq = 1e18F;

    // 1. Приоритет: живые зомби
    for (size_t zIdx : activeZombies) {
        const Unit &z = g_units[zIdx];
        if (z.hp <= 0)
            continue;
        float dx = z.x - u.x;
        float dy = z.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestIdx = static_cast<int>(zIdx);
        }
    }

    if (bestIdx != -1)
        return bestIdx;

    // 2. Если зомби нет — охота на инфицированных жителей
    for (size_t i = 0; i < g_units.size(); ++i) {
        const Unit &cand = g_units[i];
        if (cand.hp <= 0 || cand.type != UnitType::Citizen || !cand.isInfected)
            continue;
        float dx = cand.x - u.x;
        float dy = cand.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}
void GameWorld::routeSoldierToTarget(Unit &u,
                                     float targetX,
                                     float targetY,
                                     const std::vector<BuildingObject> &buildings)
{
    // Задачи:
    // 1. Проверить, нужно ли перестроить маршрут по дорожным узлам (nodes) к цели
    // 2. Построить путь через buildPathBetween и вывести солдата из здания, если он внутри
    bool needRebuild = u.waypoints.empty();
    if (!needRebuild) {
        QPointF lastPt = u.waypoints.back();
        float shift = static_cast<float>(std::hypot(lastPt.x() - targetX, lastPt.y() - targetY));
        if (shift > 18.0F) {
            needRebuild = true;
        }
    }
    if (!needRebuild)
        return;
    // 1. Построение пути по узлам дорожного графа
    u.waypoints = buildPathBetween(QPointF(u.x, u.y), QPointF(targetX, targetY));
    // 2. Если солдат внутри здания — добавляем выход по тропинке в начало маршрута
    if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        const auto &b = buildings[u.insideBuildingId];
        std::vector<QPointF> exitPts;
        for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
            exitPts.push_back(b.pathFromRoad[p]);
        u.waypoints.insert(u.waypoints.begin(), exitPts.begin(), exitPts.end());
    }
}
bool GameWorld::followSoldierWaypoints(Unit &u)
{
    // Задачи:
    // 1. Двигаться к следующей точке в waypoints
    // 2. Удалять пройденные точки маршрута
    if (u.waypoints.empty())
        return false;
    QPointF target = u.waypoints.front();
    float dx = static_cast<float>(target.x()) - u.x;
    float dy = static_cast<float>(target.y()) - u.y;
    float dist = std::hypot(dx, dy);
    if (dist < 2.0F) {
        u.waypoints.erase(u.waypoints.begin());
    } else {
        u.vx = (dx / dist) * ZOMBIE_CHASE_SPEED;
        u.vy = (dy / dist) * ZOMBIE_CHASE_SPEED;
    }
    return true;
}
void GameWorld::patrolSoldier(Unit &u, const std::vector<BuildingObject> &buildings)
{
    // Задачи:
    // 1. Выйти из пустого здания, если солдат внутри
    // 2. Иначе — выбрать случайный ближайший узел дороги для патрулирования
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
        u.waypoints = buildPathBetween(QPointF(u.x, u.y), nodes[randNode].pos);
    }
}
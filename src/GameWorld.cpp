#include "GameWorld.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QVariant>
#include <algorithm>
#include <cmath>

#include "ai/CitizenAI.h"
#include "ai/SoldierAI.h"
#include "ai/UnitMovement.h"
#include "ai/ZombieAI.h"

GameWorld::GameWorld(QObject *parent)
    : QObject(parent)
{
    // Задачи:
    // 1. Связать зависимости между подсистемами (эффекты, навигация, карта)
    // 2. Запустить уровень с начальными вспышками и таймером симуляции 60 FPS

    helicopterSystem_.setEffectsSystem(&effectsSystem_);
    wallSystem_.setEffectsSystem(&effectsSystem_);
    evacuationSystem_.setDependencies(&g_generator, &navService_);

    LevelConfig defaultCfg;
    defaultCfg.outbreaks = {{4.0F, 1, false}, {30.0F, 1, false}};

    startLevel(defaultCfg);

    connect(&g_timer, &QTimer::timeout, this, &GameWorld::tick);
    constexpr int targetFPS = 60;
    g_timer.start(1000 / targetFPS);
}

void GameWorld::startLevel(const LevelConfig &config)
{
    // Задачи:
    // 1. Сохранить конфиг уровня и сбросить флаги вспышек
    // 2. Сбросить WorldState и все игровые подсистемы
    // 3. Сгенерировать карту, заполнить NavGrid и кэш дорожного графа
    // 4. Создать начальных жителей и пересчитать экономику
    // 5. Подготовить геометрию рендера и уведомить QML

    // 1. Конфиг
    g_currentConfig = config;
    for (auto &ob : g_currentConfig.outbreaks)
        ob.triggered = false;

    // 2. Состояние симуляции
    state_.budget = 300.0;
    state_.eliminatedCount = 0;
    state_.evacuatedTotalCount = 0;
    state_.lostCount = 0;
    state_.infectedCount = 0;
    state_.elapsedSimSec = 0.0F;
    state_.timeOfDayHours = g_currentConfig.startHour;
    state_.dayNumber = 1;
    state_.nextUnitId = 1;
    state_.paused = true;
    state_.units.clear();

    g_lastTrackedHour = static_cast<int>(state_.timeOfDayHours) % 24;
    g_selectedBuildingId = -1;
    g_selectedUnitId = -1;

    effectsSystem_.onLevelReset(state_);
    helicopterSystem_.onLevelReset(state_);
    evacuationSystem_.onLevelReset(state_);
    wallSystem_.onLevelReset(state_);

    // 3. Карта, NavGrid и граф дорог
    g_generator.generateFromLevel(g_currentLevel);
    state_.navGrid.resize(g_generator.width(), g_generator.height());
    state_.navGrid.buildBaseFromMap(g_generator.walkGrid(), g_generator.cityMask());

    navService_.buildGraphCache(g_generator, state_.navGrid);
    spatialGrid_.initBuildings(g_generator.width(), g_generator.height(), g_generator.buildings());
    prevRoofOpen.assign(g_generator.buildings().size(), 0);

    buildBuildingsGeometry();
    rebuildBurningLights();
    syncNavigationObstacles();

    // 4. Юниты и налоги
    spawnInitialUnits(g_currentConfig.initialCitizens);
    updateHourlyTaxes(g_lastTrackedHour, true);

    // 5. Рендер и сигналы для QML
    rebuildUnitRenderData();
    rebuildWorkplaceLabels();
    evacuationSystem_.rebuildEvacPoints(g_generator.nodes());
    updateSelectedBuildingInfo();

    emit helicoptersChanged();
    emit selectedBuildingChanged();
    emit evacuationsChanged();
    emit mapUpdated();
    emit unitsUpdated();
    emit statsChanged();
    emit timeChanged();
    emit stateChanged();
}

void GameWorld::syncNavigationObstacles()
{
    // Задачи:
    // 1. Обновить динамические препятствия в NavGrid (стены, блокпосты)
    // 2. Пересчитать веса рёбер дорожного графа и перестроить заблокированные маршруты юнитов

    state_.navGrid.clearDynamicObstacles();
    wallSystem_.applyToNavGrid(state_.navGrid);

    const auto &nodes = g_generator.nodes();
    const auto &edges = g_generator.edges();
    const int n = static_cast<int>(nodes.size());
    if (n == 0)
        return;

    // Обновляем веса рёбер графа с учётом активных стен
    auto &adj = const_cast<std::vector<std::vector<std::pair<int, float>>> &>(navService_.roadAdj());
    adj.assign(n, {});
    for (const auto &e : edges) {
        const QPointF p1 = nodes[e.fromId].pos;
        const QPointF p2 = nodes[e.toId].pos;
        const float dist = static_cast<float>(std::hypot(p1.x() - p2.x(), p1.y() - p2.y()));
        const float weight = wallSystem_.blocksSegment(p1, p2) ? 1e8F : dist;
        adj[e.fromId].push_back({e.toId, weight});
        adj[e.toId].push_back({e.fromId, weight});
    }

    if (wallSystem_.walls().empty())
        return;

    // Перестраиваем пути жителям и солдатам, если их маршрут пересёк новую стену
    for (auto &u : state_.units) {
        if (u.hp <= 0 || u.waypoints.empty() || u.type == UnitType::Zombie)
            continue;

        bool pathBlocked = false;
        QPointF prevPt(u.x, u.y);
        for (const auto &wp : u.waypoints) {
            if (wallSystem_.blocksSegment(prevPt, wp)) {
                pathBlocked = true;
                break;
            }
            prevPt = wp;
        }
        if (pathBlocked) {
            u.waypoints = navService_.buildPathBetween(QPointF(u.x, u.y),
                                                       u.waypoints.back(),
                                                       g_generator,
                                                       state_.navGrid);
        }
    }
}

void GameWorld::tick()
{
    // Задачи:
    // 1. Обновить игровое время, налоги и вспышки заражения
    // 2. Определить положение юнитов в зданиях, обновить таймеры зданий и инкубацию
    // 3. Обновить пространственную сетку и выполнить ИИ + физику юнитов
    // 4. Пересчитать занятость зданий и удалить погибших юнитов
    // 5. Обновить подсистемы (эффекты, вертолёты, эвакуацию, стены) и данные рендера

    if (state_.paused)
        return;

    profiler_.beginTick();

    const float dt = 0.016F * static_cast<float>(state_.speedMultiplier);
    const float dtHours = dt * g_currentConfig.gameHoursPerSec;

    // 1. Время, налоги и вспышки
    state_.elapsedSimSec += dt;
    state_.timeOfDayHours += dtHours;
    if (state_.timeOfDayHours >= 24.0F) {
        state_.timeOfDayHours -= 24.0F;
        state_.dayNumber++;
    }

    const int currentHour = static_cast<int>(state_.timeOfDayHours) % 24;
    if (currentHour != g_lastTrackedHour) {
        g_lastTrackedHour = currentHour;
        updateHourlyTaxes(currentHour, false);
    }
    emit timeChanged();

    if (state_.incomePerSec > 0.0) {
        state_.budget += state_.incomePerSec * dt;
        emit statsChanged();
    }

    processOutbreaks();

    // 2. Зоны зданий, таймеры, инкубация и пространственные сетки
    auto &buildings = g_generator.mutableBuildings();
    bool buildingsVisualDirty = false;
    bool workplaceLabelsDirty = false;
    bool statsDirty = false;

    for (auto &b : buildings)
        b.currentOccupants = 0;

    const int currentHourIdx = static_cast<int>(state_.timeOfDayHours) % 24;
    for (auto &u : state_.units) {
        const int bId = state_.navGrid.buildingIdAt(static_cast<int>(u.x), static_cast<int>(u.y));
        if (bId >= 0 && bId < static_cast<int>(buildings.size())) {
            u.insideBuildingId = bId;
            if (u.hp > 0) {
                auto &b = buildings[bId];
                b.currentOccupants++;
                if ((b.type == BuildingType::Office || b.type == BuildingType::Factory)
                    && u.type != UnitType::Zombie
                    && b.currentOccupants > b.hourlyMaxWorkers[currentHourIdx]) {
                    b.hourlyMaxWorkers[currentHourIdx] = b.currentOccupants;
                }
            }
        } else {
            u.insideBuildingId = -1;
        }
    }

    for (auto &b : buildings) {
        if (b.underAttackTimerSec > 0.0F)
            b.underAttackTimerSec = std::max(0.0F, b.underAttackTimerSec - dt);
        if (b.isBurning) {
            b.burnTimerSec -= dt;
            if (b.burnTimerSec <= 0.0F) {
                b.isBurning = false;
                b.isBurnedOut = true;
                buildingsVisualDirty = true;
                workplaceLabelsDirty = true;
            }
        }
    }

    for (auto &u : state_.units) {
        if (u.isInfected && u.type != UnitType::Zombie) {
            u.infectionHoursLeft -= dtHours;
            if (u.infectionHoursLeft <= 0.0F) {
                const int bId = u.insideBuildingId;
                if (bId >= 0 && bId < static_cast<int>(buildings.size())) {
                    const auto &b = buildings[bId];
                    for (auto &other : state_.units) {
                        if (other.insideBuildingId == bId && &other != &u) {
                            other.panicTimerSec = 3.0F;
                            other.state = UnitState::Walking;
                            other.stateTimerSec = 6.0F;
                            other.waypoints.clear();
                            for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                                other.waypoints.push_back(b.pathFromRoad[p]);
                        }
                    }
                }
                CitizenAI::infectUnit(u, state_.units, true);
            }
        }
        if (u.attackCooldownSec > 0.0F)
            u.attackCooldownSec -= dt;
    }

    spatialGrid_.rebuildUnits(state_.units);

    if (prevRoofOpen.size() != buildings.size())
        prevRoofOpen.resize(buildings.size(), 0);
    for (size_t i = 0; i < buildings.size(); ++i)
        prevRoofOpen[i] = (buildings[i].currentOccupants > 0) ? 1 : 0;

    profiler_.markZonesAndTimers();

    // 3. ИИ и физика движения юнитов
    updateAllUnits(dt, buildingsVisualDirty, workplaceLabelsDirty, statsDirty);
    profiler_.markPeopleActions();

    // 4. Занятость зданий и удаление погибших
    postUpdateUnitsAndBuildings(buildingsVisualDirty);
    if (workplaceLabelsDirty)
        rebuildWorkplaceLabels();
    if (statsDirty)
        emit statsChanged();
    profiler_.markPostUnits();

    // 5. Подсистемы
    effectsSystem_.update(dt, state_);
    helicopterSystem_.update(dt, state_);
    evacuationSystem_.update(dt, state_);
    wallSystem_.update(dt, state_);

    if (helicopterSystem_.consumeChangedFlag())
        emit helicoptersChanged();
    if (evacuationSystem_.consumeChangedFlag())
        emit evacuationsChanged();
    if (wallSystem_.consumeMapDirtyFlag()) {
        syncNavigationObstacles();
        emit mapUpdated();
    }
    if (wallSystem_.consumeOverlaysDirtyFlag())
        emit wallsChanged();
    if (wallSystem_.consumeSelectionDirtyFlag())
        emit selectedWallChanged();

    profiler_.markSystems();

    // 6. Обновление геометрии рендера и оверлеев выделения
    rebuildUnitRenderData();
    rebuildBuildingOverlays();
    if (g_selectedBuildingId != -1) {
        updateSelectedBuildingInfo();
        emit selectedBuildingChanged();
    }
    if (g_selectedUnitId != -1) {
        updateSelectedUnitInfo(false);
        emit selectedUnitChanged();
    }
    emit unitsUpdated();

    profiler_.endTick(state_.units.size(), state_.speedMultiplier);
}

void GameWorld::updateAllUnits(float dt,
                               bool &buildingsVisualDirty,
                               bool &workplaceLabelsDirty,
                               bool &statsDirty)
{
    // Задачи:
    // 1. Для каждого живого юнита вызвать ИИ соответствующей роли
    // 2. Выполнить шаг физики перемещения (если солдат не идёт по эвакуационному мосту)

    auto &buildings = g_generator.mutableBuildings();

    for (size_t i = 0; i < state_.units.size(); ++i) {
        Unit &u = state_.units[i];
        if (u.hp <= 0)
            continue;

        u.stateTimerSec -= dt;

        if (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated) {
            CitizenAI::updateBehavior(u,
                                      i,
                                      dt,
                                      state_,
                                      buildings,
                                      spatialGrid_,
                                      g_generator,
                                      navService_,
                                      evacuationSystem_,
                                      statsDirty);
        } else if (u.type == UnitType::Soldier) {
            SoldierAI::updateBehavior(u,
                                      dt,
                                      state_,
                                      buildings,
                                      spatialGrid_,
                                      g_generator,
                                      navService_,
                                      effectsSystem_);
        } else if (u.type == UnitType::Zombie) {
            ZombieAI::updateBehavior(u,
                                     dt,
                                     state_,
                                     buildings,
                                     spatialGrid_,
                                     g_generator,
                                     navService_,
                                     &wallSystem_,
                                     buildingsVisualDirty,
                                     workplaceLabelsDirty);
        }

        if (!u.onEvacBridge)
            UnitMovement::step(u, dt, g_generator, navService_, state_.navGrid);
    }
}

void GameWorld::postUpdateUnitsAndBuildings(bool buildingsVisualDirty)
{
    // Задачи:
    // 1. Пересчитать currentOccupants и reservedOccupants после шага движения
    // 2. Пересобрать геометрию зданий, если открылась/закрылась хотя бы одна крыша
    // 3. Удалить погибших юнитов (через swap-and-pop) и обновить счётчики статистики

    auto &buildings = g_generator.mutableBuildings();
    for (auto &b : buildings) {
        b.currentOccupants = 0;
        b.reservedOccupants = 0;
    }

    for (auto &u : state_.units) {
        const int bId = state_.navGrid.buildingIdAt(static_cast<int>(u.x), static_cast<int>(u.y));
        if (bId >= 0 && bId < static_cast<int>(buildings.size())) {
            u.insideBuildingId = bId;
            if (u.type != UnitType::Zombie) {
                buildings[bId].currentOccupants++;
                buildings[bId].reservedOccupants++;
            }
        } else {
            u.insideBuildingId = -1;
            if ((u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome)
                && u.targetBuildingId >= 0
                && u.targetBuildingId < static_cast<int>(buildings.size())) {
                buildings[u.targetBuildingId].reservedOccupants++;
            }
        }
    }

    for (size_t i = 0; i < buildings.size(); ++i) {
        const uint8_t currOpen = (buildings[i].currentOccupants > 0) ? 1 : 0;
        if (currOpen != prevRoofOpen[i]) {
            buildingsVisualDirty = true;
            break;
        }
    }

    if (buildingsVisualDirty) {
        buildBuildingsGeometry();
        rebuildBurningLights();
        emit mapUpdated();
    }

    int citizens = 0;
    int infected = 0;
    int zombies = 0;
    for (size_t i = 0; i < state_.units.size();) {
        if (state_.units[i].hp <= 0) {
            const Unit dead = state_.units[i];
            state_.units[i] = state_.units.back();
            state_.units.pop_back();

            if (dead.type == UnitType::Zombie) {
                state_.budget += 5.0;
                state_.eliminatedCount++;
            } else if ((dead.type == UnitType::Citizen || dead.type == UnitType::Vaccinated)
                       && !dead.isEvacuated) {
                state_.lostCount++;
            }
            emit statsChanged();
        } else {
            const auto &u = state_.units[i];
            if (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated) {
                citizens++;
                if (u.isInfected)
                    infected++;
            } else if (u.type == UnitType::Zombie) {
                zombies++;
            }
            ++i;
        }
    }

    if (citizens != state_.citizensCount || infected != state_.infectedCount
        || zombies != state_.zombiesCount) {
        state_.citizensCount = citizens;
        state_.infectedCount = infected;
        state_.zombiesCount = zombies;
        emit statsChanged();
    }
}

void GameWorld::buildBuildingsGeometry()
{
    // Задачи:
    // 1. Сформировать геометрию каждого здания: тень -> контур -> корпус (с учётом состояния)

    g_buildingsGeometry.clear();
    const auto &buildings = g_generator.buildings();
    g_buildingsGeometry.reserve(buildings.size() * 5);
    const float doorWidth = g_generator.level.roadWidthMetersToBuilding;

    for (const auto &b : buildings) {
        g_buildingsGeometry.push_back(
            {b.cx + 2.5F, b.cy + 2.5F, b.width + 4.0F, b.height + 4.0F, b.angleRad, 12, 14, 18, 180});
        g_buildingsGeometry
            .push_back({b.cx, b.cy, b.width + 4.0F, b.height + 4.0F, b.angleRad, 26, 30, 38, 255});

        if (b.isBurnedOut) {
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 28, 25, 24, 255});
        } else if (b.isBurning) {
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 235, 105, 25, 255});
            g_buildingsGeometry.push_back(
                {b.cx, b.cy, b.width * 0.52F, b.height * 0.52F, b.angleRad, 250, 185, 55, 255});
        } else if (b.currentOccupants > 0) {
            const uint8_t fr = (b.floorColor >> 16) & 0xFF;
            const uint8_t fg = (b.floorColor >> 8) & 0xFF;
            const uint8_t fb = b.floorColor & 0xFF;

            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, fr, fg, fb, 255});

            if (!b.pathFromRoad.empty()) {
                g_buildingsGeometry.push_back({static_cast<float>(b.doorCenter.x()),
                                               static_cast<float>(b.doorCenter.y()),
                                               5.0F,
                                               doorWidth,
                                               b.doorAngleRad,
                                               fr,
                                               fg,
                                               fb,
                                               255});
            }
        } else {
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, b.r, b.g, b.b, 255});
        }
    }
}

void GameWorld::spawnInitialUnits(int citizenCount)
{
    // Задачи:
    // 1. Собрать пиксели широких дорог с отступом от бордюров
    // 2. Разместить citizenCount жителей со случайным направлением и сменой дня/ночи

    auto *rng = QRandomGenerator::global();
    state_.units.clear();
    const auto &walkGrid = g_generator.walkGrid();
    const int w = g_generator.width();
    const int h = g_generator.height();
    if (w == 0 || h == 0)
        return;

    const int margin = std::max(2, static_cast<int>(g_generator.roadWidth() * 0.25F));
    std::vector<int> roadPixels;
    for (int y = margin; y < h - margin; ++y) {
        for (int x = margin; x < w - margin; ++x) {
            const int idx = y * w + x;
            if (walkGrid[idx] == 1 && walkGrid[idx + margin] == 1 && walkGrid[idx - margin] == 1
                && walkGrid[(y + margin) * w + x] == 1 && walkGrid[(y - margin) * w + x] == 1) {
                roadPixels.push_back(idx);
            }
        }
    }
    if (roadPixels.empty())
        return;

    auto &buildings = g_generator.mutableBuildings();
    int initialWorkCapacity = 0;
    for (auto &b : buildings) {
        b.currentOccupants = 0;
        b.health = b.maxHealth;
        if (b.type == BuildingType::Office || b.type == BuildingType::Factory)
            initialWorkCapacity += b.capacity;
    }

    state_.units.reserve(citizenCount);
    for (int i = 0; i < citizenCount; ++i) {
        const int idx = roadPixels[rng->bounded(static_cast<int>(roadPixels.size()))];
        const float ux = static_cast<float>(idx % w) + 0.5F;
        const float uy = static_cast<float>(idx / w) + 0.5F;
        const float angle = static_cast<float>(rng->generateDouble() * 6.28318);

        Unit u;
        u.id = state_.nextUnitId++;
        u.x = ux;
        u.y = uy;
        u.lastCheckPos = QPointF(ux, uy);
        u.vx = std::cos(angle) * UnitMovement::BASE_CITIZEN_SPEED;
        u.vy = std::sin(angle) * UnitMovement::BASE_CITIZEN_SPEED;
        u.type = UnitType::Citizen;
        u.hp = 2;
        u.isNightWorker = (rng->bounded(100) < g_generator.level.nightWorkPercent);
        u.state = UnitState::Walking;
        u.stateTimerSec = static_cast<float>(rng->bounded(4));
        u.laneOffset = static_cast<float>((rng->generateDouble() - 0.5) * 0.75);
        state_.units.push_back(u);
    }

    const int dayWorkersEstimate = std::min(static_cast<int>(citizenCount * 0.9F),
                                            initialWorkCapacity);
    state_.incomePerSec = dayWorkersEstimate * TAX_PER_WORKER_SEC;
    state_.citizensCount = citizenCount;
    state_.zombiesCount = 0;
}

void GameWorld::updateHourlyTaxes(int newHour, bool isInitialSetup)
{
    // Задачи:
    // 1. При старте карты рассчитать стартовый прогноз налогов по ёмкости рабочих мест
    // 2. Каждый игровой час суммировать пиковую занятость офисов и заводов за последние 24 часа

    auto &buildings = g_generator.mutableBuildings();

    if (isInitialSetup) {
        int totalCapacity = 0;
        for (const auto &b : buildings) {
            if (b.type == BuildingType::Office || b.type == BuildingType::Factory)
                totalCapacity += b.capacity;
        }
        const int estimatedWorkers = std::min(static_cast<int>(g_currentConfig.initialCitizens
                                                               * 0.9F),
                                              totalCapacity);
        state_.incomePerSec = estimatedWorkers * TAX_PER_WORKER_SEC;
        emit statsChanged();
        return;
    }

    int totalPeak24h = 0;
    for (auto &b : buildings) {
        if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
            int max24h = 0;
            for (int h = 0; h < 24; ++h)
                max24h = std::max(max24h, b.hourlyMaxWorkers[h]);
            totalPeak24h += max24h;
            b.hourlyMaxWorkers[newHour] = b.currentOccupants;
        }
    }

    state_.incomePerSec = totalPeak24h * TAX_PER_WORKER_SEC;
    emit statsChanged();
    rebuildWorkplaceLabels();
}

void GameWorld::processOutbreaks()
{
    for (size_t i = 0; i < g_currentConfig.outbreaks.size(); ++i) {
        auto &ob = g_currentConfig.outbreaks[i];
        if (!ob.triggered && state_.elapsedSimSec >= ob.triggerTimeSec) {
            ob.triggered = true;
            triggerSingleOutbreak(ob.initialInfected, static_cast<int>(i) + 1);
        }
    }
}

void GameWorld::triggerSingleOutbreak(int count, int index)
{
    std::vector<size_t> outdoorHealthy;
    for (size_t i = 0; i < state_.units.size(); ++i) {
        const auto &u = state_.units[i];
        if (u.type == UnitType::Citizen && !u.isInfected && u.insideBuildingId == -1)
            outdoorHealthy.push_back(i);
    }
    if (outdoorHealthy.empty())
        return;

    auto *rng = QRandomGenerator::global();
    const int toInfect = std::min(count, static_cast<int>(outdoorHealthy.size()));
    for (int k = 0; k < toInfect; ++k) {
        const int pick = rng->bounded(static_cast<int>(outdoorHealthy.size()));
        const size_t unitIdx = outdoorHealthy[pick];
        outdoorHealthy[pick] = outdoorHealthy.back();
        outdoorHealthy.pop_back();

        CitizenAI::infectUnit(state_.units[unitIdx], state_.units, true);
        emit outbreakOccurred(state_.units[unitIdx].x, state_.units[unitIdx].y, index);
    }
}

void GameWorld::rebuildBurningLights()
{
    QVariantList list;
    for (const auto &b : g_generator.buildings()) {
        if (b.isBurning) {
            QVariantMap item;
            item["cx"] = b.cx;
            item["cy"] = b.cy;
            item["radius"] = 40.0F + b.area / 5.0F;
            list.append(item);
        }
    }
    g_burningLights = list;
    emit burningLightsChanged();
}

void GameWorld::rebuildBuildingOverlays()
{
    // Задачи:
    // 1. Для каждого целого здания построить полоску заполненности и полоску здоровья

    g_buildingOverlaysGeometry.clear();
    for (const auto &b : g_generator.buildings()) {
        if (b.isBurning || b.isBurnedOut)
            continue;

        const bool isWorkplace = (b.type == BuildingType::Office || b.type == BuildingType::Factory);
        const bool showOccupancy = (b.currentOccupants > 0 && b.capacity > 0 && b.health > 0);
        const bool showHealth = (b.health < b.maxHealth && b.health > 0 && b.maxHealth > 0);
        if (!showOccupancy && !showHealth)
            continue;

        const float barW = std::max(14.0F, b.width);
        const float barH = 2.6F;
        const float halfExtentY = (std::abs(b.width * std::sin(b.angleRad))
                                   + std::abs(b.height * std::cos(b.angleRad)))
                                  * 0.5F;
        float baseY = b.cy - halfExtentY - 4.0F;

        if (showOccupancy) {
            const float fillRatio = std::clamp(static_cast<float>(b.currentOccupants) / b.capacity,
                                               0.0F,
                                               1.0F);
            g_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2F, barH + 1.2F, 0.0F, 10, 12, 16, 240});

            const uint8_t r = isWorkplace ? 250 : 110;
            const uint8_t g = isWorkplace ? 210 : 200;
            const uint8_t bl = isWorkplace ? 45 : 255;

            const float filledW = std::max(1.5F, barW * fillRatio);
            const float fillCx = (b.cx - barW * 0.5F) + filledW * 0.5F;
            g_buildingOverlaysGeometry.push_back({fillCx, baseY, filledW, barH, 0.0F, r, g, bl, 255});
            baseY -= (barH + 2.0F);
        }

        if (showHealth) {
            const float hpRatio = std::clamp(static_cast<float>(b.health) / b.maxHealth,
                                             0.0F,
                                             1.0F);
            g_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2F, barH + 1.2F, 0.0F, 10, 12, 16, 240});

            if (hpRatio > 0.0F) {
                const uint8_t r = (hpRatio < 0.5F)
                                      ? 245
                                      : static_cast<uint8_t>((1.0F - hpRatio) * 2.0F * 245);
                const uint8_t g = (hpRatio > 0.5F) ? 220
                                                   : static_cast<uint8_t>(hpRatio * 2.0F * 220);

                const float hpW = std::max(1.5F, barW * hpRatio);
                const float hpCx = (b.cx - barW * 0.5F) + hpW * 0.5F;
                g_buildingOverlaysGeometry
                    .push_back({hpCx, baseY, hpW, barH, 0.0F, r, g, 50, 255});
            }
        }
    }
}

void GameWorld::rebuildWorkplaceLabels()
{
    QVariantList list;
    for (const auto &b : g_generator.buildings()) {
        if ((b.type == BuildingType::Office || b.type == BuildingType::Factory) && b.health > 0) {
            int max24h = 0;
            for (int h = 0; h < 24; ++h)
                max24h = std::max(max24h, b.hourlyMaxWorkers[h]);
            const int activeWorkers = std::max(max24h, b.currentOccupants);
            const double income = activeWorkers * TAX_PER_WORKER_SEC;

            QVariantMap item;
            item["cx"] = b.cx;
            item["cy"] = b.cy;
            item["incomeText"] = QString("+%1₽").arg(income, 0, 'f', 2);
            item["active"] = (activeWorkers > 0);
            list.append(item);
        }
    }
    g_workplaceLabels = list;
}

void GameWorld::rebuildUnitRenderData()
{
    // Задачи:
    // 1. Преобразовать всех активных юнитов в цветные кружки RenderQuad
    // 2. Добавить пассажиров конвоев из EvacuationSystem

    g_unitGeometry.clear();
    g_unitGeometry.reserve(state_.units.size());

    for (const auto &u : state_.units) {
        uint8_t r = 240, g = 240, b = 240;
        float size = 2.0F;
        if (u.type == UnitType::Vaccinated) {
            r = 80;
            g = 180;
            b = 255;
        } else if (u.type == UnitType::Zombie) {
            r = 70;
            g = 230;
            b = 120;
            size = 2.4F;
        } else if (u.type == UnitType::Soldier) {
            g_unitGeometry.push_back({u.x, u.y, 4.4F, 4.4F, 0.0F, 255, 255, 255, 255});
            r = 65;
            g = 145;
            b = 255;
            size = 3.0F;
        } else if (u.isInfected) {
            r = 255;
            g = 175;
            b = 45;
        } else if (u.panicTimerSec > 0.0F || u.state == UnitState::PanicEvacuating) {
            r = 255;
            g = 115;
            b = 115;
        }
        g_unitGeometry.push_back({u.x, u.y, size, size, 0.0F, r, g, b, 255});
    }

    // Пассажиры эвакуации из подсистемы
    std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> tmpLayers;
    evacuationSystem_.collectRenderData(tmpLayers);
    const auto &evacCircles = tmpLayers[static_cast<size_t>(RenderLayerId::Units)].circles;
    g_unitGeometry.insert(g_unitGeometry.end(), evacCircles.begin(), evacCircles.end());
}

void GameWorld::selectBuildingAt(float worldX, float worldY)
{
    int foundId = -1;
    for (const auto &b : g_generator.buildings()) {
        const float dx = worldX - b.cx;
        const float dy = worldY - b.cy;
        const float cosA = std::cos(-b.angleRad);
        const float sinA = std::sin(-b.angleRad);
        const float localX = dx * cosA - dy * sinA;
        const float localY = dx * sinA + dy * cosA;
        if (std::abs(localX) <= b.width * 0.5F + 2.0F
            && std::abs(localY) <= b.height * 0.5F + 2.0F) {
            foundId = b.id;
            break;
        }
    }

    if (g_selectedBuildingId != foundId) {
        g_selectedBuildingId = foundId;
        updateSelectedBuildingInfo();
        emit selectedBuildingChanged();
    }
}

QString GameWorld::unitDisplayName(int unitId) const
{
    if (unitId < 0)
        return "Нет";
    for (const auto &u : state_.units) {
        if (u.id == unitId && u.hp > 0) {
            QString role = "Житель";
            if (u.type == UnitType::Soldier)
                role = "Солдат";
            else if (u.type == UnitType::Vaccinated)
                role = "Вакцинированный";
            else if (u.type == UnitType::Zombie)
                role = "Зомби";
            else if (u.isInfected)
                role = "Инфицирован";
            return QString("%1 #%2").arg(role).arg(u.id);
        }
    }
    return QString("Юнит #%1 (погиб)").arg(unitId);
}

QString GameWorld::buildingDisplayName(int buildingId) const
{
    const auto &buildings = g_generator.buildings();
    if (buildingId < 0 || buildingId >= static_cast<int>(buildings.size()))
        return "Нет";
    const auto &b = buildings[buildingId];
    QString base = "Жилой дом";
    if (b.type == BuildingType::Office)
        base = "Офис";
    else if (b.type == BuildingType::Factory)
        base = "Завод";
    return QString("%1 #%2").arg(base).arg(b.id);
}

QString GameWorld::unitStateDisplayName(const Unit &u) const
{
    if (u.type == UnitType::Zombie)
        return u.hasLastKnownTarget ? "Преследование (след)" : "Охота / Бродяжничество";
    if (u.type == UnitType::Soldier) {
        if (u.onEvacBridge)
            return "Уход по мосту";
        if (u.isRetreating)
            return "Отступление к выходу";
        return QString("Бой / Патруль (%1с)").arg(static_cast<int>(std::ceil(u.lifeTimerSec)));
    }
    if (u.panicTimerSec > 0.0F)
        return "Паника!";
    switch (u.state) {
    case UnitState::GoingToWork:
        return "Идёт на работу";
    case UnitState::Working:
        return "На работе";
    case UnitState::GoingHome:
        return "Идёт домой";
    case UnitState::AtHome:
        return "Дома";
    case UnitState::Evacuating:
        return "Эвакуация";
    case UnitState::PanicEvacuating:
        return "Паническая эвакуация";
    case UnitState::Walking:
    default:
        return "Прогулка";
    }
}

bool GameWorld::selectUnitAt(float worldX, float worldY, float radius)
{
    int foundId = -1;
    float bestDistSq = radius * radius;
    for (const auto &u : state_.units) {
        if (u.hp <= 0)
            continue;
        const float dx = u.x - worldX;
        const float dy = u.y - worldY;
        const float dSq = dx * dx + dy * dy;
        if (dSq <= bestDistSq) {
            bestDistSq = dSq;
            foundId = u.id;
        }
    }

    g_selectedUnitId = foundId;
    updateSelectedUnitInfo(true);
    emit selectedUnitChanged();
    emit unitsUpdated();
    return (foundId != -1);
}

void GameWorld::rebuildSelectedUnitPathGeometry()
{
    g_selectedPathGeometry.clear();
    const Unit *found = nullptr;
    for (const auto &u : state_.units) {
        if (u.id == g_selectedUnitId && u.hp > 0) {
            found = &u;
            break;
        }
    }
    if (!found)
        return;

    std::vector<QPointF> pts;
    pts.reserve(found->waypoints.size() + 2);
    pts.emplace_back(found->x, found->y);
    for (const auto &wp : found->waypoints)
        pts.push_back(wp);

    if (found->waypoints.empty()) {
        if (found->type == UnitType::Zombie && found->hasLastKnownTarget) {
            pts.push_back(found->lastKnownTargetPos);
        } else if (found->type == UnitType::Soldier && found->onEvacBridge) {
            pts.push_back(found->evacBridgeEnd);
        } else if ((found->state == UnitState::Evacuating
                    || found->state == UnitState::PanicEvacuating)
                   && found->targetBuildingId >= 0) {
            const auto &nodes = g_generator.nodes();
            if (found->targetBuildingId < static_cast<int>(nodes.size()))
                pts.push_back(nodes[found->targetBuildingId].pos);
        }
    }
    if (pts.size() < 2)
        return;

    uint8_t r = 246, g = 173, b = 85;
    if (found->type == UnitType::Soldier) {
        r = 99;
        g = 179;
        b = 237;
    } else if (found->type == UnitType::Zombie) {
        r = 252;
        g = 129;
        b = 129;
    } else if (found->state == UnitState::Evacuating || found->state == UnitState::PanicEvacuating) {
        r = 104;
        g = 211;
        b = 145;
    }

    const float lineThickness = 1.4F;
    const float nodeMarkerSize = 2.6F;

    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const float x1 = static_cast<float>(pts[i].x());
        const float y1 = static_cast<float>(pts[i].y());
        const float x2 = static_cast<float>(pts[i + 1].x());
        const float y2 = static_cast<float>(pts[i + 1].y());
        const float dx = x2 - x1;
        const float dy = y2 - y1;
        const float len = std::hypot(dx, dy);
        if (len < 0.05F)
            continue;
        const float cx = (x1 + x2) * 0.5F;
        const float cy = (y1 + y2) * 0.5F;
        const float angle = std::atan2(dy, dx);
        g_selectedPathGeometry
            .push_back({cx, cy, len + 0.8F, lineThickness + 1.0F, angle, 15, 20, 28, 210});
        g_selectedPathGeometry.push_back({cx, cy, len, lineThickness, angle, r, g, b, 235});
    }

    for (size_t i = 1; i < pts.size(); ++i) {
        const float px = static_cast<float>(pts[i].x());
        const float py = static_cast<float>(pts[i].y());
        const bool isFinal = (i + 1 == pts.size());
        const float sz = isFinal ? (nodeMarkerSize * 1.5F) : nodeMarkerSize;
        g_selectedPathGeometry.push_back({px, py, sz + 1.2F, sz + 1.2F, 0.0F, 15, 20, 28, 230});
        g_selectedPathGeometry.push_back({px,
                                          py,
                                          sz,
                                          sz,
                                          0.0F,
                                          isFinal ? static_cast<uint8_t>(255) : r,
                                          isFinal ? static_cast<uint8_t>(255) : g,
                                          isFinal ? static_cast<uint8_t>(255) : b,
                                          255});
    }
}

void GameWorld::updateSelectedUnitInfo(bool logDebug)
{
    // Задачи:
    // 1. Найти выбранного живого юнита и заполнить QVariantMap для карточки в QML
    // 2. Определить отображаемую цель (жертва, выход эвакуации или здание) и построить линию пути

    QVariantMap info;
    const Unit *found = nullptr;
    for (const auto &u : state_.units) {
        if (u.id == g_selectedUnitId && u.hp > 0) {
            found = &u;
            break;
        }
    }
    if (!found) {
        g_selectedUnitId = -1;
        info["valid"] = false;
        g_selectedUnitInfo = info;
        g_selectedPathGeometry.clear();
        return;
    }

    QString roleName = "Житель";
    if (found->type == UnitType::Vaccinated)
        roleName = "Вакцинированный";
    else if (found->type == UnitType::Zombie)
        roleName = "Зомби";
    else if (found->type == UnitType::Soldier)
        roleName = "Солдат";
    else if (found->isInfected)
        roleName = "Инфицирован";

    QString targetName = "Нет";
    const auto &buildings = g_generator.buildings();
    const auto &nodes = g_generator.nodes();

    if (found->type == UnitType::Zombie && found->hasLastKnownTarget) {
        targetName = unitDisplayName(found->targetUnitId)
                     + QString(" (x:%1, y:%2)")
                           .arg(found->lastKnownTargetPos.x(), 0, 'f', 1)
                           .arg(found->lastKnownTargetPos.y(), 0, 'f', 1);
    } else if (found->type == UnitType::Soldier && found->targetUnitId != -1) {
        QString targetPos;
        for (const auto &cand : state_.units) {
            if (cand.id == found->targetUnitId && cand.hp > 0) {
                targetPos = QString(" (x:%1, y:%2)").arg(cand.x, 0, 'f', 1).arg(cand.y, 0, 'f', 1);
                break;
            }
        }
        targetName = unitDisplayName(found->targetUnitId) + targetPos;
    } else if (found->type == UnitType::Soldier && found->isRetreating) {
        if (found->targetBuildingId >= 0
            && found->targetBuildingId < static_cast<int>(nodes.size())) {
            const auto &node = nodes[found->targetBuildingId];
            targetName = node.evacName.isEmpty() ? QString("Выход #%1").arg(node.id) : node.evacName;
        }
    } else if (found->state == UnitState::Evacuating || found->state == UnitState::PanicEvacuating) {
        if (found->targetBuildingId >= 0
            && found->targetBuildingId < static_cast<int>(nodes.size())) {
            const auto &node = nodes[found->targetBuildingId];
            targetName = node.evacName.isEmpty() ? QString("Эвакуация #%1").arg(node.id)
                                                 : node.evacName;
        }
    } else if (found->targetBuildingId >= 0
               && found->targetBuildingId < static_cast<int>(buildings.size())) {
        targetName = buildingDisplayName(found->targetBuildingId);
    }

    const QString insideName = (found->insideBuildingId != -1)
                                   ? buildingDisplayName(found->insideBuildingId)
                                   : "Улица";

    info["valid"] = true;
    info["id"] = found->id;
    info["role"] = roleName;
    info["state"] = unitStateDisplayName(*found);
    info["target"] = targetName;
    info["inside"] = insideName;
    info["hp"] = found->hp;
    info["x"] = found->x;
    info["y"] = found->y;
    g_selectedUnitInfo = info;

    if (logDebug) {
        qDebug().noquote() << QString("=== UNIT DEBUG #%1 (%2) ===").arg(found->id).arg(roleName)
                           << "Pos:" << found->x << found->y << "Target:" << targetName;
    }

    rebuildSelectedUnitPathGeometry();
}

void GameWorld::updateSelectedBuildingInfo()
{
    QVariantMap info;
    const auto &buildings = g_generator.buildings();
    if (g_selectedBuildingId < 0 || g_selectedBuildingId >= static_cast<int>(buildings.size())) {
        info["valid"] = false;
        g_selectedBuildingInfo = info;
        return;
    }

    const auto &b = buildings[g_selectedBuildingId];
    QString typeName = "Жилой дом";
    if (b.type == BuildingType::Office)
        typeName = "Офис";
    else if (b.type == BuildingType::Factory)
        typeName = "Завод";
    if (b.isBurnedOut)
        typeName += " (Сгорело)";
    else if (b.isBurning)
        typeName += " (Горит!)";

    const float halfExtentY = (std::abs(b.width * std::sin(b.angleRad))
                               + std::abs(b.height * std::cos(b.angleRad)))
                              * 0.5F;

    info["valid"] = true;
    info["type"] = typeName;
    info["id"] = b.id;
    info["occupants"] = b.currentOccupants;
    info["capacity"] = b.capacity;
    info["health"] = b.health;
    info["maxHealth"] = b.maxHealth;
    info["cx"] = b.cx;
    info["topY"] = b.cy - halfExtentY;
    g_selectedBuildingInfo = info;
}

// --- Делегирование QML-методов в подсистемы ---

QVariantMap GameWorld::previewWall(float x1, float y1, float x2, float y2) const
{
    return wallSystem_.previewWall(x1,
                                   y1,
                                   x2,
                                   y2,
                                   state_.budget,
                                   g_generator.walkGrid(),
                                   state_.navGrid);
}

bool GameWorld::placeWall(float x1, float y1, float x2, float y2)
{
    if (!wallSystem_.placeWall(x1, y1, x2, y2, g_generator.walkGrid(), state_))
        return false;

    if (wallSystem_.consumeMapDirtyFlag())
        emit mapUpdated();
    if (wallSystem_.consumeOverlaysDirtyFlag())
        emit wallsChanged();
    emit statsChanged();
    return true;
}

bool GameWorld::selectWallAt(float worldX, float worldY)
{
    const bool hit = wallSystem_.selectWallAt(worldX, worldY);
    if (wallSystem_.consumeSelectionDirtyFlag())
        emit selectedWallChanged();
    return hit;
}

bool GameWorld::startWallTeardown(int wallId)
{
    if (!wallSystem_.startWallTeardown(wallId, state_))
        return false;

    if (wallSystem_.consumeSelectionDirtyFlag())
        emit selectedWallChanged();
    if (wallSystem_.consumeOverlaysDirtyFlag())
        emit wallsChanged();
    if (wallSystem_.consumeMapDirtyFlag())
        emit mapUpdated();
    emit statsChanged();
    return true;
}

QVariantMap GameWorld::snapEvacPoint(float worldX, float worldY, float snapRadius) const
{
    return navService_.snapEvacPoint(worldX, worldY, snapRadius, g_generator.nodes());
}

void GameWorld::setEquippedTools(const QVariantList &tools)
{
    if (g_equippedTools != tools) {
        g_equippedTools = tools;
        if (!g_equippedTools.contains(g_selectedTool))
            setSelectedTool(0);
        emit equippedToolsChanged();
    }
}

void GameWorld::selectAt(float worldX, float worldY)
{
    // Задачи:
    // 1. Последовательно проверить попадание в юнита -> стену -> здание
    // 2. Сбросить выделение остальных типов при выборе одного объекта

    if (selectUnitAt(worldX, worldY, 7.0F)) {
        selectBuildingAt(-1000.0F, -1000.0F);
        selectWallAt(-1000.0F, -1000.0F);
        return;
    }
    if (selectWallAt(worldX, worldY)) {
        selectBuildingAt(-1000.0F, -1000.0F);
        return;
    }
    selectBuildingAt(worldX, worldY);
}

QVariantMap GameWorld::previewTool(int toolId, float x1, float y1, float x2, float y2) const
{
    return toolRegistry_.previewTool(toolId,
                                     x1,
                                     y1,
                                     x2,
                                     y2,
                                     state_,
                                     g_generator,
                                     navService_,
                                     wallSystem_);
}

bool GameWorld::useToolSegment(int toolId, float x1, float y1, float x2, float y2)
{
    // Задачи:
    // 1. Сформировать ToolExecutionContext и делегировать выполнение в ToolRegistry
    // 2. Пробросить соответствующие сигналы обновления в QML

    ToolExecutionContext ctx{
        state_,
        g_generator,
        navService_,
        effectsSystem_,
        helicopterSystem_,
        evacuationSystem_,
        wallSystem_,
        [this](float x, float y, float radius, const QString &color) {
            emit pulseWaveTriggered(x, y, radius, color);
        },
        [this]() { rebuildUnitRenderData(); },
    };

    const bool ok = toolRegistry_.executeTool(toolId, x1, y1, x2, y2, ctx);
    if (!ok)
        return false;

    if (ctx.unitsChanged)
        emit unitsUpdated();
    if (ctx.helicoptersChanged)
        emit helicoptersChanged();
    if (ctx.evacuationsChanged)
        emit evacuationsChanged();
    if (ctx.wallsChanged)
        emit wallsChanged();
    if (ctx.mapUpdated)
        emit mapUpdated();
    if (ctx.statsChanged)
        emit statsChanged();
    return true;
}

void GameWorld::useToolAt(float worldX, float worldY)
{
    useToolSegment(g_selectedTool, worldX, worldY, worldX, worldY);
}

void GameWorld::regenerateMap()
{
    startLevel(g_currentConfig);
}

void GameWorld::setSelectedTool(int tool)
{
    if (g_selectedTool != tool) {
        g_selectedTool = tool;
        emit stateChanged();
    }
}

void GameWorld::togglePause()
{
    state_.paused = !state_.paused;
    emit stateChanged();
}

void GameWorld::toggleSpeedTo1()
{
    state_.speedMultiplier = 1;
    emit stateChanged();
}

void GameWorld::toggleSpeedTo2()
{
    state_.speedMultiplier = 2;
    emit stateChanged();
}

void GameWorld::toggleSpeedTo3()
{
    state_.speedMultiplier = 3;
    emit stateChanged();
}

void GameWorld::toggleSpeedTo4()
{
    state_.speedMultiplier = 4;
    emit stateChanged();
}

QString GameWorld::formattedTime() const
{
    const int totalMinutes = static_cast<int>(state_.timeOfDayHours * 60.0F);
    const int hh = (totalMinutes / 60) % 24;
    const int mm = totalMinutes % 60;
    return QString("%1:%2").arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'));
}

bool GameWorld::isNight() const
{
    return (state_.timeOfDayHours >= 21.0F || state_.timeOfDayHours < 5.5F);
}

float GameWorld::nightDarkness() const
{
    constexpr float maxOp = 0.40F;
    if (state_.timeOfDayHours >= 21.5F || state_.timeOfDayHours < 4.5F)
        return maxOp;
    if (state_.timeOfDayHours >= 19.0F && state_.timeOfDayHours < 21.5F)
        return ((state_.timeOfDayHours - 19.0F) / 2.5F) * maxOp;
    if (state_.timeOfDayHours >= 4.5F && state_.timeOfDayHours < 7.0F)
        return (1.0F - (state_.timeOfDayHours - 4.5F) / 2.5F) * maxOp;
    return 0.0F;
}

#include <QDebug>
#include <QRandomGenerator>
#include <QVariant>
#include <algorithm>
#include <cmath>

#include "Ai.h"
#include "AiSolder.h"
#include "Effects.h"
#include "Evacuation.h"
#include "GameWorld.h"
#include "Helicopter.h"
#include "Navigation.h"

GameWorld::GameWorld(QObject *parent)
    : QObject(parent)
{
    LevelConfig defaultCfg;
    defaultCfg.outbreaks = {{4.0f, 1, false}, {30.0f, 1, false}};

    startLevel(defaultCfg);

    connect(&g_timer, &QTimer::timeout, this, &GameWorld::tick);
    g_timer.start(16);
}

void GameWorld::startLevel(const LevelConfig &config)
{
    // Задачи:
    // 1. Сохранить конфиг и сбросить флаги вспышек
    // 2. Сбросить runtime-состояние симуляции
    // 3. Сгенерировать карту и вспомогательные структуры
    // 4. Создать юнитов и пересчитать экономику
    // 5. Подготовить данные рендера и оповестить QML

    // 1. Конфиг
    g_currentConfig = config;
    for (auto &ob : g_currentConfig.outbreaks) {
        ob.triggered = false;
    }

    // 2. Runtime-состояние
    g_budget = 300.0;
    g_eliminatedCount = 0;
    g_elapsedSimSec = 0.0f;
    g_timeOfDayHours = g_currentConfig.startHour;
    g_dayNumber = 1;
    g_lastTrackedHour = static_cast<int>(g_timeOfDayHours) % 24;
    g_selectedBuildingId = -1;
    g_paused = true;
    g_helicopters.clear();

    // 3. Карта и граф
    g_generator.generateFromLevel(g_currentLevel);
    g_evacs.clear();
    rebuildActiveEvacuationsList();
    buildRoadGraphCache();
    buildBuildingsGeometry();

    // 4. Юниты и налоги
    spawnInitialUnits(g_currentConfig.initialCitizens);
    updateHourlyTaxes(g_lastTrackedHour, true);

    // 5. Рендер и сигналы
    rebuildUnitRenderData();
    rebuildWorkplaceLabels();
    rebuildEvacPoints();
    updateSelectedBuildingInfo();
    rebuildHelicoptersRenderData();

    emit helicoptersChanged();
    emit selectedBuildingChanged();
    emit evacuationsChanged();
    emit mapUpdated();
    emit unitsUpdated();
    emit statsChanged();
    emit timeChanged();
    emit stateChanged();
}

void GameWorld::buildBuildingsGeometry()
{
    // Задачи:
    // 1. Очистить и зарезервировать геометрию зданий
    // 2. Для каждого здания добавить: тень → контур → корпус (по состоянию)
    //    Состояния: обугленное / горящее / занятое (крыша снята + дверь) / пустое

    g_buildingsGeometry.clear();
    const auto &buildings = g_generator.buildings();
    g_buildingsGeometry.reserve(buildings.size() * 5);
    const float doorWidth = g_generator.level.roadWidthMetersToBuilding;

    for (const auto &b : buildings) {
        // Тень
        g_buildingsGeometry.push_back(
            {b.cx + 2.5F, b.cy + 2.5F, b.width + 4.0F, b.height + 4.0F, b.angleRad, 12, 14, 18, 180});

        // Контур стен
        g_buildingsGeometry
            .push_back({b.cx, b.cy, b.width + 4.0F, b.height + 4.0F, b.angleRad, 26, 30, 38, 255});

        if (b.isBurnedOut) {
            // Обугленное
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 28, 25, 24, 255});
        } else if (b.isBurning) {
            // Горящее: оранжевый корпус + жёлтое ядро
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 235, 105, 25, 255});
            g_buildingsGeometry.push_back(
                {b.cx, b.cy, b.width * 0.52F, b.height * 0.52F, b.angleRad, 250, 185, 55, 255});
        } else if (b.currentOccupants > 0) {
            uint8_t fr = (b.floorColor >> 16) & 0xFF;
            uint8_t fg = (b.floorColor >> 8) & 0xFF;
            uint8_t fb = b.floorColor & 0xFF;

            // Крыша снята — рисуем пол
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, fr, fg, fb, 255});

            // Проход в стене (дверь)
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
            // Пустое — закрытая крыша
            g_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, b.r, b.g, b.b, 255});
        }
    }
}

void GameWorld::spawnInitialUnits(int citizenCount)
{
    auto *rng = QRandomGenerator::global();
    g_units.clear();
    const auto &walkGrid = g_generator.walkGrid();
    int w = g_generator.width();
    int h = g_generator.height();
    if (w == 0 || h == 0)
        return;
    int margin = std::max(2, static_cast<int>(roadWidth() * 0.25f));
    std::vector<int> roadPixels;
    for (int y = margin; y < h - margin; ++y) {
        for (int x = margin; x < w - margin; ++x) {
            int idx = y * w + x;
            if (walkGrid[idx] == 1 && walkGrid[idx + margin] == 1 && walkGrid[idx - margin] == 1
                && walkGrid[(y + margin) * w + x] == 1 && walkGrid[(y - margin) * w + x] == 1) {
                roadPixels.push_back(idx);
            }
        }
    }

    if (roadPixels.empty())
        return;

    auto &buildings = const_cast<std::vector<BuildingObject> &>(g_generator.buildings());
    int initialWorkCapacity = 0;
    for (auto &b : buildings) {
        b.currentOccupants = 0;
        b.health = b.maxHealth;
        if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
            initialWorkCapacity += b.capacity;
        }
    }
    g_units.reserve(citizenCount);
    for (int i = 0; i < citizenCount; ++i) {
        int idx = roadPixels[rng->bounded(static_cast<double>(roadPixels.size()))];
        float ux = static_cast<double>(idx % w) + 0.5f;
        float uy = static_cast<double>(idx / w) + 0.5f;
        float angle = static_cast<double>(rng->generateDouble() * 6.28318);
        Unit u;
        u.x = ux;
        u.y = uy;
        u.lastCheckPos = QPointF(ux, uy);
        u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
        u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
        u.type = UnitType::Citizen;
        u.hp = 2; // Здоровье жителя = 2
        u.isNightWorker = (rng->bounded(100) < g_generator.level.nightWorkPercent);
        u.state = UnitState::Walking;
        u.stateTimerSec = static_cast<double>(rng->bounded(4));
        u.laneOffset = static_cast<float>(
            (rng->generateDouble() - 0.5)
            * 0.75); // Случайная полоса в пределах 75% ширины дороги (-0.375 .. +0.375)

        g_units.push_back(u);
    }
    int dayWorkersEstimate = std::min(static_cast<int>(citizenCount * 0.9f), initialWorkCapacity);
    g_incomePerSec = dayWorkersEstimate * TAX_PER_WORKER_SEC;
    g_citizensCount = citizenCount;
    g_zombiesCount = 0;
}
void GameWorld::updateHourlyTaxes(int newHour, bool isInitialSetup)
{
    auto &buildings = const_cast<std::vector<BuildingObject> &>(g_generator.buildings());

    if (isInitialSetup) {
        // При старте карты заполняем базовый прогноз, чтобы доход сразу работал с первой секунды
        int totalCapacity = 0;
        for (const auto &b : buildings) {
            if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
                totalCapacity += b.capacity;
            }
        }
        int estimatedWorkers = std::min(static_cast<int>(g_currentConfig.initialCitizens * 0.9f),
                                        totalCapacity);
        g_incomePerSec = estimatedWorkers * TAX_PER_WORKER_SEC;
        emit statsChanged();
        return;
    }

    int totalPeak24h = 0;
    for (auto &b : buildings) {
        if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
            // Ищем максимум одновременных работников за последние 24 часа
            int max24h = 0;
            for (int h = 0; h < 24; ++h) {
                if (b.hourlyMaxWorkers[h] > max24h) {
                    max24h = b.hourlyMaxWorkers[h];
                }
            }
            totalPeak24h += max24h;

            // Обнуляем ячейку наступившего часа и записываем тех, кто уже внутри
            b.hourlyMaxWorkers[newHour] = b.currentOccupants;
        }
    }

    g_incomePerSec = totalPeak24h * TAX_PER_WORKER_SEC;
    emit statsChanged();
    rebuildWorkplaceLabels();
}

void GameWorld::processOutbreaks()
{
    for (size_t i = 0; i < g_currentConfig.outbreaks.size(); ++i) {
        auto &ob = g_currentConfig.outbreaks[i];
        if (!ob.triggered && g_elapsedSimSec >= ob.triggerTimeSec) {
            ob.triggered = true;
            triggerSingleOutbreak(ob.initialInfected, static_cast<double>(i) + 1);
        }
    }
}
void GameWorld::triggerSingleOutbreak(int count, int index)
{
    std::vector<size_t> outdoorHealthy;
    for (size_t i = 0; i < g_units.size(); ++i) {
        if (g_units[i].type == UnitType::Citizen && !g_units[i].isInfected
            && g_units[i].insideBuildingId == -1) {
            outdoorHealthy.push_back(i);
        }
    }
    if (outdoorHealthy.empty())
        return;
    auto *rng = QRandomGenerator::global();
    int toInfect = std::min(count, static_cast<int>(outdoorHealthy.size()));
    for (int k = 0; k < toInfect; ++k) {
        int pick = rng->bounded(static_cast<int>(outdoorHealthy.size()));
        size_t unitIdx = outdoorHealthy[pick];
        outdoorHealthy[pick] = outdoorHealthy.back();
        outdoorHealthy.pop_back();
        // Нулевой пациент сразу становится зомби
        infectUnit(g_units[unitIdx], true);
        emit outbreakOccurred(g_units[unitIdx].x, g_units[unitIdx].y, index);
    }
}

void GameWorld::rebuildBurningLights()
{
    QVariantList list;
    const auto &buildings = g_generator.buildings();

    for (const auto &b : buildings) {
        if (b.isBurning) {
            QVariantMap item;
            item["cx"] = b.cx;
            item["cy"] = b.cy;
            item["radius"] = 40 + b.area / 5.0f; // Радиус освещения X метров
            list.append(item);
        }
    }
    g_burningLights = list;
    emit burningLightsChanged();
}

void GameWorld::rebuildBuildingOverlays()
{
    g_buildingOverlaysGeometry.clear();
    const auto &buildings = g_generator.buildings();

    for (const auto &b : buildings) {
        // Над горящими и обугленными зданиями полоски не показываем
        if (b.isBurning || b.isBurnedOut)
            continue;

        bool isWorkplace = (b.type == BuildingType::Office || b.type == BuildingType::Factory);
        bool showOccupancy = (b.currentOccupants > 0 && b.capacity > 0 && b.health > 0);
        bool showHealth = (b.health < b.maxHealth && b.health > 0 && b.maxHealth > 0);

        if (!showOccupancy && !showHealth)
            continue;

        float barW = std::max(14.0f, b.width);
        float barH = 2.6f;
        float halfExtentY = (std::abs(b.width * std::sin(b.angleRad))
                             + std::abs(b.height * std::cos(b.angleRad)))
                            * 0.5f;
        float baseY = b.cy - halfExtentY - 4.0f;

        if (showOccupancy) {
            float fillRatio = std::clamp(static_cast<float>(b.currentOccupants) / b.capacity,
                                         0.0f,
                                         1.0f);

            g_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2f, barH + 1.2f, 0.0f, 10, 12, 16, 240});

            // Производственные здания (офисы/заводы) -> ЖЁЛТАЯ заполненность; жилые -> голубая
            uint8_t r = isWorkplace ? 250 : 110;
            uint8_t g = isWorkplace ? 210 : 200;
            uint8_t bl = isWorkplace ? 45 : 255;

            float filledW = std::max(1.5f, barW * fillRatio);
            float fillCx = (b.cx - barW * 0.5f) + filledW * 0.5f;
            g_buildingOverlaysGeometry
                .push_back({fillCx, baseY, filledW, barH, 0.0f, r, g, bl, 255});

            baseY -= (barH + 2.0f);
        }

        if (showHealth) {
            float hpRatio = std::clamp(static_cast<float>(b.health) / b.maxHealth, 0.0f, 1.0f);

            g_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2f, barH + 1.2f, 0.0f, 10, 12, 16, 240});

            if (hpRatio > 0.0f) {
                uint8_t r = (hpRatio < 0.5f) ? 245
                                             : static_cast<uint8_t>((1.0f - hpRatio) * 2.0f * 245);
                uint8_t g = (hpRatio > 0.5f) ? 220 : static_cast<uint8_t>(hpRatio * 2.0f * 220);

                float hpW = std::max(1.5f, barW * hpRatio);
                float hpCx = (b.cx - barW * 0.5f) + hpW * 0.5f;
                g_buildingOverlaysGeometry.push_back({hpCx, baseY, hpW, barH, 0.0f, r, g, 50, 255});
            }
        }
    }
}

void GameWorld::rebuildWorkplaceLabels()
{
    QVariantList list;
    const auto &buildings = g_generator.buildings();

    for (const auto &b : buildings) {
        if ((b.type == BuildingType::Office || b.type == BuildingType::Factory) && b.health > 0) {
            int max24h = 0;
            for (int h = 0; h < 24; ++h) {
                if (b.hourlyMaxWorkers[h] > max24h)
                    max24h = b.hourlyMaxWorkers[h];
            }
            // Показываем либо зафиксированный суточный доход, либо текущий активный в первый день
            int activeWorkers = std::max(max24h, b.currentOccupants);
            double income = activeWorkers * TAX_PER_WORKER_SEC;

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

void GameWorld::tick()
{
    // Главный цикл симуляции (~60 FPS).
    // Задачи:
    // 1. Обновить игровое время и бюджет
    // 2. Обработать вспышки инфекции
    // 3. Определить, в каком здании находится каждый юнит + заполненность
    // 4. Обновить таймеры зданий (атака / горение)
    // 5. Обновить инкубацию заражённых и кулдауны атак
    // 6. Собрать списки активных целей
    // 7. Обновить поведение юнитов (peopleUpdateActions)
    // 8. Обновить эвакуации, эффекты и данные рендера

    if (g_paused)
        return;

    auto *rng = QRandomGenerator::global();
    const float dt = 0.016f * g_speedMultiplier;
    const float dtHours = dt * g_currentConfig.gameHoursPerSec;

    // 1. Время и бюджет
    g_elapsedSimSec += dt;
    g_timeOfDayHours += dtHours;
    if (g_timeOfDayHours >= 24.0f) {
        g_timeOfDayHours -= 24.0f;
        g_dayNumber++;
    }

    int currentHour = static_cast<int>(g_timeOfDayHours) % 24;
    if (currentHour != g_lastTrackedHour) {
        g_lastTrackedHour = currentHour;
        updateHourlyTaxes(currentHour, false);
    }
    emit timeChanged();

    if (g_incomePerSec > 0.0) {
        g_budget += g_incomePerSec * dt;
        emit statsChanged();
    }

    // 2. Вспышки
    processOutbreaks();

    auto &buildings = const_cast<std::vector<BuildingObject> &>(g_generator.buildings());
    bool buildingsVisualDirty = false;

    // 3. Определяем зону каждого юнита и считаем заполненность зданий
    const auto &walkGrid = g_generator.walkGrid();
    int mapW = g_generator.width();
    int mapH = g_generator.height();
    int currentHourIdx = static_cast<int>(g_timeOfDayHours) % 24;

    for (auto &b : buildings) {
        b.currentOccupants = 0;
    }
    for (auto &u : g_units) {
        int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
        int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
        int16_t cell = walkGrid[gy * mapW + gx];

        if (cell >= 10 && (cell - 10) < static_cast<int>(buildings.size())) {
            int bId = cell - 10;
            u.insideBuildingId = bId;

            if (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated) {
                auto &b = buildings[bId];
                b.currentOccupants++;
                if ((b.type == BuildingType::Office || b.type == BuildingType::Factory)
                    && b.currentOccupants > b.hourlyMaxWorkers[currentHourIdx]) {
                    b.hourlyMaxWorkers[currentHourIdx] = b.currentOccupants;
                }
            }
        } else {
            u.insideBuildingId = -1;
        }
    }

    // 4. Таймеры зданий (атака / горение)
    for (auto &b : buildings) {
        if (b.underAttackTimerSec > 0.0f) {
            b.underAttackTimerSec = std::max(0.0f, b.underAttackTimerSec - dt);
        }
        if (b.isBurning) {
            b.burnTimerSec -= dt;
            if (b.burnTimerSec <= 0.0f) {
                b.isBurning = false;
                b.isBurnedOut = true;
                buildingsVisualDirty = true;
                rebuildWorkplaceLabels();
            }
        }
    }

    // 5. Инкубация заражённых + кулдауны атак
    for (size_t i = 0; i < g_units.size(); ++i) {
        Unit &u = g_units[i];
        if (u.isInfected && u.type != UnitType::Zombie) {
            u.infectionHoursLeft -= dtHours;
            if (u.infectionHoursLeft <= 0.0F) {
                int bId = u.insideBuildingId;
                if (bId >= 0 && bId < static_cast<int>(buildings.size())) {
                    const auto &b = buildings[bId];
                    // Превращение внутри здания → остальные в панике бегут к выходу
                    for (auto &other : g_units) {
                        if (other.insideBuildingId == bId && &other != &u) {
                            other.panicTimerSec = 3.0F;
                            other.state = UnitState::Walking;
                            other.stateTimerSec = 6.0F;
                            other.waypoints.clear();
                            for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p) {
                                other.waypoints.push_back(b.pathFromRoad[p]);
                            }
                        }
                    }
                }
                infectUnit(u, true);
            }
        }
        if (u.attackCooldownSec > 0.0f) {
            u.attackCooldownSec -= dt;
        }
    }

    // 6. Списки активных целей
    std::vector<size_t> activeZombies;
    std::vector<size_t> panickingOutdoors;
    std::vector<size_t> zombieTargets;

    for (size_t i = 0; i < g_units.size(); ++i) {
        const auto &u = g_units[i];
        if (u.hp <= 0)
            continue;

        if (u.type == UnitType::Zombie) {
            activeZombies.push_back(i);
        } else {
            if (u.insideBuildingId == -1 && u.panicTimerSec > 0.0f) {
                panickingOutdoors.push_back(i);
            }
            if (!u.isInfected) {
                zombieTargets.push_back(i);
            }
        }
    }

    // Сохраняем состояние занятости зданий (пусто / есть люди) до шага симуляции
    std::vector<uint8_t> prevRoofOpen(buildings.size(), 0);
    for (size_t i = 0; i < buildings.size(); ++i) {
        prevRoofOpen[i] = (buildings[i].currentOccupants > 0) ? 1 : 0;
    }

    peopleUpdateActions(dt, buildings, activeZombies, panickingOutdoors, zombieTargets,
                        buildingsVisualDirty);

    // обновляем currentOccupants по актуальным координатам
    for (auto &b : buildings) {
        b.currentOccupants = 0;
        b.reservedOccupants = 0;
    }

    for (auto &u : g_units) {
        int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
        int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
        int16_t cell = walkGrid[gy * mapW + gx];

        if (cell >= 10 && (cell - 10) < static_cast<int>(buildings.size())) {
            int bId = cell - 10;
            u.insideBuildingId = bId;
            if (u.type != UnitType::Zombie) {
                buildings[bId].currentOccupants++;
                buildings[bId].reservedOccupants++;
            }
        } else {
            u.insideBuildingId = -1;
            // Если юнит ещё на улице, но уже забронировал это здание — учитываем в reservedOccupants
            if ((u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome)
                && u.targetBuildingId >= 0
                && u.targetBuildingId < static_cast<int>(buildings.size())) {
                buildings[u.targetBuildingId].reservedOccupants++;
            }
        }
    }

    // Теперь сравнение покажет реальное изменение (вошёл первый человек или вышел последний)
    for (size_t i = 0; i < buildings.size(); ++i) {
        uint8_t currOpen = (buildings[i].currentOccupants > 0) ? 1 : 0;
        if (currOpen != prevRoofOpen[i]) {
            buildingsVisualDirty = true;
            break;
        }
    }

    if (buildingsVisualDirty) {
        buildBuildingsGeometry();
        emit mapUpdated();
    }

    // 4. Удаляем погибших и обновляем счётчики
    int citizens = 0;
    int zombies = 0;
    for (size_t i = 0; i < g_units.size();) {
        if (g_units[i].hp <= 0) {
            g_units[i] = g_units.back();
            g_units.pop_back();
        } else {
            if (g_units[i].type == UnitType::Citizen || g_units[i].type == UnitType::Vaccinated)
                citizens++;
            else if (g_units[i].type == UnitType::Zombie)
                zombies++;
            ++i;
        }
    }
    if (citizens != g_citizensCount || zombies != g_zombiesCount) {
        g_citizensCount = citizens;
        g_zombiesCount = zombies;
        emit statsChanged();
    }
    updateEffects(dt);
    updateHelicopters(dt);
    updateEvacuations(dt);
    // Постоянный призыв на эвакуацию всех жителей (на улице и внутри зданий), оказавшихся в радиусе 100м
    for (const auto &ev : g_evacs) {
        if (ev.completed)
            continue; // Когда 50 мест заняты или 30с вышли, новых больше не зовём

        for (auto &u : g_units) {
            if (u.type != UnitType::Citizen && u.type != UnitType::Vaccinated)
                continue;
            if ((u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating)
                && u.targetBuildingId == ev.evacNodeId)
                continue; // Уже бежит на эту эвакуацию

            float ux = u.x, uy = u.y;
            if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
                ux = buildings[u.insideBuildingId].cx;
                uy = buildings[u.insideBuildingId].cy;
            }

            float dx = ux - static_cast<float>(ev.evacPos.x());
            float dy = uy - static_cast<float>(ev.evacPos.y());
            if (dx * dx + dy * dy <= 100.0F * 100.0F) {
                if (u.panicTimerSec > 0.0F) {
                    u.state = UnitState::PanicEvacuating;
                } else {
                    u.state = UnitState::Evacuating;
                }
                u.targetBuildingId = ev.evacNodeId;
                u.waypoints = buildPathBetween(QPointF(u.x, u.y), ev.evacPos);

                // Если юнит был внутри здания — сначала выводим его по тропинке наружу
                int bId = u.insideBuildingId;
                if (bId >= 0 && bId < static_cast<int>(buildings.size())) {
                    const auto &b = buildings[bId];
                    std::vector<QPointF> exitPts;
                    for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p) {
                        exitPts.push_back(b.pathFromRoad[p]);
                    }
                    u.waypoints.insert(u.waypoints.begin(), exitPts.begin(), exitPts.end());
                }
            }
        }
    }

    rebuildUnitRenderData();
    rebuildBuildingOverlays();
    if (g_selectedBuildingId != -1) {
        updateSelectedBuildingInfo();
        emit selectedBuildingChanged();
    }

    emit unitsUpdated();
}
void GameWorld::peopleUpdateActions(float dt,
                                    std::vector<BuildingObject> &buildings,
                                    const std::vector<size_t> &activeZombies,
                                    const std::vector<size_t> &panickingOutdoors,
                                    const std::vector<size_t> &zombieTargets,
                                    bool &buildingsVisualDirty)
{
    // Задачи:
    // 1. Для каждого живого юнита выбрать обработчик по типу
    // 2. Применить урон от огня, отталкивание от стен и шаг перемещения

    for (size_t i = 0; i < g_units.size(); ++i) {
        Unit &u = g_units[i];
        if (u.hp <= 0)
            continue;

        // 1. Поведение по типу
        if (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated) {
            updateCitizenBehavior(u, i, dt, buildings, activeZombies, panickingOutdoors);
        } else if (u.type == UnitType::Soldier) {
            updateSoldierBehavior(u, dt, buildings, activeZombies);
        } else if (u.type == UnitType::Zombie) {
            updateZombieBehavior(u, dt, buildings, zombieTargets, buildingsVisualDirty);
        }

        // 2. Общая физика шага
        if (u.onEvacBridge == false) // Если не эвакуируется
            stepUnitMovement(u, dt, buildings);
    }
}

void GameWorld::updateCitizenBehavior(Unit &u,
                                      size_t unitIndex,
                                      float dt,
                                      std::vector<BuildingObject> &buildings,
                                      const std::vector<size_t> &activeZombies,
                                      const std::vector<size_t> &panickingOutdoors)
{
    // Задачи:
    // 1. Эвакуация (приоритет)
    // 2. Поведение внутри здания (пожар / зомби внутри / атака двери / расписание)
    // 3. Поведение на улице (угроза зомби / паника толпы / расписание)

    auto *rng = QRandomGenerator::global();
    const auto &walkGrid = g_generator.walkGrid();
    const int mapW = g_generator.width();
    const int mapH = g_generator.height();

    const bool isInside = (u.insideBuildingId >= 0
                           && u.insideBuildingId < static_cast<int>(buildings.size()));

    // 1. Эвакуация всегда в приоритете
    if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
        u.panicTimerSec = 0.0f;
        updateCitizenSchedule(u, dt, buildings);
        return;
    }

    // 2. Внутри здания
    if (isInside) {
        const auto &b = buildings[u.insideBuildingId];

        bool zombieInsideRoom = false;
        for (size_t zIdx : activeZombies) {
            if (g_units[zIdx].insideBuildingId == u.insideBuildingId) {
                zombieInsideRoom = true;
                break;
            }
        }

        // 2a. Здание горит / обуглено / зомби внутри → бежим наружу
        if (b.isBurning || b.isBurnedOut || zombieInsideRoom) {
            u.panicTimerSec = 3.0f;
            u.state = UnitState::Walking;
            u.stateTimerSec = 6.0f;

            if (u.waypoints.empty()) {
                for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
                    u.waypoints.push_back(b.pathFromRoad[p]);
            }

            float routeDirX = 0.0f, routeDirY = 0.0f;
            if (!u.waypoints.empty()) {
                QPointF target = u.waypoints.front();
                float dx = static_cast<float>(target.x()) - u.x;
                float dy = static_cast<float>(target.y()) - u.y;
                float dist = std::hypot(dx, dy);
                if (dist < 1.5f) {
                    u.waypoints.erase(u.waypoints.begin());
                } else {
                    routeDirX = dx / dist;
                    routeDirY = dy / dist;
                }
            }

            float avoidX = 0.0f, avoidY = 0.0f;
            if (zombieInsideRoom) {
                for (size_t zIdx : activeZombies) {
                    if (g_units[zIdx].insideBuildingId != u.insideBuildingId)
                        continue;
                    float zx = u.x - g_units[zIdx].x;
                    float zy = u.y - g_units[zIdx].y;
                    float zDist = std::hypot(zx, zy);
                    if (zDist <= 16.0f && zDist > 0.1f) {
                        float weight = (16.0f - zDist) / 16.0f;
                        avoidX += (zx / zDist) * weight * 1.3f;
                        avoidY += (zy / zDist) * weight * 1.3f;
                    }
                }
            }

            float finalX = routeDirX * 1.3f + avoidX;
            float finalY = routeDirY * 1.3f + avoidY;
            float finalLen = std::hypot(finalX, finalY);
            if (finalLen > 0.01f) {
                u.vx = (finalX / finalLen) * RUN_SPEED;
                u.vy = (finalY / finalLen) * RUN_SPEED;
            }
            return;
        }

        // 2b. Зомби ломает дверь снаружи → паникуем внутри, не выходим
        if (b.underAttackTimerSec > 0.0f) {
            u.panicTimerSec = 1.5f;
            u.waypoints.clear();

            float panicIndoorSpeed = RUN_SPEED;
            float curSpd = std::hypot(u.vx, u.vy);
            if (curSpd < 1.0f || rng->bounded(100) < 12) {
                float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                u.vx = std::cos(angle) * panicIndoorSpeed;
                u.vy = std::sin(angle) * panicIndoorSpeed;
            }

            // Не выбегаем случайно в дверной проём
            float lookAheadX = u.x + (u.vx / panicIndoorSpeed) * 2.5f;
            float lookAheadY = u.y + (u.vy / panicIndoorSpeed) * 2.5f;
            int lx = std::clamp(static_cast<int>(lookAheadX), 0, mapW - 1);
            int ly = std::clamp(static_cast<int>(lookAheadY), 0, mapH - 1);
            if (walkGrid[ly * mapW + lx] != 10 + u.insideBuildingId) {
                float toCenter = std::atan2(b.cy - u.y, b.cx - u.x)
                                 + static_cast<float>((rng->generateDouble() - 0.5) * 1.2);
                u.vx = std::cos(toCenter) * panicIndoorSpeed;
                u.vy = std::sin(toCenter) * panicIndoorSpeed;
            }
            return;
        }

        // 2c. Всё спокойно → обычное расписание
        if (u.panicTimerSec > 0.0f)
            u.panicTimerSec = std::max(0.0f, u.panicTimerSec - dt);

        updateCitizenSchedule(u, dt, buildings);
        return;
    }

    // 3. На улице
    bool hasThreat = false;
    float threatX = 0.0f, threatY = 0.0f;

    for (size_t zIdx : activeZombies) {
        if (g_units[zIdx].insideBuildingId != -1)
            continue; // видим только уличных зомби
        float dx = g_units[zIdx].x - u.x;
        float dy = g_units[zIdx].y - u.y;
        if (dx * dx + dy * dy <= 45.0f * 45.0f
            && hasLineOfSight(u.x, u.y, g_units[zIdx].x, g_units[zIdx].y)) {
            hasThreat = true;
            threatX = g_units[zIdx].x;
            threatY = g_units[zIdx].y;
            break;
        }
    }

    if (!hasThreat) {
        int nearbyPanic = 0;
        float avgPx = 0.0f, avgPy = 0.0f;
        for (size_t pIdx : panickingOutdoors) {
            if (pIdx == unitIndex)
                continue;
            float dx = g_units[pIdx].x - u.x;
            float dy = g_units[pIdx].y - u.y;
            if (dx * dx + dy * dy <= 35.0f * 35.0f
                && hasLineOfSight(u.x, u.y, g_units[pIdx].x, g_units[pIdx].y)) {
                nearbyPanic++;
                avgPx += g_units[pIdx].x;
                avgPy += g_units[pIdx].y;
            }
        }
        if (nearbyPanic > 10 || (nearbyPanic >= 5 && rng->bounded(100) < 50)) {
            hasThreat = true;
            threatX = avgPx / nearbyPanic;
            threatY = avgPy / nearbyPanic;
        }
    }

    if (hasThreat) {
        u.panicTimerSec = 3.0f;
        u.waypoints.clear();
        float baseAway = std::atan2(u.y - threatY, u.x - threatX);
        float bestAngle = baseAway;
        for (int attempt = 0; attempt < 5; ++attempt) {
            float testAngle = baseAway + static_cast<float>((rng->generateDouble() - 0.5) * 1.4);
            float testX = u.x + std::cos(testAngle) * 6.0f;
            float testY = u.y + std::sin(testAngle) * 6.0f;
            if (isWalkable(testX, testY)) {
                bestAngle = testAngle;
                break;
            }
        }
        u.vx = std::cos(bestAngle) * RUN_SPEED;
        u.vy = std::sin(bestAngle) * RUN_SPEED;
        return;
    }

    if (u.panicTimerSec > 0.0f) {
        u.panicTimerSec -= dt;
        if (u.panicTimerSec <= 0.0f) {
            u.panicTimerSec = 0.0f;
            u.state = UnitState::Walking;
            u.stateTimerSec = 2.0f;
        }
        return;
    }

    // Спокойно — расписание + редкий случайный поворот
    updateCitizenSchedule(u, dt, buildings);
    if (u.state == UnitState::Walking && rng->bounded(100) < 4) {
        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
        u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
    }
}

void GameWorld::updateZombieBehavior(Unit &u,
                                     float dt,
                                     std::vector<BuildingObject> &buildings,
                                     const std::vector<size_t> &zombieTargets,
                                     bool &buildingsVisualDirty)
{
    // Задачи:
    // 1. Преследовать и атаковать/заражать ближайшую видимую жертву
    // 2. Иначе — waypoints / lastKnown / выход из здания / ломать дверь здания с людьми

    auto *rng = QRandomGenerator::global();
    (void)dt;

    int bestTarget = -1;
    float bestDistSq = 75.0f * 75.0f;

    for (size_t tIdx : zombieTargets) {
        const auto &t = g_units[tIdx];
        if (t.hp <= 0)
            continue;

        bool sameBuilding = (u.insideBuildingId != -1 && u.insideBuildingId == t.insideBuildingId);
        bool canSee = sameBuilding
                      || (u.insideBuildingId == -1 && t.insideBuildingId == -1
                          && hasLineOfSight(u.x, u.y, t.x, t.y));
        if (!canSee)
            continue;

        float dx = t.x - u.x;
        float dy = t.y - u.y;
        float dSq = dx * dx + dy * dy;
        if (dSq < bestDistSq) {
            bestDistSq = dSq;
            bestTarget = static_cast<int>(tIdx);
        }
    }

    // 1. Есть жертва
    if (bestTarget != -1) {
        u.waypoints.clear();
        float tx = g_units[bestTarget].x;
        float ty = g_units[bestTarget].y;
        u.hasLastKnownTarget = true;
        u.lastKnownTargetPos = QPointF(tx, ty);
        float dx = tx - u.x;
        float dy = ty - u.y;
        float d = std::sqrt(bestDistSq);

        if (d <= 3.5f) {
            if (u.attackCooldownSec <= 0.0f) {
                u.attackCooldownSec = 0.5f;
                Unit &victim = g_units[bestTarget];
                if (victim.type == UnitType::Soldier || victim.type == UnitType::Vaccinated) {
                    victim.hp -= 1;
                } else if (victim.type == UnitType::Citizen) {
                    infectUnit(victim, false);
                    u.hasLastKnownTarget = false;
                }
            }
        } else {
            u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
            u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
        }
        return;
    }

    // 2. Waypoints
    if (!u.waypoints.empty()) {
        QPointF target = u.waypoints.front();
        float dx = static_cast<float>(target.x()) - u.x;
        float dy = static_cast<float>(target.y()) - u.y;
        float dist = std::hypot(dx, dy);
        if (dist < 1.5f) {
            u.waypoints.erase(u.waypoints.begin());
        } else {
            u.vx = (dx / dist) * ZOMBIE_CHASE_SPEED;
            u.vy = (dy / dist) * ZOMBIE_CHASE_SPEED;
        }
        return;
    }

    // 3. Last known target
    if (u.hasLastKnownTarget) {
        float dx = static_cast<float>(u.lastKnownTargetPos.x()) - u.x;
        float dy = static_cast<float>(u.lastKnownTargetPos.y()) - u.y;
        float d = std::hypot(dx, dy);
        if (d < 4.0f) {
            u.hasLastKnownTarget = false;
        } else {
            u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
            u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
        }
        return;
    }

    // 4. Внутри здания без жертв — выходим
    if (u.insideBuildingId != -1 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        const auto &b = buildings[u.insideBuildingId];
        for (int p = static_cast<int>(b.pathFromRoad.size()) - 1; p >= 0; --p)
            u.waypoints.push_back(b.pathFromRoad[p]);
        return;
    }

    // 5. На улице — ищем здание с людьми и ломаем дверь
    int bestBldg = -1;
    float bestBldgDistSq = 75.0f * 75.0f;
    for (const auto &b : buildings) {
        if (b.currentOccupants > 0 && b.health > 0 && !b.isBurning && !b.isBurnedOut) {
            QPointF targetPt = !b.pathFromRoad.empty() ? b.doorCenter : QPointF(b.cx, b.cy);
            float bx = static_cast<float>(targetPt.x());
            float by = static_cast<float>(targetPt.y());
            float dx = bx - u.x;
            float dy = by - u.y;
            float dSq = dx * dx + dy * dy;
            if (dSq < bestBldgDistSq && hasLineOfSight(u.x, u.y, bx, by)) {
                bestBldgDistSq = dSq;
                bestBldg = b.id;
            }
        }
    }

    if (bestBldg != -1) {
        auto &b = buildings[bestBldg];
        QPointF targetPt = !b.pathFromRoad.empty() ? b.doorCenter : QPointF(b.cx, b.cy);
        float bx = static_cast<float>(targetPt.x());
        float by = static_cast<float>(targetPt.y());
        float dx = bx - u.x;
        float dy = by - u.y;
        float d = std::sqrt(bestBldgDistSq);

        if (d <= 5.0f) {
            // Ломаем дверь, внутрь не заходим
            u.vx = 0.0f;
            u.vy = 0.0f;
            if (u.attackCooldownSec <= 0.0f) {
                u.attackCooldownSec = 0.5f;
                b.health = std::max(0, b.health - 1);
                b.underAttackTimerSec = 3.0f;
                spawnBurstParticles(bx, by, 3, 255, 160, 60);

                if (b.health == 0 && !b.isBurning && !b.isBurnedOut) {
                    b.isBurning = true;
                    b.burnTimerSec = b.area / 15.0f;
                    buildingsVisualDirty = true;
                    rebuildWorkplaceLabels();

                    for (auto &occupant : g_units) {
                        if (occupant.insideBuildingId == b.id) {
                            occupant.panicTimerSec = 3.0f;
                            occupant.state = UnitState::Walking;
                            occupant.stateTimerSec = 8.0f;
                            occupant.waypoints.clear();
                            for (int pIdx = static_cast<int>(b.pathFromRoad.size()) - 1; pIdx >= 0; --pIdx)
                                occupant.waypoints.push_back(b.pathFromRoad[pIdx]);
                        }
                    }
                }
            }
        } else {
            u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
            u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
        }
        return;
    }

    // 6. Бродяжничество
    if (rng->bounded(100) < 4) {
        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        u.vx = std::cos(angle) * ZOMBIE_WANDER_SPEED;
        u.vy = std::sin(angle) * ZOMBIE_WANDER_SPEED;
    }
}

void GameWorld::stepUnitMovement(Unit &u,
                                 float dt,
                                 std::vector<BuildingObject> &buildings)
{
    // Задачи:
    // 1. Урон/избегание огня
    // 2. Отталкивание от бордюров
    // 3. Замедление внутри зданий
    // 4. Шаг перемещения со скольжением вдоль стен
    // 5. Анти-застревание при движении к зданию

    auto *rng = QRandomGenerator::global();
    const auto &walkGrid = g_generator.walkGrid();
    const int mapW = g_generator.width();
    const int mapH = g_generator.height();

    // 1–2
    applyFireAvoidanceAndDamage(u, dt, buildings);
    applyWallRepulsion(u, dt);

    // 3. Замедление внутри здания
    if (u.insideBuildingId != -1) {
        if (u.state == UnitState::AtHome || u.state == UnitState::Working) {
            float speedFactor = g_generator.level.speedInBuildingPercent / 100.0f;
            u.vx *= speedFactor;
            u.vy *= speedFactor;
        }
    }

    // 4. Шаг со скольжением
    float nextX = u.x + u.vx * dt;
    float nextY = u.y + u.vy * dt;
    bool movedX = false;
    bool movedY = false;

    if (isWalkable(nextX, u.y)) {
        u.x = nextX;
        movedX = true;
    }
    if (isWalkable(u.x, nextY)) {
        u.y = nextY;
        movedY = true;
    }

    if (!movedX && std::abs(u.vx) > 0.1f) {
        float slideStep = (u.vy >= 0.0f) ? 1.5f : -1.5f;
        if (isWalkable(nextX, u.y + slideStep)) {
            u.x = nextX;
            u.y += slideStep;
            movedX = true;
        } else if (isWalkable(nextX, u.y - slideStep)) {
            u.x = nextX;
            u.y -= slideStep;
            movedX = true;
        }
    }

    if (!movedY && std::abs(u.vy) > 0.1f) {
        float slideStep = (u.vx >= 0.0f) ? roadWidth() * 0.12f : -roadWidth() * 0.12f;
        if (isWalkable(u.x + slideStep, nextY)) {
            u.x += slideStep;
            u.y = nextY;
            movedY = true;
        } else if (isWalkable(u.x - slideStep, nextY)) {
            u.x -= slideStep;
            u.y = nextY;
            movedY = true;
        }
    }

    if (!movedX && !movedY) {
        float escapeAngle = static_cast<float>(rng->generateDouble() * 6.28318);
        float currentSpeed = std::hypot(u.vx, u.vy);
        if (currentSpeed < 5.0f)
            currentSpeed = BASE_CITIZEN_SPEED;
        u.vx = std::cos(escapeAngle) * currentSpeed;
        u.vy = std::sin(escapeAngle) * currentSpeed;
    }

    // 5. Анти-застревание на пути к зданию
    if ((u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome)
        && u.insideBuildingId == -1) {
        int gx = std::clamp(static_cast<int>(u.x), 0, mapW - 1);
        int gy = std::clamp(static_cast<int>(u.y), 0, mapH - 1);
        if (walkGrid[gy * mapW + gx] == 1) {
            u.stuckTimerSec += dt;
            if (u.stuckTimerSec >= 1.5f) {
                float moved = static_cast<float>(
                    std::hypot(u.x - u.lastCheckPos.x(), u.y - u.lastCheckPos.y()));
                u.lastCheckPos = QPointF(u.x, u.y);
                u.stuckTimerSec = 0.0f;
                if (moved < roadWidth() * 0.25f && u.targetBuildingId >= 0
                    && u.targetBuildingId < static_cast<int>(buildings.size())) {
                    u.waypoints = buildPathToBuilding(QPointF(u.x, u.y),
                                                      buildings[u.targetBuildingId]);
                }
            }
        }
    }
}

void GameWorld::rebuildUnitRenderData()
{
    g_unitGeometry.clear();
    g_unitGeometry.reserve(g_units.size());
    for (const auto &u : g_units) {
        uint8_t r = 240, g = 240, b = 240; // Белый — обычный спокойный житель
        float size = 2.0f;
        if (u.type == UnitType::Vaccinated) {
            r = 80;
            g = 180;
            b = 255; // Синий — вакцинированный
        } else if (u.type == UnitType::Zombie) {
            r = 70;
            g = 230;
            b = 120; // Зелёный — зомби
            size = 2.4f;
        } else if (u.type == UnitType::Soldier) {
            g_unitGeometry
                .push_back({u.x, u.y, 4.4f, 4.4f, 0.0f, 255, 255, 255, 255}); // Белая окантовка
            r = 65;
            g = 145;
            b = 255; // Ярко-синий солдат
            size = 3.0f;
        } else if (u.isInfected) {
            r = 255;
            g = 175;
            b = 45; // Жёлто-оранжевый — скрыто инфицированный
        } else if (u.panicTimerSec > 0.0f || u.state == UnitState::PanicEvacuating) {
            r = 255;
            g = 115;
            b = 115; // Красноватый — в панике
        }
        g_unitGeometry.push_back({u.x, u.y, size, size, 0.0f, r, g, b, 255});
    }
    // Отрисовка эвакуированных пассажиров в конвое (круг -> овал при движении)
    for (const auto &ev : g_evacs) {
        float dx = static_cast<float>(ev.convoyEnd.x() - ev.convoyStart.x());
        float dy = static_cast<float>(ev.convoyEnd.y() - ev.convoyStart.y());
        float angle = std::atan2(dy, dx);
        float cosA = std::cos(angle);
        float sinA = std::sin(angle);

        float stretchAlong = ev.completed ? 1.85f : 1.0f; // Растяжение в овал при движении
        float squashAcross = ev.completed ? 0.65f : 1.0f;

        for (const auto &off : ev.passengerOffsets) {
            float lx = static_cast<float>(off.x()) * stretchAlong;
            float ly = static_cast<float>(off.y()) * squashAcross;

            float wx = static_cast<float>(ev.convoyPos.x()) + (lx * cosA - ly * sinA);
            float wy = static_cast<float>(ev.convoyPos.y()) + (lx * sinA + ly * cosA);

            g_unitGeometry.push_back({wx, wy, 2.0f, 2.0f, 0.0f, 240, 245, 255, 255});
        }
    }
}

void GameWorld::selectBuildingAt(float worldX, float worldY)
{
    const auto &buildings = g_generator.buildings();
    int foundId = -1;

    for (const auto &b : buildings) {
        // Смещение клика относительно центра здания
        float dx = worldX - b.cx;
        float dy = worldY - b.cy;

        // Обратный поворот в локальные координаты здания
        float cosA = std::cos(-b.angleRad);
        float sinA = std::sin(-b.angleRad);
        float localX = dx * cosA - dy * sinA;
        float localY = dx * sinA + dy * cosA;

        // Проверка попадания в границы с небольшим запасом (+2м на рамку)
        float hw = b.width * 0.5F + 2.0F;
        float hh = b.height * 0.5F + 2.0F;

        if (std::abs(localX) <= hw && std::abs(localY) <= hh) {
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

    // Вычисляем верхнюю границу здания по оси Y для позиционирования плашки над ним
    float halfExtentY = (std::abs(b.width * std::sin(b.angleRad))
                         + std::abs(b.height * std::cos(b.angleRad)))
                        * 0.5F;

    info["valid"] = true;
    info["type"] = typeName;
    info["occupants"] = b.currentOccupants;
    info["capacity"] = b.capacity;
    info["health"] = b.health;
    info["maxHealth"] = b.maxHealth;
    info["cx"] = b.cx;
    info["topY"] = b.cy - halfExtentY;

    g_selectedBuildingInfo = info;
}
/*Свойство для QML*/ void GameWorld::setEquippedTools(const QVariantList &tools)
{
    if (g_equippedTools != tools) {
        g_equippedTools = tools;
        // Если текущий выбранный инструмент убрали из слотов — сбрасываем выбор
        if (!g_equippedTools.contains(g_selectedTool)) {
            setSelectedTool(0);
        }
        emit equippedToolsChanged();
    }
}

/*Вызывается из QML*/ void GameWorld::useToolAt(float worldX, float worldY)
{
    if (!isCityArea(worldX, worldY))
        return;
    if (g_selectedTool == 1 && g_budget >= 90) {
        g_budget -= 90;
        for (auto &u : g_units) {
            float dx = u.x - worldX;
            float dy = u.y - worldY;
            if (dx * dx + dy * dy <= 45.0f * 45.0f) {
                if (u.type == UnitType::Zombie || u.type == UnitType::Citizen) {
                    u.type = UnitType::Vaccinated;
                    u.isInfected = false;
                    u.hp = 2;
                }
            }
        }
        emit pulseWaveTriggered(worldX, worldY, 45.0f, "#4299e1");
        spawnBurstParticles(worldX, worldY, 16, 120, 220, 255);

        rebuildUnitRenderData();
        emit unitsUpdated();
        emit statsChanged();
    } else if (g_selectedTool == 2 && g_budget >= 200) {
        QPointF dropPos = findNearestWalkable(worldX, worldY, 40.0f);
        if (dropPos.x() < 0.0f)
            return;
        g_budget -= 200;
        callDropHelicopter(static_cast<float>(dropPos.x()), static_cast<float>(dropPos.y()));
        emit statsChanged();
    } else if (g_selectedTool == 3 && g_budget >= 75) {
        QVariantMap snap = snapEvacPoint(worldX, worldY, 20.0f);
        if (!snap["snapped"].toBool())
            return;

        int nodeId = snap["nodeId"].toInt();
        QPointF evacPos(snap["x"].toDouble(), snap["y"].toDouble());

        // Не запускаем повторно на той же точке, пока идёт сбор
        for (const auto &ev : g_evacs) {
            if (ev.evacNodeId == nodeId && !ev.completed)
                return;
        }

        // Ищем ближайшую стартовую точку маршрута из группы 1 (#10FFFF) или группы 2 (#1077FF)
        const auto &routes = g_generator.evacRoutes();
        if (routes.empty())
            return;

        const EvacRoute *bestRoute = &routes[0];
        float bestRouteDist = 1e9f;
        for (const auto &r : routes) {
            float d = static_cast<float>(
                std::hypot(r.startPos.x() - evacPos.x(), r.startPos.y() - evacPos.y()));
            if (d < bestRouteDist) {
                bestRouteDist = d;
                bestRoute = &r;
            }
        }

        g_budget -= 75;

        ActiveEvacuation ev;
        ev.evacNodeId = nodeId;
        ev.evacPos = evacPos;
        ev.timeLeftSec = 30.0f;
        ev.boardedCount = 0;
        ev.completed = false;
        ev.convoyStart = bestRoute->startPos;
        ev.convoyEnd = bestRoute->endPos;
        ev.convoyPos = bestRoute->startPos;
        g_evacs.push_back(ev);

        // Оповещаем всех жителей в круговом радиусе 100 метров (выводим из зданий и направляем к выходу)
        auto &buildings = const_cast<std::vector<BuildingObject> &>(g_generator.buildings());
        for (auto &u : g_units) {
            if (u.type != UnitType::Citizen && u.type != UnitType::Vaccinated)
                continue;

            float ux = u.x, uy = u.y;
            if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
                ux = buildings[u.insideBuildingId].cx;
                uy = buildings[u.insideBuildingId].cy;
            }

            float dx = ux - static_cast<float>(evacPos.x());
            float dy = uy - static_cast<float>(evacPos.y());
            if (dx * dx + dy * dy <= 250.0f * 250.0f) {
                u.panicTimerSec = 0.0f; // Целенаправленно бегут на эвакуацию
                u.state = UnitState::Evacuating;
                u.targetBuildingId = nodeId; // Храним ID точки эвакуации
                u.waypoints = buildPathBetween(QPointF(u.x, u.y), evacPos);
            }
        }
        emit pulseWaveTriggered(static_cast<float>(evacPos.x()),
                                static_cast<float>(evacPos.y()),
                                100.0f,
                                "#38b2ac");

        rebuildActiveEvacuationsList();
        emit evacuationsChanged();
        emit statsChanged();
    }
}
/*Вызывается из QML*/ void GameWorld::regenerateMap()
{
    startLevel(g_currentConfig);
}

/*Свойство для QML*/ void GameWorld::setSelectedTool(int tool)
{
    if (g_selectedTool != tool) {
        g_selectedTool = tool;
        emit stateChanged();
    }
}

/*Вызывается из QML*/ void GameWorld::togglePause()
{
    g_paused = !g_paused;
    emit stateChanged();
}

/*Вызывается из QML*/ void GameWorld::toggleSpeed()
{
    g_speedMultiplier = (g_speedMultiplier == 1) ? 2 : 1;
    emit stateChanged();
}

/*Свойство для QML*/ QString GameWorld::formattedTime() const
{
    int totalMinutes = static_cast<int>(g_timeOfDayHours * 60.0f);
    int hh = (totalMinutes / 60) % 24;
    int mm = totalMinutes % 60;
    return QString("%1:%2").arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'));
}

/*Свойство для QML*/ bool GameWorld::isNight() const
{
    return (g_timeOfDayHours >= 21.0f || g_timeOfDayHours < 5.5f);
}

/*Свойство для QML*/ float GameWorld::nightDarkness() const
{
    const float maxOp = 0.40f;
    if (g_timeOfDayHours >= 21.5f || g_timeOfDayHours < 4.5f)
        return maxOp;
    if (g_timeOfDayHours >= 19.0f && g_timeOfDayHours < 21.5f)
        return ((g_timeOfDayHours - 19.0f) / 2.5f) * maxOp;
    if (g_timeOfDayHours >= 4.5f && g_timeOfDayHours < 7.0f)
        return (1.0f - (g_timeOfDayHours - 4.5f) / 2.5f) * maxOp;
    return 0.0f;
}

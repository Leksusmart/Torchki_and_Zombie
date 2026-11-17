#include "GameWorld.h"
#include <QDebug>
#include <QRandomGenerator>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <queue>

GameWorld::GameWorld(QObject *parent)
    : QObject(parent)
{
    LevelConfig defaultCfg;
    defaultCfg.mapPngPath = ":/Torchki_and_Zombie/levels/Level1.png";
    defaultCfg.initialCitizens = 2000;
    defaultCfg.startHour = 7.5f;
    defaultCfg.gameHoursPerSec = 0.25f;
    defaultCfg.outbreaks = {{4.0f, 1, false}, {30.0f, 1, false}};

    startLevel(defaultCfg);

    connect(&m_timer, &QTimer::timeout, this, &GameWorld::tick);
    m_timer.start(16);
}

QVariantMap GameWorld::snapEvacPoint(float worldX, float worldY, float snapRadius) const
{
    QVariantMap res;
    res["snapped"] = false;
    res["x"] = worldX;
    res["y"] = worldY;

    float bestDist = snapRadius;
    for (const auto &node : m_generator.nodes()) {
        if (!node.isEvacExit)
            continue;
        float d = static_cast<float>(std::hypot(node.pos.x() - worldX, node.pos.y() - worldY));
        if (d <= bestDist) {
            bestDist = d;
            res["snapped"] = true;
            res["x"] = node.pos.x();
            res["y"] = node.pos.y();
            res["nodeId"] = node.id;
            res["name"] = node.evacName;
        }
    }
    return res;
}

void GameWorld::startLevel(const LevelConfig &config)
{
    m_currentConfig = config;
    for (auto &ob : m_currentConfig.outbreaks) {
        ob.triggered = false;
    }

    m_budget = 300.0;
    m_eliminatedCount = 0;
    m_elapsedSimSec = 0.0f;
    m_timeOfDayHours = m_currentConfig.startHour;
    m_dayNumber = 1;
    m_lastTrackedHour = static_cast<int>(m_timeOfDayHours) % 24;

    m_generator.generateFromLevel(m_currentLevel);
    m_evacs.clear();
    rebuildActiveEvacuationsList();

    buildRoadGraphCache();
    buildBuildingsGeometry();
    spawnInitialUnits(m_currentConfig.initialCitizens);
    updateHourlyTaxes(m_lastTrackedHour, true);
    rebuildUnitRenderData();
    rebuildWorkplaceLabels();
    rebuildEvacPoints();

    emit evacuationsChanged();
    emit mapUpdated();
    emit unitsUpdated();
    emit statsChanged();
    emit timeChanged();
}

void GameWorld::spawnBulletTracer(float x1, float y1, float x2, float y2)
{
    EffectParticle p;
    p.x = x1;
    p.y = y1;
    p.x2 = x2;
    p.y2 = y2;
    p.size = 1.1f; // Толщина трассера
    p.lifeSec = 0.09f;
    p.maxLifeSec = 0.09f;
    p.r = 255;
    p.g = 240;
    p.b = 140;
    p.a = 240;
    p.isLine = true;
    m_particles.push_back(p);

    // Искры попадания в точке зомби
    spawnBurstParticles(x2, y2, 3, 255, 180, 60);
}

void GameWorld::spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b)
{
    EffectParticle p;
    p.x = x;
    p.y = y;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.size = 4.0f;
    p.endSize = maxRadius * 2.0f;
    p.lifeSec = 0.45f;
    p.maxLifeSec = 0.45f;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 140;
    p.isLine = false;
    m_particles.push_back(p);
}

void GameWorld::spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b)
{
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < count; ++i) {
        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        float speed = 15.0f + static_cast<float>(rng->generateDouble() * 25.0);
        EffectParticle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.size = 2.0f;
        p.endSize = 0.5f;
        p.lifeSec = 0.25f + static_cast<float>(rng->generateDouble() * 0.2);
        p.maxLifeSec = p.lifeSec;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a = 255;
        p.isLine = false;
        m_particles.push_back(p);
    }
}

void GameWorld::updateEffects(float dt)
{
    m_effectsGeometry.clear();

    rebuildBurningLights();

    // Обновляем пули, волны и искры
    for (size_t i = 0; i < m_particles.size();) {
        auto &p = m_particles[i];
        p.lifeSec -= dt;
        if (p.lifeSec <= 0.0f) {
            m_particles[i] = m_particles.back();
            m_particles.pop_back();
            continue;
        }

        float t = 1.0f - (p.lifeSec / p.maxLifeSec);
        uint8_t alpha = static_cast<uint8_t>(p.a * (1.0f - t));

        if (p.isLine) {
            float dx = p.x2 - p.x;
            float dy = p.y2 - p.y;
            float len = std::hypot(dx, dy);
            float angle = std::atan2(dy, dx);
            m_effectsGeometry.push_back(
                {(p.x + p.x2) * 0.5f, (p.y + p.y2) * 0.5f, len, p.size, angle, p.r, p.g, p.b, alpha});
        } else {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            float curSize = p.size + (p.endSize - p.size) * t;
            m_effectsGeometry.push_back({p.x, p.y, curSize, curSize, 0.0f, p.r, p.g, p.b, alpha});
        }
        ++i;
    }
}

void GameWorld::startCustomLevel(int citizens, const QVariantList &outbreakDelaysSec)
{
    LevelConfig cfg = m_currentConfig;
    cfg.initialCitizens = std::max(0, citizens);
    cfg.outbreaks.clear();

    for (const QVariant &v : outbreakDelaysSec) {
        cfg.outbreaks.push_back({v.toFloat(), 1, false});
    }
    startLevel(cfg);
}

void GameWorld::regenerateMap()
{
    startLevel(m_currentConfig);
}

void GameWorld::setSelectedTool(int tool)
{
    if (m_selectedTool != tool) {
        m_selectedTool = tool;
        emit stateChanged();
    }
}

void GameWorld::togglePause()
{
    m_paused = !m_paused;
    emit stateChanged();
}

void GameWorld::toggleSpeed()
{
    m_speedMultiplier = (m_speedMultiplier == 1) ? 2 : 1;
    emit stateChanged();
}

QString GameWorld::formattedTime() const
{
    int totalMinutes = static_cast<int>(m_timeOfDayHours * 60.0f);
    int hh = (totalMinutes / 60) % 24;
    int mm = totalMinutes % 60;
    return QString("%1:%2").arg(hh, 2, 10, QChar('0')).arg(mm, 2, 10, QChar('0'));
}

bool GameWorld::isNight() const
{
    return (m_timeOfDayHours >= 21.0f || m_timeOfDayHours < 5.5f);
}

float GameWorld::nightDarkness() const
{
    const float maxOp = 0.40f;
    if (m_timeOfDayHours >= 21.5f || m_timeOfDayHours < 4.5f)
        return maxOp;
    if (m_timeOfDayHours >= 19.0f && m_timeOfDayHours < 21.5f)
        return ((m_timeOfDayHours - 19.0f) / 2.5f) * maxOp;
    if (m_timeOfDayHours >= 4.5f && m_timeOfDayHours < 7.0f)
        return (1.0f - (m_timeOfDayHours - 4.5f) / 2.5f) * maxOp;
    return 0.0f;
}

void GameWorld::buildRoadGraphCache()
{
    const auto &nodes = m_generator.nodes();
    const auto &edges = m_generator.edges();
    m_roadAdj.assign(nodes.size(), {});

    for (const auto &e : edges) {
        float dist = static_cast<float>(std::hypot(nodes[e.fromId].pos.x() - nodes[e.toId].pos.x(),
                                                   nodes[e.fromId].pos.y() - nodes[e.toId].pos.y()));
        m_roadAdj[e.fromId].push_back({e.toId, dist});
        m_roadAdj[e.toId].push_back({e.fromId, dist});
    }
}

int GameWorld::findNearestRoadNode(float x, float y) const
{
    const auto &nodes = m_generator.nodes();
    int bestVisibleId = -1;
    float bestVisibleDist = 1e9f;

    int fallbackId = -1;
    float fallbackDist = 1e9f;

    for (size_t i = 0; i < nodes.size(); ++i) {
        float nx = static_cast<float>(nodes[i].pos.x());
        float ny = static_cast<float>(nodes[i].pos.y());
        float d = std::hypot(nx - x, ny - y);

        if (d < fallbackDist) {
            fallbackDist = d;
            fallbackId = static_cast<int>(i);
        }

        if (d < bestVisibleDist && hasLineOfSight(x, y, nx, ny)) {
            bestVisibleDist = d;
            bestVisibleId = static_cast<int>(i);
        }
    }
    return (bestVisibleId != -1) ? bestVisibleId : fallbackId;
}

std::vector<QPointF> GameWorld::buildPathBetween(QPointF startPos, QPointF endPos) const
{
    std::vector<QPointF> path;
    const auto &nodes = m_generator.nodes();

    float sx = static_cast<float>(startPos.x());
    float sy = static_cast<float>(startPos.y());
    float ex = static_cast<float>(endPos.x());
    float ey = static_cast<float>(endPos.y());

    // 1. Прямой путь разрешён ТОЛЬКО если дверь близко И видна без единого препятствия
    if (std::hypot(ex - sx, ey - sy) < 45.0f && hasLineOfSight(sx, sy, ex, ey)) {
        path.push_back(endPos);
        return path;
    }

    // 2. Ищем ближайший узел графа, который РЕАЛЬНО ВИДЕН с текущей точки (без стен и островков)
    int startNode = -1;
    float bestStartDist = 1e9f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        float nx = static_cast<float>(nodes[i].pos.x());
        float ny = static_cast<float>(nodes[i].pos.y());
        float d = std::hypot(nx - sx, ny - sy);
        if (d < bestStartDist && hasLineOfSight(sx, sy, nx, ny)) {
            bestStartDist = d;
            startNode = static_cast<int>(i);
        }
    }
    if (startNode == -1) {
        startNode = findNearestRoadNode(sx, sy);
    }

    // 3. Ищем ближайший к двери узел графа, из которого дверь ВИДНА напрямую без препятствий
    int endNode = -1;
    float bestEndDist = 1e9f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        float nx = static_cast<float>(nodes[i].pos.x());
        float ny = static_cast<float>(nodes[i].pos.y());
        float d = std::hypot(nx - ex, ny - ey);
        if (d < bestEndDist && hasLineOfSight(nx, ny, ex, ey)) {
            bestEndDist = d;
            endNode = static_cast<int>(i);
        }
    }
    if (endNode == -1) {
        endNode = findNearestRoadNode(ex, ey);
    }

    if (startNode == -1 || endNode == -1) {
        path.push_back(endPos);
        return path;
    }

    if (startNode == endNode) {
        path.push_back(nodes[startNode].pos);
        path.push_back(endPos);
        return path;
    }

    // 4. Поиск кратчайшего пути по рёбрам графа (алгоритм Дейкстры)
    const int n = static_cast<int>(nodes.size());
    std::vector<float> dist(n, 1e9f);
    std::vector<int> prev(n, -1);

    using PQItem = std::pair<float, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    dist[startNode] = 0.0f;
    pq.push({0.0f, startNode});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (u == endNode)
            break;
        if (d > dist[u])
            continue;

        for (const auto &[v, weight] : m_roadAdj[u]) {
            if (dist[u] + weight < dist[v]) {
                dist[v] = dist[u] + weight;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    std::vector<int> nodeChain;
    for (int curr = endNode; curr != -1; curr = prev[curr]) {
        nodeChain.push_back(curr);
    }
    std::reverse(nodeChain.begin(), nodeChain.end());

    size_t startIdx = 0;
    if (!nodeChain.empty()) {
        float dToFirst = static_cast<float>(
            std::hypot(nodes[nodeChain[0]].pos.x() - sx, nodes[nodeChain[0]].pos.y() - sy));
        if (dToFirst < 3.0f && nodeChain.size() > 1) {
            startIdx = 1;
        }
    }

    float rw = roadWidth();

    for (size_t i = startIdx; i < nodeChain.size(); ++i) {
        QPointF pt = nodes[nodeChain[i]].pos;

        // Вычисляем направление сегмента дороги для смещения по полосе (в пределах 75% ширины)
        QPointF prevPt = (i > 0) ? nodes[nodeChain[i - 1]].pos : startPos;
        QPointF nextPt = (i + 1 < nodeChain.size()) ? nodes[nodeChain[i + 1]].pos : endPos;
        float dirX = static_cast<float>(nextPt.x() - prevPt.x());
        float dirY = static_cast<float>(nextPt.y() - prevPt.y());
        float len = std::hypot(dirX, dirY);

        if (len > 0.01f) {
            // Перпендикуляр к дороге
            float perpX = -dirY / len;
            float perpY = dirX / len;
            // Применяем смещение только если сдвинутая точка остаётся на асфальте
            // (передаём laneOffset как параметр или сдвигаем в updateCitizenSchedule)
        }
        path.push_back(pt);
    }
    path.push_back(endPos);

    return path;
}
void GameWorld::rebuildEvacPoints()
{
    QVariantList list;
    for (const auto &node : m_generator.nodes()) {
        if (node.isEvacExit) {
            QVariantMap item;
            item["id"] = node.id;
            item["x"] = node.pos.x();
            item["y"] = node.pos.y();
            item["name"] = node.evacName;
            item["evacuatedCount"] = 0; // Задел для будущей статистики эвакуации
            list.append(item);
        }
    }
    m_evacPoints = list;
}

int GameWorld::findNearestAvailableBuilding(float x, float y, bool wantWorkplace) const
{
    const auto &buildings = m_generator.buildings();
    int bestId = -1;
    float bestDist = 1e9f;

    for (const auto &b : buildings) {
        bool isWork = (b.type == BuildingType::Office || b.type == BuildingType::Factory);
        if (wantWorkplace != isWork)
            continue;
        if (b.currentOccupants >= b.capacity)
            continue;
        if (b.health <= 0 || b.isBurning || b.isBurnedOut)
            continue;
        if (b.underAttackTimerSec
            > 0.0f) // Пока здание атакуют зомби (и 3 сек после), туда не заходим
            continue;

        float d = static_cast<float>(std::hypot(b.entrancePos.x() - x, b.entrancePos.y() - y));
        if (d < bestDist) {
            bestDist = d;
            bestId = b.id;
        }
    }
    return bestId;
}

void GameWorld::exitBuilding(Unit &u, std::vector<BuildingObject> &buildings)
{
    if (u.insideBuildingId >= 0 && u.insideBuildingId < static_cast<int>(buildings.size())) {
        auto &b = buildings[u.insideBuildingId];
        b.currentOccupants = std::max(0, b.currentOccupants - 1);
        u.x = static_cast<float>(b.entrancePos.x());
        u.y = static_cast<float>(b.entrancePos.y());
    }
    u.insideBuildingId = -1;
}

void GameWorld::enterBuilding(Unit &u, BuildingObject &b)
{
    u.insideBuildingId = b.id;
    u.waypoints.clear();
    b.currentOccupants++;

    if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
        int h = static_cast<int>(m_timeOfDayHours) % 24;
        if (b.currentOccupants > b.hourlyMaxWorkers[h]) {
            b.hourlyMaxWorkers[h] = b.currentOccupants;
        }
    }
}

void GameWorld::infectUnit(Unit &u, bool immediateZombie)
{
    if (u.type == UnitType::Zombie || u.type == UnitType::Vaccinated || u.type == UnitType::Soldier)
        return;

    if (immediateZombie) {
        u.type = UnitType::Zombie;
        u.hp = 5; // Здоровье зомби = 5 единиц
        u.isInfected = false;
        u.panicTimerSec = 0.0f;
        u.hasLastKnownTarget = false;
        u.waypoints.clear();
    } else if (!u.isInfected) {
        auto *rng = QRandomGenerator::global();
        u.isInfected = true;
        u.infectionHoursLeft = 3.0f + static_cast<float>(rng->generateDouble() * 7.0);
    }
}

void GameWorld::updateEvacuations(float dt)
{
    bool listDirty = false;

    for (size_t i = 0; i < m_evacs.size();) {
        auto &ev = m_evacs[i];

        if (!ev.completed) {
            ev.timeLeftSec -= dt;
            listDirty = true;
            if (ev.timeLeftSec <= 0.0f) {
                ev.timeLeftSec = 0.0f;
                ev.completed = true;
                if (ev.passengerOffsets.empty()) {
                    // Никто не успел сесть — закрываем операцию
                    m_evacs.erase(m_evacs.begin() + i);
                    continue;
                }
            }
            ++i;
        } else {
            // Эвакуация выполнена: конвой движется к конечной точке маршрута
            float dx = static_cast<float>(ev.convoyEnd.x() - ev.convoyPos.x());
            float dy = static_cast<float>(ev.convoyEnd.y() - ev.convoyPos.y());
            float dist = std::hypot(dx, dy);

            if (dist < 4.0f) {
                // Конвой достиг финишной точки (#A0FFFF / #5077FF) и исчезает
                m_evacs.erase(m_evacs.begin() + i);
                listDirty = true;
            } else {
                float convoySpeed = 32.0f;
                ev.convoyPos.rx() += (dx / dist) * convoySpeed * dt;
                ev.convoyPos.ry() += (dy / dist) * convoySpeed * dt;
                ++i;
            }
        }
    }

    if (listDirty) {
        rebuildActiveEvacuationsList();
        emit evacuationsChanged();
    }
}

void GameWorld::rebuildActiveEvacuationsList()
{
    QVariantList list;
    for (const auto &ev : m_evacs) {
        if (ev.completed)
            continue; // После завершения круг 100м и таймер убираются
        QVariantMap m;
        m["x"] = ev.evacPos.x();
        m["y"] = ev.evacPos.y();
        m["timeLeft"] = static_cast<int>(std::ceil(ev.timeLeftSec));
        m["boarded"] = ev.boardedCount;
        list.append(m);
    }
    m_activeEvacuationsList = list;
}

void GameWorld::updateCitizenSchedule(Unit &u, float dt, std::vector<BuildingObject> &buildings)
{
    auto *rng = QRandomGenerator::global();

    // Во время паники распорядок дня отключается (управляется бегством)
    if (u.panicTimerSec > 0.0f && u.state != UnitState::PanicEvacuating)
        return;

    bool shouldBeAtWork = false;
    if (!u.isNightShift) {
        shouldBeAtWork = (m_timeOfDayHours >= 8.0f && m_timeOfDayHours < 18.0f);
    } else {
        shouldBeAtWork = (m_timeOfDayHours >= 20.0f || m_timeOfDayHours < 6.0f);
    }

    if (u.state == UnitState::Evacuating || u.state == UnitState::PanicEvacuating) {
        ActiveEvacuation *targetEvac = nullptr;
        for (auto &ev : m_evacs) {
            if (ev.evacNodeId == u.targetBuildingId) {
                targetEvac = &ev;
                break;
            }
        }

        if (!targetEvac) {
            u.state = UnitState::Walking;
            u.stateTimerSec = 3.0f;
            return;
        }

        // Базовый вектор движения по маршруту к точке эвакуации
        float rw = roadWidth();
        float routeDirX = 0.0f, routeDirY = 0.0f;

        if (!u.waypoints.empty()) {
            QPointF baseTarget = u.waypoints.front();
            float dx = static_cast<float>(baseTarget.x()) - u.x;
            float dy = static_cast<float>(baseTarget.y()) - u.y;
            float dist = std::hypot(dx, dy);

            if (dist < rw * 0.35f) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                routeDirX = dx / dist;
                routeDirY = dy / dist;
            }
        } else {
            // Если waypoints опустели, но до точки ещё > 5м — тянем напрямую к выходу
            float dx = static_cast<float>(targetEvac->evacPos.x()) - u.x;
            float dy = static_cast<float>(targetEvac->evacPos.y()) - u.y;
            float dist = std::hypot(dx, dy);
            if (dist > 0.1f) {
                routeDirX = dx / dist;
                routeDirY = dy / dist;
            }
        }

        // Если режим PanicEvacuating — корректируем курс от ближайшего видимого зомби в сторону выхода!
        if (u.state == UnitState::PanicEvacuating) {
            float avoidX = 0.0f, avoidY = 0.0f;
            bool closeZombie = false;

            for (const auto &other : m_units) {
                if (other.type != UnitType::Zombie || other.insideBuildingId != -1)
                    continue;
                float zx = u.x - other.x;
                float zy = u.y - other.y;
                float zDist = std::hypot(zx, zy);

                if (zDist <= 45.0f && zDist > 0.1f && hasLineOfSight(u.x, u.y, other.x, other.y)) {
                    // Чем ближе зомби, тем сильнее отталкивание от него
                    float weight = (45.0f - zDist) / 45.0f;
                    avoidX += (zx / zDist) * weight * 1.6f;
                    avoidY += (zy / zDist) * weight * 1.6f;
                    if (zDist < 14.0f) {
                        closeZombie = true;
                    }
                }
            }

            // Складываем вектор к выходу и вектор уклонения от зомби
            float finalX = routeDirX * (closeZombie ? 0.6f : 1.2f) + avoidX;
            float finalY = routeDirY * (closeZombie ? 0.6f : 1.2f) + avoidY;
            float finalLen = std::hypot(finalX, finalY);

            if (finalLen > 0.01f) {
                u.vx = (finalX / finalLen) * RUN_SPEED;
                u.vy = (finalY / finalLen) * RUN_SPEED;
            }
        } else {
            // Спокойная эвакуация: бежим строго по маршруту
            if (std::hypot(routeDirX, routeDirY) > 0.01f) {
                u.vx = routeDirX * RUN_SPEED;
                u.vy = routeDirY * RUN_SPEED;
            }
        }

        // Проверяем достижение радиуса 5 метров от точки эвакуации
        float distToExit = static_cast<float>(
            std::hypot(u.x - targetEvac->evacPos.x(), u.y - targetEvac->evacPos.y()));
        if (distToExit <= 5.0f) {
            if (!targetEvac->completed && !u.isInfected
                && targetEvac->boardedCount < ActiveEvacuation::MAX_CAPACITY) {
                targetEvac->boardedCount++;
                m_evacuatedTotalCount++;
                m_budget += 3.0;

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
                // Мест нет или инфицирован — цель эвакуации исчезает
                u.state = UnitState::Walking;
                u.stateTimerSec = 4.0f;
                u.waypoints.clear();
            }
        }
        return;
    }

    if (shouldBeAtWork && u.state != UnitState::Working && u.state != UnitState::GoingToWork
        && u.state != UnitState::Walking) {
        exitBuilding(u, buildings);
        int workId = findNearestAvailableBuilding(u.x, u.y, true);
        if (workId != -1) {
            u.targetBuildingId = workId;
            u.state = UnitState::GoingToWork;
            u.waypoints = buildPathBetween(QPointF(u.x, u.y), buildings[workId].entrancePos);
        } else {
            u.state = UnitState::Walking;
            u.stateTimerSec = 10.0f;
        }
    } else if (!shouldBeAtWork && u.state != UnitState::AtHome && u.state != UnitState::GoingHome
               && u.state != UnitState::Walking) {
        exitBuilding(u, buildings);
        int homeId = findNearestAvailableBuilding(u.x, u.y, false);
        if (homeId != -1) {
            u.targetBuildingId = homeId;
            u.state = UnitState::GoingHome;
            u.waypoints = buildPathBetween(QPointF(u.x, u.y), buildings[homeId].entrancePos);
        } else {
            u.state = UnitState::Walking;
            u.stateTimerSec = 10.0f;
        }
    }

    if (u.state == UnitState::Working || u.state == UnitState::AtHome) {
        u.stateTimerSec -= dt;
        if (u.stateTimerSec <= 0.0f) {
            exitBuilding(u, buildings);
            u.state = UnitState::Walking;
            u.stateTimerSec = static_cast<float>(6 + rng->bounded(9));
            float angle = static_cast<float>(rng->generateDouble() * 6.28318);
            u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
            u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
        }
        return;
    }

    if (u.state == UnitState::Walking) {
        u.stateTimerSec -= dt;
        if (u.stateTimerSec <= 0.0f) {
            int bId = findNearestAvailableBuilding(u.x, u.y, shouldBeAtWork);
            if (bId != -1) {
                u.targetBuildingId = bId;
                u.state = shouldBeAtWork ? UnitState::GoingToWork : UnitState::GoingHome;
                u.waypoints = buildPathBetween(QPointF(u.x, u.y), buildings[bId].entrancePos);
            } else {
                u.stateTimerSec = 5.0f;
            }
        }
        return;
    }
    if (u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome) {
        float rw = roadWidth();
        float reachRadius = rw * 0.35f; // Достижение точки в пределах 35% ширины дороги

        if (!u.waypoints.empty()) {
            QPointF baseTarget = u.waypoints.front();
            float dx = static_cast<float>(baseTarget.x()) - u.x;
            float dy = static_cast<float>(baseTarget.y()) - u.y;
            float dist = std::hypot(dx, dy);

            if (dist < reachRadius) {
                u.waypoints.erase(u.waypoints.begin());
            } else {
                // Смещаем целевую точку перпендикулярно курсу на величину полосы (75% ширины дороги)
                float perpX = -dy / dist;
                float perpY = dx / dist;
                float shift = u.laneOffset * rw;

                float targetX = static_cast<float>(baseTarget.x()) + perpX * shift;
                float targetY = static_cast<float>(baseTarget.y()) + perpY * shift;

                // Если смещённая точка не на дороге (например, у самого входа), берём центр
                if (!isWalkable(targetX, targetY)) {
                    targetX = static_cast<float>(baseTarget.x());
                    targetY = static_cast<float>(baseTarget.y());
                }

                float tdx = targetX - u.x;
                float tdy = targetY - u.y;
                float tdist = std::hypot(tdx, tdy);
                if (tdist > 0.1f) {
                    u.vx = (tdx / tdist) * BASE_CITIZEN_SPEED;
                    u.vy = (tdy / tdist) * BASE_CITIZEN_SPEED;
                }
            }
        }

        if (u.waypoints.empty()) {
            bool wantWork = (u.state == UnitState::GoingToWork);
            if (u.targetBuildingId >= 0 && u.targetBuildingId < static_cast<int>(buildings.size())) {
                auto &targetB = buildings[u.targetBuildingId];
                if (targetB.currentOccupants < targetB.capacity && targetB.health > 0) {
                    enterBuilding(u, targetB);
                    u.state = wantWork ? UnitState::Working : UnitState::AtHome;
                    u.stateTimerSec = static_cast<double>(15 + rng->bounded(21));
                    return;
                }
            }
            int nextBId = findNearestAvailableBuilding(u.x, u.y, wantWork);
            if (nextBId != -1) {
                u.targetBuildingId = nextBId;
                u.waypoints = buildPathBetween(QPointF(u.x, u.y), buildings[nextBId].entrancePos);
            } else {
                u.state = UnitState::Walking;
                u.stateTimerSec = 10.0f;
            }
        }
    }
}
void GameWorld::buildBuildingsGeometry()
{
    m_buildingsGeometry.clear();
    const auto &buildings = m_generator.buildings();
    m_buildingsGeometry.reserve(buildings.size() * 4);

    for (const auto &b : buildings) {
        // 0. Падающая тень вправо-вниз
        m_buildingsGeometry.push_back(
            {b.cx + 2.5f, b.cy + 2.5f, b.width + 4.0f, b.height + 4.0f, b.angleRad, 12, 14, 18, 180});

        // 1. Внешний контур (border 2px)
        m_buildingsGeometry
            .push_back({b.cx, b.cy, b.width + 4.0f, b.height + 4.0f, b.angleRad, 26, 30, 38, 255});

        if (b.isBurnedOut) {
            // ОБУГЛЕННОЕ ЗДАНИЕ (после окончания таймера горения): тёмно-угольное
            m_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 28, 25, 24, 255});
        } else if (b.isBurning) {
            // ГОРЯЩЕЕ ЗДАНИЕ (уничтожено): оранжевый корпус + жёлтое ядро
            m_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, 235, 105, 25, 255});
            m_buildingsGeometry.push_back(
                {b.cx, b.cy, b.width * 0.52f, b.height * 0.52f, b.angleRad, 250, 185, 55, 255});
        } else {
            // ЦЕЛОЕ ЗДАНИЕ
            m_buildingsGeometry
                .push_back({b.cx, b.cy, b.width, b.height, b.angleRad, b.r, b.g, b.b, 255});
        }
    }
}

void GameWorld::spawnInitialUnits(int citizenCount)
{
    auto *rng = QRandomGenerator::global();
    m_units.clear();
    const auto &walkGrid = m_generator.walkGrid();
    int w = m_generator.width();
    int h = m_generator.height();
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

    auto &buildings = const_cast<std::vector<BuildingObject> &>(m_generator.buildings());
    int initialWorkCapacity = 0;
    for (auto &b : buildings) {
        b.currentOccupants = 0;
        b.health = b.maxHealth;
        if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
            initialWorkCapacity += b.capacity;
        }
    }
    m_units.reserve(citizenCount);
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
        u.isNightShift = (rng->bounded(100) < 10);
        u.state = UnitState::Walking;
        u.stateTimerSec = static_cast<double>(rng->bounded(4));
        u.laneOffset = static_cast<float>(
            (rng->generateDouble() - 0.5)
            * 0.75); // Случайная полоса в пределах 75% ширины дороги (-0.375 .. +0.375)

        m_units.push_back(u);
    }
    int dayWorkersEstimate = std::min(static_cast<int>(citizenCount * 0.9f), initialWorkCapacity);
    m_incomePerSec = dayWorkersEstimate * TAX_PER_WORKER_SEC;
    m_citizensCount = citizenCount;
    m_zombiesCount = 0;
}
void GameWorld::updateHourlyTaxes(int newHour, bool isInitialSetup)
{
    auto &buildings = const_cast<std::vector<BuildingObject> &>(m_generator.buildings());

    if (isInitialSetup) {
        // При старте карты заполняем базовый прогноз, чтобы доход сразу работал с первой секунды
        int totalCapacity = 0;
        for (const auto &b : buildings) {
            if (b.type == BuildingType::Office || b.type == BuildingType::Factory) {
                totalCapacity += b.capacity;
            }
        }
        int estimatedWorkers = std::min(static_cast<int>(m_currentConfig.initialCitizens * 0.9f),
                                        totalCapacity);
        m_incomePerSec = estimatedWorkers * TAX_PER_WORKER_SEC;
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

    m_incomePerSec = totalPeak24h * TAX_PER_WORKER_SEC;
    emit statsChanged();
    rebuildWorkplaceLabels();
}

void GameWorld::processOutbreaks()
{
    for (size_t i = 0; i < m_currentConfig.outbreaks.size(); ++i) {
        auto &ob = m_currentConfig.outbreaks[i];
        if (!ob.triggered && m_elapsedSimSec >= ob.triggerTimeSec) {
            ob.triggered = true;
            triggerSingleOutbreak(ob.initialInfected, static_cast<double>(i) + 1);
        }
    }
}
void GameWorld::triggerSingleOutbreak(int count, int index)
{
    std::vector<size_t> outdoorHealthy;
    for (size_t i = 0; i < m_units.size(); ++i) {
        if (m_units[i].type == UnitType::Citizen && !m_units[i].isInfected
            && m_units[i].insideBuildingId == -1) {
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
        infectUnit(m_units[unitIdx], true);
        emit outbreakOccurred(m_units[unitIdx].x, m_units[unitIdx].y, index);
    }
}
bool GameWorld::isWalkable(float x, float y) const
{
    int gx = static_cast<int>(x);
    int gy = static_cast<int>(y);
    int w = m_generator.width();
    int h = m_generator.height();
    if (gx < 0 || gx >= w || gy < 0 || gy >= h)
        return false;
    return m_generator.walkGrid()[gy * w + gx] == 1;
}
bool GameWorld::isCityArea(float x, float y) const
{
    int gx = static_cast<int>(x);
    int gy = static_cast<int>(y);
    int w = m_generator.width();
    int h = m_generator.height();
    if (gx < 0 || gx >= w || gy < 0 || gy >= h)
        return false;
    return m_generator.cityMask()[gy * w + gx] == 1;
}
QPointF GameWorld::findNearestWalkable(float x, float y, float maxRadius) const
{
    if (isWalkable(x, y))
        return QPointF(x, y);
    for (float r = 2.0f; r <= maxRadius; r += 2.0f) {
        int steps = static_cast<double>(r * 3.14f);
        for (int i = 0; i < steps; ++i) {
            float angle = (6.28318f * i) / steps;
            float tx = x + std::cos(angle) * r;
            float ty = y + std::sin(angle) * r;
            if (isWalkable(tx, ty))
                return QPointF(tx, ty);
        }
    }
    return QPointF(-1.0f, -1.0f);
}
void GameWorld::useToolAt(float worldX, float worldY)
{
    if (!isCityArea(worldX, worldY))
        return;
    if (m_selectedTool == 1 && m_budget >= 90) {
        m_budget -= 90;
        for (auto &u : m_units) {
            if (u.insideBuildingId != -1)
                continue;
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
    } else if (m_selectedTool == 2 && m_budget >= 200) {
        QPointF dropPos = findNearestWalkable(worldX, worldY, 40.0f);
        if (dropPos.x() < 0.0f)
            return;
        m_budget -= 200;
        auto *rng = QRandomGenerator::global();
        for (int i = 0; i < 5; ++i) {
            float angle = static_cast<double>(rng->generateDouble() * 6.28318);
            Unit s;
            s.x = static_cast<int>(dropPos.x());
            s.y = static_cast<int>(dropPos.y());
            s.vx = std::cos(angle) * ZOMBIE_CHASE_SPEED;
            s.vy = std::sin(angle) * ZOMBIE_CHASE_SPEED;
            s.type = UnitType::Soldier;
            s.hp = 3; // Здоровье солдата = 3
            s.laneOffset = static_cast<float>(
                (rng->generateDouble() - 0.5)
                * 0.75); // Случайная полоса в пределах 75% ширины дороги (-0.375 .. +0.375)
            m_units.push_back(s);
        }
        float dx = static_cast<float>(dropPos.x());
        float dy = static_cast<float>(dropPos.y());
        emit pulseWaveTriggered(dx, dy, 22.0f, "#48bb78");
        spawnBurstParticles(dx, dy, 12, 120, 255, 160);

        rebuildUnitRenderData();
        emit unitsUpdated();
        emit statsChanged();
    } else if (m_selectedTool == 3 && m_budget >= 75) {
        QVariantMap snap = snapEvacPoint(worldX, worldY, 20.0f);
        if (!snap["snapped"].toBool())
            return;

        int nodeId = snap["nodeId"].toInt();
        QPointF evacPos(snap["x"].toDouble(), snap["y"].toDouble());

        // Не запускаем повторно на той же точке, пока идёт сбор
        for (const auto &ev : m_evacs) {
            if (ev.evacNodeId == nodeId && !ev.completed)
                return;
        }

        // Ищем ближайшую стартовую точку маршрута из группы 1 (#10FFFF) или группы 2 (#1077FF)
        const auto &routes = m_generator.evacRoutes();
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

        m_budget -= 75;

        ActiveEvacuation ev;
        ev.evacNodeId = nodeId;
        ev.evacPos = evacPos;
        ev.timeLeftSec = 30.0f;
        ev.boardedCount = 0;
        ev.completed = false;
        ev.convoyStart = bestRoute->startPos;
        ev.convoyEnd = bestRoute->endPos;
        ev.convoyPos = bestRoute->startPos;
        m_evacs.push_back(ev);

        // Оповещаем всех жителей в круговом радиусе 100 метров (выводим из зданий и направляем к выходу)
        auto &buildings = const_cast<std::vector<BuildingObject> &>(m_generator.buildings());
        for (auto &u : m_units) {
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
                if (u.insideBuildingId != -1) {
                    exitBuilding(u, buildings);
                }
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
bool GameWorld::hasLineOfSight(float x1, float y1, float x2, float y2) const
{
    const auto &walkGrid = m_generator.walkGrid();
    const auto &cityMask = m_generator.cityMask();
    int w = m_generator.width();
    int h = m_generator.height();

    int ix1 = static_cast<int>(x1);
    int iy1 = static_cast<int>(y1);
    int ix2 = static_cast<int>(x2);
    int iy2 = static_cast<int>(y2);

    int dx = std::abs(ix2 - ix1);
    int dy = std::abs(iy2 - iy1);
    int sx = (ix1 < ix2) ? 1 : -1;
    int sy = (iy1 < iy2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (ix1 >= 0 && ix1 < w && iy1 >= 0 && iy1 < h) {
            int idx = iy1 * w + ix1;
            // Если пиксель относится к зоне застройки (cityMask == 1), но это НЕ дорога (walkGrid == 0) — это стена дома
            if (cityMask[idx] == 1 && walkGrid[idx] == 0) {
                return false;
            }
        }
        if (ix1 == ix2 && iy1 == iy2)
            break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            ix1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            iy1 += sy;
        }
    }
    return true;
}
void GameWorld::rebuildBurningLights()
{
    QVariantList list;
    const auto &buildings = m_generator.buildings();

    for (const auto &b : buildings) {
        if (b.isBurning) {
            QVariantMap item;
            item["cx"] = b.cx;
            item["cy"] = b.cy;
            item["radius"] = 40 + b.area / 5.0f; // Радиус освещения X метров
            list.append(item);
        }
    }
    m_burningLights = list;
    emit burningLightsChanged();
}

void GameWorld::rebuildBuildingOverlays()
{
    m_buildingOverlaysGeometry.clear();
    const auto &buildings = m_generator.buildings();

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

            m_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2f, barH + 1.2f, 0.0f, 10, 12, 16, 240});

            // Производственные здания (офисы/заводы) -> ЖЁЛТАЯ заполненность; жилые -> голубая
            uint8_t r = isWorkplace ? 250 : 110;
            uint8_t g = isWorkplace ? 210 : 200;
            uint8_t bl = isWorkplace ? 45 : 255;

            float filledW = std::max(1.5f, barW * fillRatio);
            float fillCx = (b.cx - barW * 0.5f) + filledW * 0.5f;
            m_buildingOverlaysGeometry
                .push_back({fillCx, baseY, filledW, barH, 0.0f, r, g, bl, 255});

            baseY -= (barH + 2.0f);
        }

        if (showHealth) {
            float hpRatio = std::clamp(static_cast<float>(b.health) / b.maxHealth, 0.0f, 1.0f);

            m_buildingOverlaysGeometry
                .push_back({b.cx, baseY, barW + 1.2f, barH + 1.2f, 0.0f, 10, 12, 16, 240});

            if (hpRatio > 0.0f) {
                uint8_t r = (hpRatio < 0.5f) ? 245
                                             : static_cast<uint8_t>((1.0f - hpRatio) * 2.0f * 245);
                uint8_t g = (hpRatio > 0.5f) ? 220 : static_cast<uint8_t>(hpRatio * 2.0f * 220);

                float hpW = std::max(1.5f, barW * hpRatio);
                float hpCx = (b.cx - barW * 0.5f) + hpW * 0.5f;
                m_buildingOverlaysGeometry.push_back({hpCx, baseY, hpW, barH, 0.0f, r, g, 50, 255});
            }
        }
    }
}

void GameWorld::rebuildWorkplaceLabels()
{
    QVariantList list;
    const auto &buildings = m_generator.buildings();

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
    m_workplaceLabels = list;
}
void GameWorld::applyFireAvoidanceAndDamage(Unit &u,
                                            float dt,
                                            const std::vector<BuildingObject> &buildings)
{
    if (u.fireDamageCooldownSec > 0.0f) {
        u.fireDamageCooldownSec -= dt;
    }

    float rw = roadWidth();
    bool isHumanOrSoldier = (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated
                             || u.type == UnitType::Soldier);

    for (const auto &b : buildings) {
        if (!b.isBurning)
            continue;

        // Световой радиус из rebuildBurningLights: R = 40.0f + b.area / 5.0f
        // Диаметр зоны пожара = R * 2 * 0.35 -> радиус опасной зоны = R * 0.35f
        float lightRadius = 40.0f + b.area / 5.0f;
        float fireRadius = lightRadius * 0.35f;
        float avoidRadius = fireRadius + rw * 0.75f; // Начинаем обходить заранее с запасом

        float dx = u.x - b.cx;
        float dy = u.y - b.cy;
        float distSq = dx * dx + dy * dy;

        // 1. Урон от огня: внутри fireRadius юнит теряет 2 HP каждые 0.5 секунды
        if (distSq <= fireRadius * fireRadius) {
            if (u.fireDamageCooldownSec <= 0.0f) {
                u.fireDamageCooldownSec = 0.5f;
                u.hp -= 2;
                spawnBurstParticles(u.x, u.y, 4, 255, 120, 30);
            }
        }

        // 2. Избегание пожара жителями и солдатами
        if (isHumanOrSoldier && distSq <= avoidRadius * avoidRadius && distSq > 0.001f) {
            float dist = std::sqrt(distSq);
            // Вектор от центра пожара + касательный вектор для обхода сбоку
            float awayX = dx / dist;
            float awayY = dy / dist;
            float tangentX = -awayY;
            float tangentY = awayX;

            float speed = std::hypot(u.vx, u.vy);
            if (speed < BASE_CITIZEN_SPEED)
                speed = RUN_SPEED;

            // Смешиваем отталкивание от огня и обход вдоль улицы
            float steerX = awayX * 1.4f + tangentX * 0.6f;
            float steerY = awayY * 1.4f + tangentY * 0.6f;
            float steerLen = std::hypot(steerX, steerY);

            u.vx = (steerX / steerLen) * speed;
            u.vy = (steerY / steerLen) * speed;
        }
    }
}

void GameWorld::applyWallRepulsion(Unit &u, float dt)
{
    const float probeDist = roadWidth() * 0.125f;
    const float pushStrength = roadWidth() * 2.2f;

    float pushX = 0.0f;
    float pushY = 0.0f;

    if (!isWalkable(u.x + probeDist, u.y))
        pushX -= pushStrength;
    if (!isWalkable(u.x - probeDist, u.y))
        pushX += pushStrength;
    if (!isWalkable(u.x, u.y + probeDist))
        pushY -= pushStrength;
    if (!isWalkable(u.x, u.y - probeDist))
        pushY += pushStrength;

    if (pushX != 0.0f || pushY != 0.0f) {
        float targetSpeed = std::hypot(u.vx, u.vy);
        u.vx += pushX * dt;
        u.vy += pushY * dt;

        float newSpeed = std::hypot(u.vx, u.vy);
        if (newSpeed > 0.01f && targetSpeed > 0.01f) {
            u.vx = (u.vx / newSpeed) * targetSpeed;
            u.vy = (u.vy / newSpeed) * targetSpeed;
        }
    }
}

void GameWorld::tick()
{
    if (m_paused)
        return;

    auto *rng = QRandomGenerator::global();
    const float dt = 0.016f * m_speedMultiplier;
    const float dtHours = dt * m_currentConfig.gameHoursPerSec;

    m_elapsedSimSec += dt;
    m_timeOfDayHours += dtHours;
    if (m_timeOfDayHours >= 24.0f) {
        m_timeOfDayHours -= 24.0f;
        m_dayNumber++;
    }

    int currentHour = static_cast<int>(m_timeOfDayHours) % 24;
    if (currentHour != m_lastTrackedHour) {
        m_lastTrackedHour = currentHour;
        updateHourlyTaxes(currentHour, false);
    }
    emit timeChanged();

    if (m_incomePerSec > 0.0) {
        m_budget += m_incomePerSec * dt;
        emit statsChanged();
    }

    processOutbreaks();

    auto &buildings = const_cast<std::vector<BuildingObject> &>(m_generator.buildings());
    bool buildingsVisualDirty = false;

    // 0. Обновляем таймеры атаки и горения зданий
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

    // 1. Обновляем инкубационный таймер у заражённых
    for (size_t i = 0; i < m_units.size(); ++i) {
        Unit &u = m_units[i];
        if (u.isInfected && u.type != UnitType::Zombie) {
            u.infectionHoursLeft -= dtHours;
            if (u.infectionHoursLeft <= 0.0f) {
                int bId = u.insideBuildingId;
                if (bId != -1) {
                    // Инфицированный превратился в зомби внутри здания -> выкидываем наружу ВСЕХ!
                    for (auto &other : m_units) {
                        if (other.insideBuildingId == bId) {
                            exitBuilding(other, buildings);
                            other.panicTimerSec = 3.0f;
                            other.state = UnitState::Walking;
                            other.stateTimerSec = 6.0f;
                            float away = static_cast<float>(rng->generateDouble() * 6.28318);
                            other.vx = std::cos(away) * RUN_SPEED;
                            other.vy = std::sin(away) * RUN_SPEED;
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

    // 2. Собираем списки открытых целей на улице
    std::vector<size_t> activeZombies;
    std::vector<size_t> panickingOutdoors;
    std::vector<size_t> zombieTargets;

    for (size_t i = 0; i < m_units.size(); ++i) {
        const auto &u = m_units[i];
        if (u.insideBuildingId != -1)
            continue;

        if (u.type == UnitType::Zombie) {
            activeZombies.push_back(i);
        } else {
            if (u.panicTimerSec > 0.0f) {
                panickingOutdoors.push_back(i);
            }
            if (!u.isInfected) {
                zombieTargets.push_back(i);
            }
        }
    }

    // 3. Обновляем поведение всех юнитов
    for (size_t i = 0; i < m_units.size(); ++i) {
        Unit &u = m_units[i];

        // --- А. ПОВЕДЕНИЕ ЖИТЕЛЕЙ И ВАКЦИНИРОВАННЫХ ---
        if (u.type == UnitType::Citizen || u.type == UnitType::Vaccinated) {
            if (u.insideBuildingId != -1) {
                updateCitizenSchedule(u, dt, buildings);
                continue;
            }

            // Если сработал эвакуационный сигнал — житель не поддаётся слепой панике, а держит курс на выход
            if (u.state == UnitState::Evacuating) {
                u.panicTimerSec = 0.0f;
                updateCitizenSchedule(u, dt, buildings);
            } else {
                bool hasThreat = false;
                float threatX = 0.0f, threatY = 0.0f;

                for (size_t zIdx : activeZombies) {
                    float dx = m_units[zIdx].x - u.x;
                    float dy = m_units[zIdx].y - u.y;
                    if (dx * dx + dy * dy <= 45.0f * 45.0f
                        && hasLineOfSight(u.x, u.y, m_units[zIdx].x, m_units[zIdx].y)) {
                        hasThreat = true;
                        threatX = m_units[zIdx].x;
                        threatY = m_units[zIdx].y;
                        break;
                    }
                }

                if (!hasThreat) {
                    int nearbyPanic = 0;
                    float avgPx = 0.0f, avgPy = 0.0f;
                    for (size_t pIdx : panickingOutdoors) {
                        if (pIdx == i)
                            continue;
                        float dx = m_units[pIdx].x - u.x;
                        float dy = m_units[pIdx].y - u.y;
                        if (dx * dx + dy * dy <= 35.0f * 35.0f
                            && hasLineOfSight(u.x, u.y, m_units[pIdx].x, m_units[pIdx].y)) {
                            nearbyPanic++;
                            avgPx += m_units[pIdx].x;
                            avgPy += m_units[pIdx].y;
                        }
                    }

                    if (nearbyPanic > 10 || (nearbyPanic >= 5 && rng->bounded(100) < 50)) {
                        hasThreat = true;
                        threatX = avgPx / nearbyPanic;
                        threatY = avgPy / nearbyPanic;
                    }
                }

                bool isEvacMode = (u.state == UnitState::Evacuating
                                   || u.state == UnitState::PanicEvacuating);

                if (hasThreat) {
                    u.panicTimerSec = 3.0f;

                    if (isEvacMode) {
                        // В поле эвакуации: переходим в паническую эвакуацию, сохраняя курс на выход!
                        u.state = UnitState::PanicEvacuating;
                        updateCitizenSchedule(u, dt, buildings);
                    } else {
                        // Обычная паника вне эвакуации: убегаем в противоположную сторону от зомби
                        u.waypoints.clear();
                        float baseAway = std::atan2(u.y - threatY, u.x - threatX);
                        float bestAngle = baseAway;

                        for (int attempt = 0; attempt < 5; ++attempt) {
                            float testAngle = baseAway
                                              + static_cast<float>((rng->generateDouble() - 0.5)
                                                                   * 1.4);
                            float testX = u.x + std::cos(testAngle) * 6.0f;
                            float testY = u.y + std::sin(testAngle) * 6.0f;
                            if (isWalkable(testX, testY)) {
                                bestAngle = testAngle;
                                break;
                            }
                        }

                        u.vx = std::cos(bestAngle) * RUN_SPEED;
                        u.vy = std::sin(bestAngle) * RUN_SPEED;
                    }
                } else if (u.panicTimerSec > 0.0f) {
                    u.panicTimerSec -= dt;
                    if (u.panicTimerSec <= 0.0f) {
                        u.panicTimerSec = 0.0f;
                        if (u.state == UnitState::PanicEvacuating) {
                            // Угроза миновала, продолжаем спокойную эвакуацию
                            u.state = UnitState::Evacuating;
                        } else {
                            u.state = UnitState::Walking;
                            u.stateTimerSec = 2.0f;
                        }
                    } else if (u.state == UnitState::PanicEvacuating) {
                        // Пока таймер паники ещё тикает — продолжаем корректировать движение к выходу
                        updateCitizenSchedule(u, dt, buildings);
                    }
                } else {
                    updateCitizenSchedule(u, dt, buildings);
                    if (u.insideBuildingId != -1)
                        continue;

                    if (u.state == UnitState::Walking && rng->bounded(100) < 4) {
                        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                        u.vx = std::cos(angle) * BASE_CITIZEN_SPEED;
                        u.vy = std::sin(angle) * BASE_CITIZEN_SPEED;
                    }
                }
            }
        }

        // --- Б. ПОВЕДЕНИЕ СОЛДАТ ---
        else if (u.type == UnitType::Soldier) {
            int bestZ = -1;
            float bestDistSq = 85.0f * 85.0f;

            for (size_t zIdx : activeZombies) {
                if (m_units[zIdx].hp <= 0)
                    continue;
                float dx = m_units[zIdx].x - u.x;
                float dy = m_units[zIdx].y - u.y;
                float dSq = dx * dx + dy * dy;
                if (dSq < bestDistSq && hasLineOfSight(u.x, u.y, m_units[zIdx].x, m_units[zIdx].y)) {
                    bestDistSq = dSq;
                    bestZ = static_cast<int>(zIdx);
                }
            }

            if (bestZ != -1) {
                float zx = m_units[bestZ].x;
                float zy = m_units[bestZ].y;
                u.hasLastKnownTarget = true;
                u.lastKnownTargetPos = QPointF(zx, zy);

                float dx = zx - u.x;
                float dy = zy - u.y;
                float d = std::sqrt(bestDistSq);

                if (d <= 35.0f && u.attackCooldownSec <= 0.0f) {
                    u.attackCooldownSec = 1.0f;
                    spawnBulletTracer(u.x, u.y, zx, zy);
                    m_units[bestZ].hp -= 1;
                    if (m_units[bestZ].hp <= 0) {
                        spawnBurstParticles(zx, zy, 8, 255, 60, 60);
                        m_eliminatedCount++;
                        m_budget += 5;
                        u.hasLastKnownTarget = false;
                    }
                }

                if (d > 11.5f) {
                    u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
                    u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
                } else if (d < 9.5f && d > 0.1f) {
                    u.vx = -(dx / d) * ZOMBIE_CHASE_SPEED;
                    u.vy = -(dy / d) * ZOMBIE_CHASE_SPEED;
                } else {
                    u.vx = 0.0f;
                    u.vy = 0.0f;
                }
            } else if (u.hasLastKnownTarget) {
                float dx = static_cast<float>(u.lastKnownTargetPos.x()) - u.x;
                float dy = static_cast<float>(u.lastKnownTargetPos.y()) - u.y;
                float d = std::hypot(dx, dy);
                if (d < 4.0f) {
                    u.hasLastKnownTarget = false;
                } else {
                    u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
                    u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
                }
            } else if (rng->bounded(100) < 4) {
                float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                u.vx = std::cos(angle) * ZOMBIE_CHASE_SPEED;
                u.vy = std::sin(angle) * ZOMBIE_CHASE_SPEED;
            }
        }
        // --- В. ПОВЕДЕНИЕ ЗОМБИ ---
        else if (u.type == UnitType::Zombie) {
            int bestTarget = -1;
            float bestDistSq = 75.0f * 75.0f;

            for (size_t tIdx : zombieTargets) {
                if (m_units[tIdx].hp <= 0)
                    continue;
                float dx = m_units[tIdx].x - u.x;
                float dy = m_units[tIdx].y - u.y;
                float dSq = dx * dx + dy * dy;
                if (dSq < bestDistSq && hasLineOfSight(u.x, u.y, m_units[tIdx].x, m_units[tIdx].y)) {
                    bestDistSq = dSq;
                    bestTarget = static_cast<int>(tIdx);
                }
            }
            if (bestTarget != -1) {
                float tx = m_units[bestTarget].x;
                float ty = m_units[bestTarget].y;
                u.hasLastKnownTarget = true;
                u.lastKnownTargetPos = QPointF(tx, ty);
                float dx = tx - u.x;
                float dy = ty - u.y;
                float d = std::sqrt(bestDistSq);
                if (d <= 3.5f) {
                    if (u.attackCooldownSec <= 0.0f) {
                        u.attackCooldownSec = 0.5f;
                        Unit &victim = m_units[bestTarget];
                        if (victim.type == UnitType::Soldier
                            || victim.type == UnitType::Vaccinated) {
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
            } else if (u.hasLastKnownTarget) {
                float dx = static_cast<float>(u.lastKnownTargetPos.x()) - u.x;
                float dy = static_cast<float>(u.lastKnownTargetPos.y()) - u.y;
                float d = std::hypot(dx, dy);
                if (d < 4.0f) {
                    u.hasLastKnownTarget = false;
                } else {
                    u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
                    u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
                }
            } else {
                // Проверяем видимые здания с людьми (жилые дома, офисы и заводы) в радиусе 75м
                int bestBldg = -1;
                float bestBldgDistSq = 75.0f * 75.0f;

                for (const auto &b : buildings) {
                    if (b.currentOccupants > 0 && b.health > 0 && !b.isBurning && !b.isBurnedOut) {
                        float bx = static_cast<float>(b.entrancePos.x());
                        float by = static_cast<float>(b.entrancePos.y());
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
                    float dx = static_cast<float>(b.entrancePos.x()) - u.x;
                    float dy = static_cast<float>(b.entrancePos.y()) - u.y;
                    float d = std::sqrt(bestBldgDistSq);
                    if (d <= 5.0f) {
                        if (u.attackCooldownSec <= 0.0f) {
                            u.attackCooldownSec = 0.5f;
                            b.health = std::max(0, b.health - 1);
                            b.underAttackTimerSec
                                = 3.0f; // Здание под атакой (и ещё 3 сек после удара)
                            spawnBurstParticles(static_cast<int>(b.entrancePos.x()),
                                                static_cast<int>(b.entrancePos.y()),
                                                3,
                                                255,
                                                160,
                                                60);
                            // При 0% здоровья здание разрушается, загорается на area/15 сек, а люди выбегают в панике!
                            if (b.health == 0 && !b.isBurning && !b.isBurnedOut) {
                                b.isBurning = true;
                                b.burnTimerSec = b.area / 15.0f;
                                buildingsVisualDirty = true;
                                rebuildWorkplaceLabels();
                                for (auto &occupant : m_units) {
                                    if (occupant.insideBuildingId == b.id) {
                                        exitBuilding(occupant, buildings);
                                        occupant.panicTimerSec = 3.0f;
                                        occupant.state = UnitState::Walking;
                                        occupant.stateTimerSec = 8.0f;
                                        float away = std::atan2(occupant.y - b.cy,
                                                                occupant.x - b.cx);
                                        occupant.vx = std::cos(away) * RUN_SPEED;
                                        occupant.vy = std::sin(away) * RUN_SPEED;
                                    }
                                }
                            }
                        }
                    } else {
                        u.vx = (dx / d) * ZOMBIE_CHASE_SPEED;
                        u.vy = (dy / d) * ZOMBIE_CHASE_SPEED;
                    }
                } else if (rng->bounded(100) < 4) {
                    float angle = static_cast<float>(rng->generateDouble() * 6.28318);
                    u.vx = std::cos(angle) * ZOMBIE_WANDER_SPEED;
                    u.vy = std::sin(angle) * ZOMBIE_WANDER_SPEED;
                }
            }
        }
        // Проверка на пожароопасные зоны
        applyFireAvoidanceAndDamage(u, dt, buildings);
        // Отталкивание от бордюров к центру дороги
        applyWallRepulsion(u, dt);

        // Шаг перемещения со скольжением вдоль стен
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

        if ((u.state == UnitState::GoingToWork || u.state == UnitState::GoingHome)
            && u.insideBuildingId == -1) {
            u.stuckTimerSec += dt;
            if (u.stuckTimerSec >= 1.5f) {
                float moved = static_cast<float>(
                    std::hypot(u.x - u.lastCheckPos.x(), u.y - u.lastCheckPos.y()));
                u.lastCheckPos = QPointF(u.x, u.y);
                u.stuckTimerSec = 0.0f;

                if (moved < roadWidth() * 0.25f && u.targetBuildingId >= 0
                    && u.targetBuildingId < static_cast<int>(buildings.size())) {
                    u.waypoints = buildPathBetween(QPointF(u.x, u.y),
                                                   buildings[u.targetBuildingId].entrancePos);
                }
            }
        }
    }

    if (buildingsVisualDirty) {
        buildBuildingsGeometry();
        emit mapUpdated();
    }
    // 4. Удаляем погибших и обновляем счётчики
    int citizens = 0;
    int zombies = 0;
    for (size_t i = 0; i < m_units.size();) {
        if (m_units[i].hp <= 0) {
            m_units[i] = m_units.back();
            m_units.pop_back();
        } else {
            if (m_units[i].type == UnitType::Citizen || m_units[i].type == UnitType::Vaccinated)
                citizens++;
            else if (m_units[i].type == UnitType::Zombie)
                zombies++;
            ++i;
        }
    }
    if (citizens != m_citizensCount || zombies != m_zombiesCount) {
        m_citizensCount = citizens;
        m_zombiesCount = zombies;
        emit statsChanged();
    }
    updateEffects(dt);
    updateEvacuations(dt);
    // Постоянный призыв на эвакуацию всех жителей (на улице и внутри зданий), оказавшихся в радиусе 100м
    for (const auto &ev : m_evacs) {
        if (ev.completed)
            continue; // Когда 50 мест заняты или 30с вышли, новых больше не зовём

        for (auto &u : m_units) {
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
            if (dx * dx + dy * dy <= 100.0f * 100.0f) {
                if (u.insideBuildingId != -1) {
                    exitBuilding(u, buildings);
                }
                // Если житель уже паникует -> переводим в PanicEvacuating, иначе -> Evacuating
                if (u.panicTimerSec > 0.0f) {
                    u.state = UnitState::PanicEvacuating;
                } else {
                    u.state = UnitState::Evacuating;
                }
                u.targetBuildingId = ev.evacNodeId;
                u.waypoints = buildPathBetween(QPointF(u.x, u.y), ev.evacPos);
            }
        }
    }

    rebuildUnitRenderData();
    rebuildBuildingOverlays();
    emit unitsUpdated();
}
void GameWorld::rebuildUnitRenderData()
{
    m_unitGeometry.clear();
    m_unitGeometry.reserve(m_units.size());
    for (const auto &u : m_units) {
        if (u.insideBuildingId != -1)
            continue;
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
            r = 80;
            g = 180;
            b = 255; // Синий — солдат
            size = 4.0f;
        } else if (u.isInfected) {
            r = 255;
            g = 175;
            b = 45; // Жёлто-оранжевый — скрыто инфицированный
        } else if (u.panicTimerSec > 0.0f || u.state == UnitState::PanicEvacuating) {
            r = 255;
            g = 115;
            b = 115; // Красноватый — в панике
        }
        m_unitGeometry.push_back({u.x, u.y, size, size, 0.0f, r, g, b, 255});
    }
    // Отрисовка эвакуированных пассажиров в конвое (круг -> овал при движении)
    for (const auto &ev : m_evacs) {
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

            m_unitGeometry.push_back({wx, wy, 2.0f, 2.0f, 0.0f, 240, 245, 255, 255});
        }
    }
}
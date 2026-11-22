#include "systems/NavigationService.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include "MapGenerator.h"
#include "core/IDestructible.h"
#include "core/NavGrid.h"

void NavigationService::buildGraphCache(MapGenerator &generator, const NavGrid &navGrid)
{
    // Задачи:
    // 1. Построить список смежности дорожного графа roadAdj_
    // 2. Заполнить пространственную сетку узлов nodeSpatialGrid_ (шаг 64 м)
    // 3. Предрассчитать матрицу кратчайших расстояний между всеми парами узлов
    // 4. Привязать каждое здание к ближайшему видимому узлу дороги

    const auto &nodes = generator.nodes();
    const auto &edges = generator.edges();
    const int n = static_cast<int>(nodes.size());

    // 1. Списки смежности
    roadAdj_.assign(n, {});
    for (const auto &e : edges) {
        const float dist = static_cast<float>(
            std::hypot(nodes[e.fromId].pos.x() - nodes[e.toId].pos.x(),
                       nodes[e.fromId].pos.y() - nodes[e.toId].pos.y()));
        roadAdj_[e.fromId].push_back({e.toId, dist});
        roadAdj_[e.toId].push_back({e.fromId, dist});
    }

    // 2. Пространственная сетка узлов дорог
    gridCols_ = (generator.width() + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE;
    gridRows_ = (generator.height() + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE;
    nodeSpatialGrid_.assign(static_cast<size_t>(gridCols_ * gridRows_), {});

    for (int i = 0; i < n; ++i) {
        const int cx = std::clamp(static_cast<int>(nodes[i].pos.x()) / GRID_CELL_SIZE,
                                  0,
                                  gridCols_ - 1);
        const int cy = std::clamp(static_cast<int>(nodes[i].pos.y()) / GRID_CELL_SIZE,
                                  0,
                                  gridRows_ - 1);
        nodeSpatialGrid_[static_cast<size_t>(cy * gridCols_ + cx)].push_back(i);
    }

    // 3. Матрица кратчайших расстояний (All-Pairs Dijkstra)
    allPairsDist_.assign(n, std::vector<float>(n, 1e9F));
    using PQItem = std::pair<float, int>;

    for (int start = 0; start < n; ++start) {
        auto &dist = allPairsDist_[start];
        dist[start] = 0.0F;

        std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;
        pq.push({0.0F, start});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            if (d > dist[u])
                continue;

            for (const auto &[v, weight] : roadAdj_[u]) {
                if (dist[u] + weight < dist[v]) {
                    dist[v] = dist[u] + weight;
                    pq.push({dist[v], v});
                }
            }
        }
    }

    // 4. Привязка зданий к узлу дороги
    auto &buildings = generator.mutableBuildings();
    for (auto &b : buildings) {
        if (b.pathFromRoad.empty()) {
            b.nearestNodeId = -1;
            b.distToNode = 1e9F;
            continue;
        }
        if (b.nearestNodeId >= 0 && b.nearestNodeId < n) {
            b.distToNode = 0.0F;
            continue;
        }

        const float bx = static_cast<float>(b.pathFromRoad.front().x());
        const float by = static_cast<float>(b.pathFromRoad.front().y());

        int bestNode = -1;
        float bestDist = 1e9F;

        for (int i = 0; i < n; ++i) {
            const float nx = static_cast<float>(nodes[i].pos.x());
            const float ny = static_cast<float>(nodes[i].pos.y());
            const float d = std::hypot(nx - bx, ny - by);
            if (d < bestDist && hasWalkableLineOfSight(nx, ny, bx, by, navGrid)) {
                bestDist = d;
                bestNode = i;
            }
        }
        if (bestNode == -1) {
            bestNode = findNearestRoadNode(bx, by, nodes, navGrid);
            if (bestNode != -1) {
                bestDist = static_cast<float>(
                    std::hypot(nodes[bestNode].pos.x() - bx, nodes[bestNode].pos.y() - by));
            }
        }
        b.nearestNodeId = bestNode;
        b.distToNode = bestDist;
    }
}

void NavigationService::updateRoadEdgeWeights(
    const std::vector<RoadNode> &nodes,
    const std::vector<RoadEdge> &edges,
    const IDestructibleProvider *obstacles,
    bool (*segmentsIntersectFn)(QPointF, QPointF, QPointF, QPointF))
{
    // Задачи:
    // 1. Пересчитать веса рёбер дорожного графа: заблокированным рёбрам назначить штраф 1e8F
    (void) segmentsIntersectFn;
    (void) obstacles;
    const int n = static_cast<int>(nodes.size());
    if (n == 0)
        return;

    roadAdj_.assign(n, {});
    for (const auto &e : edges) {
        const QPointF p1 = nodes[e.fromId].pos;
        const QPointF p2 = nodes[e.toId].pos;
        const float dist = static_cast<float>(std::hypot(p1.x() - p2.x(), p1.y() - p2.y()));
        roadAdj_[e.fromId].push_back({e.toId, dist});
        roadAdj_[e.toId].push_back({e.fromId, dist});
    }
}

std::vector<QPointF> NavigationService::buildPathBetween(float startX,
                                                         float startY,
                                                         float endX,
                                                         float endY,
                                                         const MapGenerator &generator,
                                                         const NavGrid &navGrid,
                                                         bool ignoreDestructibleWalls) const
{
    return buildPathBetween(QPointF(startX, startY),
                            QPointF(endX, endY),
                            generator,
                            navGrid,
                            ignoreDestructibleWalls);
}

std::vector<QPointF> NavigationService::buildPathBetween(QPointF startPos,
                                                         QPointF endPos,
                                                         const MapGenerator &generator,
                                                         const NavGrid &navGrid,
                                                         bool ignoreDestructibleWalls) const
{
    // Задачи:
    // 1. Обработать короткий путь внутри одного здания или по прямой видимости на улице
    // 2. Вывести путь из стартового здания до ближайшего дорожного узла
    // 3. Найти кратчайшую цепочку узлов в roadAdj_ алгоритмом Дейкстры
    // 4. Сгладить первый узел, если он остался позади юнита

    const auto &buildings = generator.buildings();
    const auto &nodes = generator.nodes();

    const int startBId = navGrid.buildingIdAt(static_cast<int>(startPos.x()),
                                              static_cast<int>(startPos.y()));
    const int endBId = navGrid.buildingIdAt(static_cast<int>(endPos.x()),
                                            static_cast<int>(endPos.y()));

    // 1. Внутри одного здания
    if (startBId != -1 && startBId == endBId) {
        if (hasWalkableLineOfSight(static_cast<float>(startPos.x()),
                                   static_cast<float>(startPos.y()),
                                   static_cast<float>(endPos.x()),
                                   static_cast<float>(endPos.y()),
                                   navGrid)) {
            return {endPos};
        }
        for (const auto &b : buildings) {
            if (b.id == startBId)
                return {QPointF(b.cx, b.cy), endPos};
        }
    }

    std::vector<QPointF> fullPath;

    // 2. Выход из стартового здания
    const BuildingObject *startBuilding = nullptr;
    if (startBId != -1) {
        for (const auto &b : buildings) {
            if (b.id == startBId) {
                startBuilding = &b;
                break;
            }
        }
        if (startBuilding && startBuilding->nearestNodeId != -1) {
            const QPointF doorPos = nodes[startBuilding->nearestNodeId].pos;
            if (!hasWalkableLineOfSight(static_cast<float>(startPos.x()),
                                        static_cast<float>(startPos.y()),
                                        static_cast<float>(doorPos.x()),
                                        static_cast<float>(doorPos.y()),
                                        navGrid)) {
                fullPath.emplace_back(startBuilding->cx, startBuilding->cy);
            }
            fullPath.push_back(doorPos);
        }
    }

    const BuildingObject *endBuilding = nullptr;
    if (endBId != -1) {
        for (const auto &b : buildings) {
            if (b.id == endBId) {
                endBuilding = &b;
                break;
            }
        }
    }

    const float sx = static_cast<float>(startPos.x());
    const float sy = static_cast<float>(startPos.y());
    const float ex = static_cast<float>(endPos.x());
    const float ey = static_cast<float>(endPos.y());

    const int startNode = (startBuilding && startBuilding->nearestNodeId != -1)
                              ? startBuilding->nearestNodeId
                              : findNearestRoadNode(sx, sy, nodes, navGrid);

    const int endNode = (endBuilding && endBuilding->nearestNodeId != -1)
                            ? endBuilding->nearestNodeId
                            : findNearestRoadNode(ex, ey, nodes, navGrid);

    if (!startBuilding && !endBuilding && std::hypot(ex - sx, ey - sy) < 45.0F
        && hasWalkableLineOfSight(sx, sy, ex, ey, navGrid)) {
        fullPath.push_back(endPos);
        return fullPath;
    }

    if (startNode == -1 || endNode == -1) {
        fullPath.push_back(endPos);
        return fullPath;
    }

    if (startNode == endNode) {
        if (fullPath.empty())
            fullPath.push_back(nodes[startNode].pos);
        if (!endBuilding
            || std::hypot(ex - static_cast<float>(nodes[endNode].pos.x()),
                          ey - static_cast<float>(nodes[endNode].pos.y()))
                   > 1.0F) {
            fullPath.push_back(endPos);
        }
        return fullPath;
    }

    // 3. Поиск кратчайшего пути по графу (Дейкстра)
    const int n = static_cast<int>(nodes.size());
    std::vector<float> dist(n, 1e9F);
    std::vector<int> prev(n, -1);
    using PQItem = std::pair<float, int>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    dist[startNode] = 0.0F;
    pq.push({0.0F, startNode});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();

        if (u == endNode)
            break;
        if (d > dist[u])
            continue;

        for (const auto &[v, weight] : roadAdj_[u]) {
            float effectiveWeight = weight;
            if (weight >= 1e7F) {
                if (!ignoreDestructibleWalls)
                    continue;
                effectiveWeight = static_cast<float>(
                    std::hypot(nodes[u].pos.x() - nodes[v].pos.x(),
                               nodes[u].pos.y() - nodes[v].pos.y()));
            }
            if (dist[u] + effectiveWeight < dist[v]) {
                dist[v] = dist[u] + effectiveWeight;
                prev[v] = u;
                pq.push({dist[v], v});
            }
        }
    }

    if (dist[endNode] >= 1e7F)
        return {};

    std::vector<int> nodeChain;
    for (int curr = endNode; curr != -1; curr = prev[curr]) {
        nodeChain.push_back(curr);
    }
    std::reverse(nodeChain.begin(), nodeChain.end());

    // 4. Сглаживание стартового узла
    size_t startIdx = (startBuilding && !nodeChain.empty() && nodeChain[0] == startNode) ? 1 : 0;

    if (!startBuilding && nodeChain.size() >= 2) {
        const QPointF p0 = nodes[nodeChain[0]].pos;
        const QPointF p1 = nodes[nodeChain[1]].pos;
        const float dToFirst = std::hypot(static_cast<float>(p0.x()) - sx,
                                          static_cast<float>(p0.y()) - sy);
        const float segLen = std::hypot(static_cast<float>(p1.x() - p0.x()),
                                        static_cast<float>(p1.y() - p0.y()));
        const float dToSecond = std::hypot(static_cast<float>(p1.x()) - sx,
                                           static_cast<float>(p1.y()) - sy);

        if (dToFirst < 3.0F
            || (dToSecond < segLen
                && hasWalkableLineOfSight(sx,
                                          sy,
                                          static_cast<float>(p1.x()),
                                          static_cast<float>(p1.y()),
                                          navGrid))) {
            startIdx = 1;
        }
    }

    for (size_t i = startIdx; i < nodeChain.size(); ++i) {
        fullPath.push_back(nodes[nodeChain[i]].pos);
    }

    fullPath.push_back(endPos);
    return fullPath;
}

int NavigationService::findNearestRoadNode(float x,
                                           float y,
                                           const std::vector<RoadNode> &nodes,
                                           const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Найти ближайший видимый узел графа дорог; при отсутствии видимых — ближайший по расстоянию

    int bestVisibleId = -1;
    float bestVisibleDist = 1e9F;
    int fallbackId = -1;
    float fallbackDist = 1e9F;

    for (size_t i = 0; i < nodes.size(); ++i) {
        const float nx = static_cast<float>(nodes[i].pos.x());
        const float ny = static_cast<float>(nodes[i].pos.y());
        const float d = std::hypot(nx - x, ny - y);

        if (d < fallbackDist) {
            fallbackDist = d;
            fallbackId = static_cast<int>(i);
        }
        if (d < bestVisibleDist && hasWalkableLineOfSight(x, y, nx, ny, navGrid)) {
            bestVisibleDist = d;
            bestVisibleId = static_cast<int>(i);
        }
    }
    return (bestVisibleId != -1) ? bestVisibleId : fallbackId;
}

QPointF NavigationService::findNearestWalkable(float x,
                                               float y,
                                               float maxRadius,
                                               const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Проверить саму точку (x, y) и затем концентрические окружности до maxRadius

    if (navGrid.isWalkable(x, y))
        return QPointF(x, y);

    for (float r = 2.0F; r <= maxRadius; r += 2.0F) {
        const int steps = std::max(4, static_cast<int>(r * 3.14F));
        for (int i = 0; i < steps; ++i) {
            const float angle = (6.28318F * i) / steps;
            const float tx = x + std::cos(angle) * r;
            const float ty = y + std::sin(angle) * r;
            if (navGrid.isWalkable(tx, ty))
                return QPointF(tx, ty);
        }
    }
    return QPointF(-1.0F, -1.0F);
}

int NavigationService::findNearestAvailableBuilding(float x,
                                                    float y,
                                                    bool wantWorkplace,
                                                    const MapGenerator &generator,
                                                    const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Найти стартовый дорожный узел через пространственную сетку 3x3
    // 2. По матрице allPairsDist_ выбрать ближайшее незаполненное и не горящее здание нужного типа

    const auto &buildings = generator.buildings();
    const auto &nodes = generator.nodes();
    if (buildings.empty() || nodes.empty() || allPairsDist_.empty())
        return -1;

    int startNode = -1;
    float bestStartDist = 1e9F;

    const int centerCx = std::clamp(static_cast<int>(x) / GRID_CELL_SIZE, 0, gridCols_ - 1);
    const int centerCy = std::clamp(static_cast<int>(y) / GRID_CELL_SIZE, 0, gridRows_ - 1);

    for (int dy = -1; dy <= 1; ++dy) {
        const int ny = centerCy + dy;
        if (ny < 0 || ny >= gridRows_)
            continue;

        for (int dx = -1; dx <= 1; ++dx) {
            const int nx = centerCx + dx;
            if (nx < 0 || nx >= gridCols_)
                continue;

            for (int nodeIdx : nodeSpatialGrid_[static_cast<size_t>(ny * gridCols_ + nx)]) {
                const float px = static_cast<float>(nodes[nodeIdx].pos.x());
                const float py = static_cast<float>(nodes[nodeIdx].pos.y());
                const float d = std::hypot(px - x, py - y);
                if (d < bestStartDist && hasWalkableLineOfSight(x, y, px, py, navGrid)) {
                    bestStartDist = d;
                    startNode = nodeIdx;
                }
            }
        }
    }

    if (startNode == -1) {
        startNode = findNearestRoadNode(x, y, nodes, navGrid);
        if (startNode != -1) {
            bestStartDist = static_cast<float>(
                std::hypot(nodes[startNode].pos.x() - x, nodes[startNode].pos.y() - y));
        }
    }
    if (startNode == -1)
        return -1;

    const auto &distFromStart = allPairsDist_[startNode];

    int bestId = -1;
    float bestDist = 1e8F;

    for (const auto &b : buildings) {
        const bool isWork = (b.type == BuildingType::Office || b.type == BuildingType::Factory);
        if (wantWorkplace != isWork)
            continue;
        if (b.reservedOccupants >= b.capacity)
            continue;
        if (b.health <= 0 || b.isBurning || b.isBurnedOut || b.underAttackTimerSec > 0.0F)
            continue;
        if (b.nearestNodeId == -1 || b.pathFromRoad.empty())
            continue;

        const float roadDist = distFromStart[b.nearestNodeId];
        if (roadDist >= 1e8F)
            continue;

        const float bx = static_cast<float>(b.pathFromRoad.front().x());
        const float by = static_cast<float>(b.pathFromRoad.front().y());
        const float dx = bx - x;
        const float dy = by - y;
        const float directDistSq = dx * dx + dy * dy;

        const float pathDistance = (directDistSq < 45.0F * 45.0F
                                    && hasWalkableLineOfSight(x, y, bx, by, navGrid))
                                       ? std::sqrt(directDistSq)
                                       : (bestStartDist + roadDist + b.distToNode);

        if (pathDistance < bestDist) {
            bestDist = pathDistance;
            bestId = b.id;
        }
    }

    return bestId;
}

bool NavigationService::hasLineOfSight(
    float x1, float y1, float x2, float y2, const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Трассировать отрезок алгоритмом Брезенхэма и проверять флаг BlocksSight в NavGrid

    int ix1 = static_cast<int>(x1);
    int iy1 = static_cast<int>(y1);
    const int ix2 = static_cast<int>(x2);
    const int iy2 = static_cast<int>(y2);

    const int dx = std::abs(ix2 - ix1);
    const int dy = std::abs(iy2 - iy1);
    const int sx = (ix1 < ix2) ? 1 : -1;
    const int sy = (iy1 < iy2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (navGrid.inBounds(ix1, iy1) && navGrid.blocksSight(ix1, iy1, false))
            return false;

        if (ix1 == ix2 && iy1 == iy2)
            break;
        const int e2 = 2 * err;
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

bool NavigationService::hasWalkableLineOfSight(
    float x1, float y1, float x2, float y2, const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Проверить по Брезенхэму, что каждая клетка отрезка проходима в NavGrid

    int ix1 = static_cast<int>(x1);
    int iy1 = static_cast<int>(y1);
    const int ix2 = static_cast<int>(x2);
    const int iy2 = static_cast<int>(y2);

    const int dx = std::abs(ix2 - ix1);
    const int dy = std::abs(iy2 - iy1);
    const int sx = (ix1 < ix2) ? 1 : -1;
    const int sy = (iy1 < iy2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (!navGrid.isWalkable(static_cast<float>(ix1), static_cast<float>(iy1)))
            return false;

        if (ix1 == ix2 && iy1 == iy2)
            break;
        const int e2 = 2 * err;
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

bool NavigationService::hasLineOfSightIgnoringWalls(
    float x1, float y1, float x2, float y2, const NavGrid &navGrid) const
{
    // Задачи:
    // 1. Трассировать отрезок по Брезенхэму, блокируясь только корпусами зданий (BlocksZombieSight)

    int ix1 = static_cast<int>(x1);
    int iy1 = static_cast<int>(y1);
    const int ix2 = static_cast<int>(x2);
    const int iy2 = static_cast<int>(y2);

    const int dx = std::abs(ix2 - ix1);
    const int dy = std::abs(iy2 - iy1);
    const int sx = (ix1 < ix2) ? 1 : -1;
    const int sy = (iy1 < iy2) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (navGrid.inBounds(ix1, iy1) && navGrid.blocksSight(ix1, iy1, true))
            return false;

        if (ix1 == ix2 && iy1 == iy2)
            break;
        const int e2 = 2 * err;
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

bool NavigationService::isCityArea(float x, float y, const MapGenerator &generator) const
{
    const int gx = static_cast<int>(x);
    const int gy = static_cast<int>(y);
    const int w = generator.width();
    const int h = generator.height();
    if (gx < 0 || gx >= w || gy < 0 || gy >= h)
        return false;
    return generator.cityMask()[gy * w + gx] >= 1;
}

QVariantMap NavigationService::snapEvacPoint(float worldX,
                                             float worldY,
                                             float snapRadius,
                                             const std::vector<RoadNode> &nodes) const
{
    // Задачи:
    // 1. Найти ближайший эвакуационный узел в радиусе snapRadius для привязки курсора в QML

    QVariantMap res;
    res["snapped"] = false;
    res["x"] = worldX;
    res["y"] = worldY;

    float bestDist = snapRadius;
    for (const auto &node : nodes) {
        if (!node.isEvacExit)
            continue;
        const float d = static_cast<float>(
            std::hypot(node.pos.x() - worldX, node.pos.y() - worldY));
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

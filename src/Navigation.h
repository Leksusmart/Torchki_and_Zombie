#pragma once
#include "GameWorld.h"
#include <queue>

void GameWorld::buildRoadGraphCache()
{
    const auto &nodes = g_generator.nodes();
    const auto &edges = g_generator.edges();
    const int n = static_cast<int>(nodes.size());

    g_roadAdj.assign(n, {});
    for (const auto &e : edges) {
        float dist = static_cast<float>(
            distanceBetween(nodes[e.fromId].pos.x() - nodes[e.toId].pos.x(),
                            nodes[e.fromId].pos.y() - nodes[e.toId].pos.y()));
        g_roadAdj[e.fromId].push_back({e.toId, dist});
        g_roadAdj[e.toId].push_back({e.fromId, dist});
    }

    // 1. Заполняем пространственную сетку узлов дорог
    g_gridCols = (g_generator.width() + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE;
    g_gridRows = (g_generator.height() + GRID_CELL_SIZE - 1) / GRID_CELL_SIZE;
    g_nodeSpatialGrid.assign(g_gridCols * g_gridRows, {});

    for (int i = 0; i < n; ++i) {
        int cx = std::clamp(static_cast<int>(nodes[i].pos.x()) / GRID_CELL_SIZE, 0, g_gridCols - 1);
        int cy = std::clamp(static_cast<int>(nodes[i].pos.y()) / GRID_CELL_SIZE, 0, g_gridRows - 1);
        g_nodeSpatialGrid[cy * g_gridCols + cx].push_back(i);
    }

    // 2. Считаем матрицу кратчайших расстояний между всеми парами узлов (All-Pairs Dijkstra)
    g_allPairsDist.assign(n, std::vector<float>(n, 1e9F));
    using PQItem = std::pair<float, int>;

    for (int start = 0; start < n; ++start) {
        auto &dist = g_allPairsDist[start];
        dist[start] = 0.0F;

        std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;
        pq.push({0.0F, start});

        while (!pq.empty()) {
            auto [d, u] = pq.top();
            pq.pop();

            if (d > dist[u])
                continue;

            for (const auto &[v, weight] : g_roadAdj[u]) {
                if (dist[u] + weight < dist[v]) {
                    dist[v] = dist[u] + weight;
                    pq.push({dist[v], v});
                }
            }
        }
    }

    // 3. Заранее привязываем каждое здание к ближайшему видимому узлу дороги (если не привязано в MapGenerator)
    auto &buildings = const_cast<std::vector<BuildingObject> &>(g_generator.buildings());
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

        // Привязываем здание по точке примыкания его тропинки к дороге (pathFromRoad.front())
        float bx = static_cast<float>(b.pathFromRoad.front().x());
        float by = static_cast<float>(b.pathFromRoad.front().y());

        int bestNode = -1;
        float bestDist = 1e9F;

        for (int i = 0; i < n; ++i) {
            float nx = static_cast<float>(nodes[i].pos.x());
            float ny = static_cast<float>(nodes[i].pos.y());
            float d = distanceBetween(nx - bx, ny - by);
            if (d < bestDist && hasWalkableLineOfSight(nx, ny, bx, by)) {
                bestDist = d;
                bestNode = i;
            }
        }
        if (bestNode == -1) {
            bestNode = findNearestRoadNode(bx, by);
            if (bestNode != -1) {
                bestDist = static_cast<float>(
                    distanceBetween(nodes[bestNode].pos.x() - bx, nodes[bestNode].pos.y() - by));
            }
        }
        b.nearestNodeId = bestNode;
        b.distToNode = bestDist;
    }
}

std::vector<QPointF> GameWorld::buildPathBetween(float startPosX,
                                                 float startPosY,
                                                 float endPosX,
                                                 float endPosY) const
{
    return buildPathBetween(QPointF(startPosX, startPosY), QPointF(endPosX, endPosY));
}
std::vector<QPointF> GameWorld::buildPathBetween(QPointF startPos, QPointF endPos) const
{
    const auto &walkGrid = g_generator.walkGrid();
    const auto &buildings = g_generator.buildings();
    const auto &nodes = g_generator.nodes();
    int w = g_generator.width();
    int h = g_generator.height();

    // Вспомогательная лямбда: определяем ID здания по координатам (-1 если на улице)
    auto getBuildingId = [&](QPointF p) -> int {
        int gx = static_cast<int>(p.x());
        int gy = static_cast<int>(p.y());
        if (gx < 0 || gx >= w || gy < 0 || gy >= h)
            return -1;
        int val = walkGrid[gy * w + gx];
        return (val >= 10) ? (val - 10) : -1;
    };

    int startBId = getBuildingId(startPos);
    int endBId = getBuildingId(endPos);

    // СЛУЧАЙ 1: Обе точки в одном здании
    if (startBId != -1 && startBId == endBId) {
        if (hasWalkableLineOfSight(static_cast<float>(startPos.x()),
                                   static_cast<float>(startPos.y()),
                                   static_cast<float>(endPos.x()),
                                   static_cast<float>(endPos.y()))) {
            return {endPos};
        }
        for (const auto &b : buildings) {
            if (b.id == startBId) {
                return {QPointF(b.cx, b.cy), endPos};
            }
        }
    }

    std::vector<QPointF> fullPath;

    // СЛУЧАЙ 2: Старт внутри здания -> доходим до его двери (узел startBuilding->nearestNodeId)
    const BuildingObject *startBuilding = nullptr;
    if (startBId != -1) {
        for (const auto &b : buildings) {
            if (b.id == startBId) {
                startBuilding = &b;
                break;
            }
        }
        if (startBuilding && startBuilding->nearestNodeId != -1) {
            QPointF doorPos = nodes[startBuilding->nearestNodeId].pos;
            if (!hasWalkableLineOfSight(static_cast<float>(startPos.x()),
                                        static_cast<float>(startPos.y()),
                                        static_cast<float>(doorPos.x()),
                                        static_cast<float>(doorPos.y()))) {
                fullPath.emplace_back(startBuilding->cx, startBuilding->cy);
            }
            fullPath.push_back(doorPos);
        }
    }

    // СЛУЧАЙ 3: Финиш внутри здания -> конечный узел графа это дверь целевого здания
    const BuildingObject *endBuilding = nullptr;
    if (endBId != -1) {
        for (const auto &b : buildings) {
            if (b.id == endBId) {
                endBuilding = &b;
                break;
            }
        }
    }

    // Определяем стартовый и конечный узлы в едином графе (дороги + переулки)
    float sx = static_cast<float>(startPos.x());
    float sy = static_cast<float>(startPos.y());
    float ex = static_cast<float>(endPos.x());
    float ey = static_cast<float>(endPos.y());

    int startNode = (startBuilding && startBuilding->nearestNodeId != -1)
                        ? startBuilding->nearestNodeId
                        : findNearestRoadNode(sx, sy);

    int endNode = (endBuilding && endBuilding->nearestNodeId != -1) ? endBuilding->nearestNodeId
                                                                    : findNearestRoadNode(ex, ey);

    // Если обе точки на улице и близко по прямой без препятствий — идём напрямую
    if (!startBuilding && !endBuilding && distanceBetween(ex - sx, ey - sy) < 45.0F
        && hasWalkableLineOfSight(sx, sy, ex, ey)) {
        fullPath.push_back(endPos);
        return fullPath;
    }

    if (startNode == -1 || endNode == -1) {
        fullPath.push_back(endPos);
        return fullPath;
    }

    if (startNode == endNode) {
        if (fullPath.empty()) {
            fullPath.push_back(nodes[startNode].pos);
        }
        if (!endBuilding
            || distanceBetween(ex - static_cast<float>(nodes[endNode].pos.x()),
                               ey - static_cast<float>(nodes[endNode].pos.y()))
                   > 1.0F) {
            fullPath.push_back(endPos);
        }
        return fullPath;
    }

    // Поиск кратчайшего пути по единому графу (дороги + все колена переулков)
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

        for (const auto &[v, weight] : g_roadAdj[u]) {
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

    // Если стартовали из здания, узел двери (nodeChain[0]) мы уже добавили в fullPath
    size_t startIdx = (startBuilding && !nodeChain.empty() && nodeChain[0] == startNode) ? 1 : 0;

    // Если стартовали с улицы и уже прошли мимо первого узла вдоль первого ребра — пропускаем его
    if (!startBuilding && nodeChain.size() >= 2) {
        QPointF p0 = nodes[nodeChain[0]].pos;
        QPointF p1 = nodes[nodeChain[1]].pos;
        float dToFirst = distanceBetween(static_cast<float>(p0.x()) - sx,
                                         static_cast<float>(p0.y()) - sy);
        float segLen = distanceBetween(static_cast<float>(p1.x() - p0.x()),
                                       static_cast<float>(p1.y() - p0.y()));
        float dToSecond = distanceBetween(static_cast<float>(p1.x()) - sx,
                                          static_cast<float>(p1.y()) - sy);

        if (dToFirst < 3.0F
            || (dToSecond < segLen
                && hasWalkableLineOfSight(sx,
                                          sy,
                                          static_cast<float>(p1.x()),
                                          static_cast<float>(p1.y())))) {
            startIdx = 1;
        }
    }

    for (size_t i = startIdx; i < nodeChain.size(); ++i) {
        fullPath.push_back(nodes[nodeChain[i]].pos);
    }

    // Финальная точка (центр целевого здания или точка на улице/выходе)
    fullPath.push_back(endPos);

    return fullPath;
}

int GameWorld::findNearestRoadNode(float x, float y) const
{
    const auto &nodes = g_generator.nodes();
    int bestVisibleId = -1;
    float bestVisibleDist = 1e9f;

    int fallbackId = -1;
    float fallbackDist = 1e9f;

    for (size_t i = 0; i < nodes.size(); ++i) {
        float nx = static_cast<float>(nodes[i].pos.x());
        float ny = static_cast<float>(nodes[i].pos.y());
        float d = distanceBetween(nx - x, ny - y);

        if (d < fallbackDist) {
            fallbackDist = d;
            fallbackId = static_cast<int>(i);
        }

        if (d < bestVisibleDist && hasWalkableLineOfSight(x, y, nx, ny)) {
            bestVisibleDist = d;
            bestVisibleId = static_cast<int>(i);
        }
    }
    return (bestVisibleId != -1) ? bestVisibleId : fallbackId;
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

int GameWorld::findNearestAvailableBuilding(float x, float y, bool wantWorkplace) const
{
    const auto &buildings = g_generator.buildings();
    const auto &nodes = g_generator.nodes();
    if (buildings.empty() || nodes.empty() || g_allPairsDist.empty())
        return -1;

    // 1. Мгновенно ищем стартовый узел через пространственную сетку (3x3 ячейки вокруг юнита)
    int startNode = -1;
    float bestStartDist = 1e9F;

    int centerCx = std::clamp(static_cast<int>(x) / GRID_CELL_SIZE, 0, g_gridCols - 1);
    int centerCy = std::clamp(static_cast<int>(y) / GRID_CELL_SIZE, 0, g_gridRows - 1);

    for (int dy = -1; dy <= 1; ++dy) {
        int ny = centerCy + dy;
        if (ny < 0 || ny >= g_gridRows)
            continue;

        for (int dx = -1; dx <= 1; ++dx) {
            int nx = centerCx + dx;
            if (nx < 0 || nx >= g_gridCols)
                continue;

            for (int nodeIdx : g_nodeSpatialGrid[ny * g_gridCols + nx]) {
                float px = static_cast<float>(nodes[nodeIdx].pos.x());
                float py = static_cast<float>(nodes[nodeIdx].pos.y());
                float d = distanceBetween(px - x, py - y);
                if (d < bestStartDist && hasWalkableLineOfSight(x, y, px, py)) {
                    bestStartDist = d;
                    startNode = nodeIdx;
                }
            }
        }
    }

    // Резервный поиск, если юнит оказался в редкой зоне без узлов в радиусе 64м
    if (startNode == -1) {
        startNode = findNearestRoadNode(x, y);
        if (startNode != -1) {
            bestStartDist = static_cast<float>(
                distanceBetween(nodes[startNode].pos.x() - x, nodes[startNode].pos.y() - y));
        }
    }

    if (startNode == -1)
        return -1;

    // Берём готовую строку расстояний от startNode ко всем перекрёсткам карты
    const auto &distFromStart = g_allPairsDist[startNode];

    // 2. Линейный проход по зданиям без аллокаций и поиска путей
    int bestId = -1;
    float bestDist = 1e8F; // Порог отсечения недостижимых зданий

    for (const auto &b : buildings) {
        bool isWork = (b.type == BuildingType::Office || b.type == BuildingType::Factory);
        if (wantWorkplace != isWork)
            continue;
        if (b.reservedOccupants >= b.capacity)
            continue;

        if (b.health <= 0 || b.isBurning || b.isBurnedOut)
            continue;
        if (b.underAttackTimerSec > 0.0F)
            continue;
        if (b.nearestNodeId == -1 || b.pathFromRoad.empty())
            continue;

        float roadDist = distFromStart[b.nearestNodeId];
        if (roadDist >= 1e8F)
            continue;

        float bx = static_cast<float>(b.pathFromRoad.front().x());
        float by = static_cast<float>(b.pathFromRoad.front().y());
        float dx = bx - x;
        float dy = by - y;
        float directDistSq = dx * dx + dy * dy;

        float pathDistance;
        if (directDistSq < 45.0F * 45.0F && hasWalkableLineOfSight(x, y, bx, by)) {
            pathDistance = std::sqrt(directDistSq);
        } else {
            pathDistance = bestStartDist + roadDist + b.distToNode;
        }

        if (pathDistance < bestDist) {
            bestDist = pathDistance;
            bestId = b.id;
        }
    }

    return bestId;
}

bool GameWorld::hasLineOfSight(float x1, float y1, float x2, float y2) const
{
    const auto &walkGrid = g_generator.walkGrid();
    const auto &cityMask = g_generator.cityMask();
    int w = g_generator.width();
    int h = g_generator.height();

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
            if (cityMask[idx] == 2 && walkGrid[idx] == 0) {
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
bool GameWorld::hasWalkableLineOfSight(float x1, float y1, float x2, float y2) const
{
    // Задачи:
    // 1. Проверить по алгоритму Брезенхэма, что все клетки на отрезке проходимы (walkGrid != 0)
    // 2. Использовать для определения возможности движения напрямую без обхода по графу дорог

    const auto &walkGrid = g_generator.walkGrid();
    int w = g_generator.width();
    int h = g_generator.height();

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
        if (ix1 < 0 || ix1 >= w || iy1 < 0 || iy1 >= h)
            return false;

        if (walkGrid[iy1 * w + ix1] == 0)
            return false;

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

bool GameWorld::isWalkable(float x, float y) const
{
    int gx = static_cast<int>(x);
    int gy = static_cast<int>(y);
    int w = g_generator.width();
    int h = g_generator.height();
    if (gx < 0 || gx >= w || gy < 0 || gy >= h)
        return false;
    return g_generator.walkGrid()[gy * w + gx] != 0;
}

bool GameWorld::isCityArea(float x, float y) const
{
    int gx = static_cast<int>(x);
    int gy = static_cast<int>(y);
    int w = g_generator.width();
    int h = g_generator.height();
    if (gx < 0 || gx >= w || gy < 0 || gy >= h)
        return false;
    return g_generator.cityMask()[gy * w + gx] >= 1;
}

/*Вызывается из QML*/ QVariantMap GameWorld::snapEvacPoint(float worldX,
                                                           float worldY,
                                                           float snapRadius) const
{
    QVariantMap res;
    res["snapped"] = false;
    res["x"] = worldX;
    res["y"] = worldY;

    float bestDist = snapRadius;
    for (const auto &node : g_generator.nodes()) {
        if (!node.isEvacExit)
            continue;
        float d = static_cast<float>(distanceBetween(node.pos.x() - worldX, node.pos.y() - worldY));
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
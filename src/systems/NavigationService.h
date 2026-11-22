#pragma once
#include <QPointF>
#include <QVariantMap>
#include <utility>
#include <vector>
#include "Types.h"

class IDestructibleProvider;
class MapGenerator;
class NavGrid;

/**
 * @brief Сервис навигации, поиска пути и проверки линии видимости.
 *
 * Работает исключительно через абстрактную сетку NavGrid и интерфейс
 * IDestructibleProvider, не зная о конкретных типах препятствий (стенах, блокпостах).
 */
class NavigationService
{
public:
    static constexpr int GRID_CELL_SIZE = 64;

    /// Строит кэш графа дорог, пространственную сетку узлов, матрицу расстояний и привязку зданий.
    void buildGraphCache(MapGenerator &generator, const NavGrid &navGrid);

    /// Пересчитывает веса рёбер графа с учётом перекрывающих дороги препятствий.
    void updateRoadEdgeWeights(const std::vector<RoadNode> &nodes,
                               const std::vector<RoadEdge> &edges,
                               const IDestructibleProvider *obstacles,
                               bool (*segmentsIntersectFn)(QPointF, QPointF, QPointF, QPointF) = nullptr);

    /// Строит маршрут между двумя точками карты с помощью алгоритма Дейкстры.
    std::vector<QPointF> buildPathBetween(QPointF startPos,
                                          QPointF endPos,
                                          const MapGenerator &generator,
                                          const NavGrid &navGrid,
                                          bool ignoreDestructibleWalls = false) const;

    /// Удобная перегрузка buildPathBetween для скалярных координат.
    std::vector<QPointF> buildPathBetween(float startX,
                                          float startY,
                                          float endX,
                                          float endY,
                                          const MapGenerator &generator,
                                          const NavGrid &navGrid,
                                          bool ignoreDestructibleWalls = false) const;

    /// Находит ближайший узел дорожного графа (с приоритетом прямой видимости).
    int findNearestRoadNode(float x,
                            float y,
                            const std::vector<RoadNode> &nodes,
                            const NavGrid &navGrid) const;

    /// Ищет ближайшую проходимую точку в радиусе maxRadius вокруг (x, y).
    QPointF findNearestWalkable(float x, float y, float maxRadius, const NavGrid &navGrid) const;

    /// Ищет ближайшее доступное здание (рабочее или жилое) со свободными местами.
    int findNearestAvailableBuilding(float x,
                                     float y,
                                     bool wantWorkplace,
                                     const MapGenerator &generator,
                                     const NavGrid &navGrid) const;

    /// Проверяет прямую видимость с учётом стен зданий и временных препятствий.
    bool hasLineOfSight(float x1, float y1, float x2, float y2, const NavGrid &navGrid) const;

    /// Проверяет, что весь отрезок между точками проходим для движения.
    bool hasWalkableLineOfSight(float x1, float y1, float x2, float y2, const NavGrid &navGrid) const;

    /// Проверяет видимость, игнорируя разрушаемые стены (используется зомби и стрельбой солдат).
    bool hasLineOfSightIgnoringWalls(float x1,
                                     float y1,
                                     float x2,
                                     float y2,
                                     const NavGrid &navGrid) const;

    /// Проверяет, находится ли точка на территории городской застройки.
    bool isCityArea(float x, float y, const MapGenerator &generator) const;

    /// Привязывает координаты клика к ближайшему эвакуационному выходу в радиусе snapRadius.
    QVariantMap snapEvacPoint(float worldX,
                              float worldY,
                              float snapRadius,
                              const std::vector<RoadNode> &nodes) const;

    /// Возвращает список смежности графа дорог.
    const std::vector<std::vector<std::pair<int, float>>> &roadAdj() const { return roadAdj_; }

private:
    std::vector<std::vector<std::pair<int, float>>> roadAdj_;
    std::vector<std::vector<int>> nodeSpatialGrid_;
    int gridCols_ = 0;
    int gridRows_ = 0;
    std::vector<std::vector<float>> allPairsDist_;
};

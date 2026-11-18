#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

#include "Types.h"

class MapGenerator
{
public: // функции
    int width() const { return mapWidth; }
    int height() const { return mapHeight; }
    float roadWidth() const { return level.roadWidthMeters; }
    bool generateFromLevel(const LevelDefinition& newLevel);
    QColor waterColor() const { return mapOutborderColor; }
    const QImage& renderedBackground() const { return backgroundImage; }
    const std::vector<RoadNode>& nodes() const { return nodesList; }
    const std::vector<BuildingObject>& buildings() const { return buildingsList; }
    const std::vector<uint8_t>& cityMask() const { return cityMap; }
    const std::vector<RoadEdge>& edges() const { return edgesList; }
    const std::vector<EvacRoute>& evacRoutes() const { return evacRoutesList; }
    const std::vector<int16_t>& walkGrid() const { return walkMap; }

public: // переменные
    LevelDefinition level;

private: // функции
    void rasterizeRoads(float roadWidth);
    void generateBuildings();
    unsigned int generateBuilding(BuildingType type,
                                  std::vector<unsigned short int> settings,
                                  std::vector<uint8_t>& occupied);
    void removeBuilding(BuildingType type, int amountToRemove, std::vector<uint8_t>& occupied);
    bool tryPlaceBuilding(
        int x, int y, float bw, float bh, BuildingType type, std::vector<uint8_t>& occupied);
    void buildBuildingInteriorsAndPaths();

private: // переменные
    int mapWidth = 0;
    int mapHeight = 0;
    QColor mapOutborderColor = QColor(24, 34, 48);
    QImage image;
    QImage backgroundImage;
    std::vector<int16_t> walkMap; // 0 = стена/пустырь, 1 = дорога, 2 = тропинка, 10 + id = пол здания
    std::vector<uint8_t> cityMap; // 1 - застройка, 0 - нет
    std::vector<EvacRoute> evacRoutesList;
    std::vector<RoadNode> nodesList;
    std::vector<RoadEdge> edgesList;
    std::vector<BuildingObject> buildingsList;
};

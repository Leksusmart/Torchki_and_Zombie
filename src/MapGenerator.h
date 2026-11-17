#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

// Статическое описание уровня (не зависящее от уровня сложности)
struct LevelDefinition
{
    QString levelName = "Manhattan";
    QString mapPngPath = ":/Torchki_and_Zombie/levels/Level1.png";
    float roadWidthMeters = 13.0f;

    // Проценты плотности застройки (0..100%)
    int factoryChancePercent = 35; // Шанс спавна завода в крупном пятне
    int officeChancePercent = 45;  // Шанс спавна офиса
    int houseChancePercent = 100;  // Шанс спавна жилого дома в оставшихся местах

    // Названия для красных пикселей (#ED1C24) по порядку сканирования (сверху вниз, слева направо)
    QStringList evacNames = {"Мост верхний 1",
                             "Мост верхний 2",
                             "Мост нижний 1",
                             "Мост нижний 2",
                             "Мост нижний 3",
                             "Мост нижний 4",
                             "Мост нижний 5"};
};

struct EvacRoute
{
    int group; // 1 или 2
    QPointF startPos;
    QPointF endPos;
};

struct RoadNode
{
    int id;
    QPointF pos;
    bool isEvacExit = false;
    QString evacName; // Название выхода
};

struct RoadEdge
{
    int fromId;
    int toId;
};

enum class BuildingType : uint8_t {
    Residential = 0, // Жилой дом (базовый размер 1x)
    Office = 1,      // Офисное здание (размер 1x - 3x)
    Factory = 2      // Завод / промзона (размер 4x - 6x)
};

struct BuildingObject
{
    int id;
    BuildingType type = BuildingType::Residential;
    float cx, cy;
    float width, height;
    float angleRad;
    float area;

    int capacity = 0;
    int currentOccupants = 0;
    int hourlyMaxWorkers[24] = {0};
    int maxHealth = 100;
    int health = 100;

    float burnTimerSec = 0.0f;        // Таймер горения (area / 15.0f сек после падения HP до 0)
    bool isBurning = false;           // Сейчас охвачено пламенем
    bool isBurnedOut = false;         // Полностью обуглилось
    float underAttackTimerSec = 0.0f; // Таймер активной атаки зомби (3.0 сек после последнего удара)

    QPointF entrancePos;
    uint8_t r, g, b;
};

class MapGenerator
{
public:
    bool generateFromLevel(const LevelDefinition& levelDef);

    const QImage& renderedBackground() const { return m_backgroundImage; }
    const std::vector<uint8_t>& walkGrid() const { return m_walkGrid; }
    const std::vector<uint8_t>& cityMask() const { return m_cityMask; }
    const std::vector<RoadNode>& nodes() const { return m_nodes; }
    const std::vector<RoadEdge>& edges() const { return m_edges; }
    const std::vector<BuildingObject>& buildings() const { return m_buildings; }
    QColor waterColor() const { return m_waterColor; }

    int width() const { return m_width; }
    int height() const { return m_height; }
    float roadWidth() const { return m_roadWidth; }
    const std::vector<EvacRoute>& evacRoutes() const { return m_evacRoutes; }

private:
    std::vector<EvacRoute> m_evacRoutes;
    void rasterizeRoads(float roadWidth);
    void generateBuildings(const LevelDefinition& levelDef);
    bool tryPlaceBuilding(
        int x, int y, float bw, float bh, BuildingType type, std::vector<uint8_t>& occupied);
    QPointF findEntranceOnRoad(float cx, float cy) const;

    int m_width = 0;
    int m_height = 0;
    QColor m_waterColor = QColor(24, 34, 48);

    QImage m_sourceMask;
    QImage m_backgroundImage;
    std::vector<uint8_t> m_cityMask;
    std::vector<uint8_t> m_walkGrid;

    std::vector<RoadNode> m_nodes;
    std::vector<RoadEdge> m_edges;
    std::vector<BuildingObject> m_buildings;
    float m_roadWidth = 13.0f;
};

#pragma once
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

#include "Types.h"

/**
 * @brief Процедурный генератор городской карты из PNG-маски уровня.
 *
 * Строит граф дорог (MST + дополнительные перекрёстки без пересечений),
 * размещает здания (заводы, офисы, жилые дома), прокладывает тропинки к дверям
 * и формирует матрицы проходимости walkMap и видимости cityMap.
 */
class MapGenerator
{
public: // функции
    /// Возвращает ширину сгенерированной карты в пикселях/метрах.
    int width() const { return mapWidth; }

    /// Возвращает высоту сгенерированной карты в пикселях/метрах.
    int height() const { return mapHeight; }

    /// Возвращает ширину основных дорог в метрах.
    float roadWidth() const { return level.roadWidthMeters; }

    /// Генерирует карту, дорожную сеть, здания и интерьеры по описанию уровня.
    bool generateFromLevel(const LevelDefinition &newLevel);

    /// Возвращает цвет воды/фона за пределами города (из пикселя (0, 0) маски).
    QColor waterColor() const { return mapOutborderColor; }

    /// Возвращает итоговое фоновое изображение карты с дорогами и тропинками.
    const QImage &renderedBackground() const { return backgroundImage; }

    /// Возвращает список узлов дорожного графа (перекрёстки, тупики, выходы эвакуации).
    const std::vector<RoadNode> &nodes() const { return nodesList; }

    /// Возвращает список зданий (только для чтения).
    const std::vector<BuildingObject> &buildings() const { return buildingsList; }

    /// Возвращает изменяемый список зданий для обновления состояния в симуляции.
    std::vector<BuildingObject> &mutableBuildings() { return buildingsList; }

    /// Возвращает маску застройки (0 = вода/пустырь, 1 = город, 2 = корпус здания).
    const std::vector<uint8_t> &cityMask() const { return cityMap; }

    /// Возвращает список рёбер дорожного графа.
    const std::vector<RoadEdge> &edges() const { return edgesList; }

    /// Возвращает маршруты эвакуационных конвоев (группы 1 и 2).
    const std::vector<EvacRoute> &evacRoutes() const { return evacRoutesList; }

    /// Возвращает базовую сетку проходимости (0 = стена, 1 = дорога, 2 = тропинка, 10+id = пол).
    const std::vector<int16_t> &walkGrid() const { return walkMap; }

    /// Вычисляет евклидову длину вектора (x, y).
    float distanceBetween(float x, float y) const { return std::hypot(x, y); }

public: // переменные
    LevelDefinition level;

private: // функции
    /// Отрисовывает дороги на backgroundImage и помечает их в walkMap (= 1).
    void rasterizeRoads(float roadWidth);

    /// Размещает заводы, офисы и жилые дома в свободных кварталах с учётом пропорций уровня.
    void generateBuildings();

    /// Пытается разместить одно здание в точке (x, y), выравнивая его по ближайшей дороге.
    bool tryPlaceBuilding(
        int x, int y, float bw, float bh, BuildingType type, std::vector<uint8_t> &occupied);

    /// Заполняет полы зданий в walkMap, находит двери и прокладывает тропинки к дорогам.
    void buildBuildingInteriorsAndPaths();

private: // переменные
    int mapWidth = 0;
    int mapHeight = 0;
    QColor mapOutborderColor = QColor(24, 34, 48);
    QImage image;
    QImage backgroundImage;
    std::vector<int16_t> walkMap;
    std::vector<uint8_t> cityMap;
    std::vector<EvacRoute> evacRoutesList;
    std::vector<RoadNode> nodesList;
    std::vector<RoadEdge> edgesList;
    std::vector<BuildingObject> buildingsList;
};

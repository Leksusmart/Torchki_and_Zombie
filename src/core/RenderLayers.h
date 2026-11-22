#pragma once
#include <cstdint>
#include <vector>
#include "Types.h"

/**
 * @brief Фиксированный Z-порядок слоёв рендера сцены.
 *
 * Исключает жёсткую привязку Renderer::updatePaintNode к числовым индексам
 * дочерних узлов (0..7) при добавлении новых типов объектов.
 */
enum class RenderLayerId : uint8_t {
    Ground = 0,     ///< Наземные зоны (минные поля, маркеры на дороге)
    Buildings,      ///< Корпуса, крыши и полы зданий
    Obstacles,      ///< Карантинные стены, блокпосты, укрепления
    ObstacleCaps,   ///< Скруглённые торцы стен и круглые элементы препятствий
    Paths,          ///< Маршрут выбранного юнита
    Units,          ///< Жители, солдаты, зомби, пассажиры конвоя
    Overlays,       ///< Индикаторы здоровья и заполненности
    Particles,      ///< Трассеры пуль, искры, волны
    Count
};

/// Геометрия одного слоя рендера (прямоугольники и/или круги).
struct RenderLayerData
{
    std::vector<RenderQuad> quads;
    std::vector<RenderQuad> circles;
    int circleSegments = 12;
    bool isStaticMapLayer = false; ///< true = обновляется по сигналу mapUpdated, false = по unitsUpdated
};

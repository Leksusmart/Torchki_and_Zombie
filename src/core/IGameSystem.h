#pragma once
#include <array>
#include "RenderLayers.h"

class NavGrid;
struct WorldState;

/**
 * @brief Базовый интерфейс игровой подсистемы (стены, эвакуация, вертолёты, эффекты и т.д.).
 *
 * Позволяет GameWorld запускать обновление, растеризацию препятствий и сбор геометрии
 * для всех подключённых модулей через единый цикл без правок ядра при добавлении нового объекта.
 */
class IGameSystem
{
public:
    virtual ~IGameSystem() = default;

    /// Сбрасывает состояние подсистемы при старте или перезапуске уровня.
    virtual void onLevelReset(WorldState &state) { (void) state; }

    /// Выполняет шаг симуляции подсистемы за время dt (в секундах).
    virtual void update(float dt, WorldState &state) = 0;

    /// Записывает свои статические/динамические препятствия в общую навигационную сетку.
    virtual void applyToNavGrid(NavGrid &grid) const { (void) grid; }

    /// Добавляет геометрию подсистемы в соответствующие слои рендера.
    virtual void collectRenderData(
        std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const
    {
        (void) layers;
    }
};

#pragma once
#include <QPointF>
#include <QVariantList>
#include <vector>
#include "Types.h"
#include "core/IGameSystem.h"

class EffectsSystem;
class NavGrid;

/**
 * @brief Подсистема десантных вертолётов.
 *
 * Управляет подлётом вертолёта к точке высадки, зависанием, спавном отряда
 * десантников, отлётом за пределы карты и расчётом ETA для QML-оверлеев.
 */
class HelicopterSystem : public IGameSystem
{
public:
    /// Устанавливает указатель на EffectsSystem для визуализации волны при высадке.
    void setEffectsSystem(EffectsSystem *effects) { effects_ = effects; }

    /// Сбрасывает список активных вертолётов и данных рендера при старте уровня.
    void onLevelReset(WorldState &state) override;

    /// Обновляет фазы полёта всех вертолётов и формирует данные для QML.
    void update(float dt, WorldState &state) override;

    /// Вызывает десантный вертолёт в указанную точку карты.
    void callDropHelicopter(float targetX, float targetY);

    /// Возвращает список вертолётов для QML-делегатов.
    const QVariantList &helicoptersList() const { return helicoptersList_; }

    /// Возвращает список активных зон высадки с оставшимся временем прибытия.
    const QVariantList &dropZonesList() const { return dropZonesList_; }

    /// Возвращает true, если данные для QML обновились на последнем шаге.
    bool consumeChangedFlag();

private:
    /// Перемещает вертолёт к точке высадки с плавным торможением за 50 м до цели.
    void moveApproaching(DropHelicopter &heli, float dt);

    /// Удерживает вертолёт в точке зависания и высаживает отряд десантников.
    void processHovering(DropHelicopter &heli, float dt, WorldState &state);

    /// Уводит вертолёт по прямой за пределы карты; возвращает true по достижении 800 м.
    bool moveDeparting(DropHelicopter &heli, float dt);

    /// Создаёт отряд из 3 солдат вокруг точки dropPos.
    void spawnParatrooperSquad(QPointF dropPos, WorldState &state);

    /// Пересобирает QVariantList для отображения вертолётов и зон посадки в QML.
    void rebuildRenderData();

private:
    EffectsSystem *effects_ = nullptr;
    std::vector<DropHelicopter> helicopters_;
    QVariantList helicoptersList_;
    QVariantList dropZonesList_;
    bool changed_ = false;
};

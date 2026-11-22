#pragma once
#include <QPointF>
#include <QVariantList>
#include <vector>
#include "Types.h"
#include "core/IGameSystem.h"

class MapGenerator;
class NavigationService;

/**
 * @brief Подсистема эвакуации жителей.
 *
 * Управляет активными точками эвакуации, таймером посадки, движением конвоя
 * к выходу за карту, периодическим призывом ближайших жителей и отрисовкой пассажиров.
 */
class EvacuationSystem : public IGameSystem
{
public:
    /// Привязывает сервисы карты и навигации для построения маршрутов призыва жителей.
    void setDependencies(const MapGenerator *generator, const NavigationService *navigation);

    /// Сбрасывает активные эвакуации при перезапуске уровня.
    void onLevelReset(WorldState &state) override;

    /// Обновляет таймеры эвакуации, движение конвоев и периодический призыв жителей.
    void update(float dt, WorldState &state) override;

    /// Добавляет эвакуированных пассажиров конвоя в слой RenderLayerId::Units.
    void collectRenderData(
        std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const override;

    /// Запускает новую операцию эвакуации в узле nodeId, если она ещё не активна.
    bool startEvacuation(int nodeId,
                         QPointF evacPos,
                         const std::vector<EvacRoute> &routes,
                         WorldState &state);

    /// Пересобирает список всех эвакуационных выходов карты для отображения в QML.
    void rebuildEvacPoints(const std::vector<RoadNode> &nodes);

    /// Ищет активную эвакуацию по ID узла выхода (или возвращает nullptr).
    ActiveEvacuation *findEvacuation(int nodeId);

    /// Возвращает список активных эвакуаций для QML.
    const QVariantList &activeEvacuationsList() const { return activeEvacuationsList_; }

    /// Возвращает список всех точек эвакуации на карте для QML.
    const QVariantList &evacPointsList() const { return evacPointsList_; }

    /// Возвращает изменяемый список операций (для совместимости в переходный период).
    std::vector<ActiveEvacuation> &evacs() { return evacs_; }
    const std::vector<ActiveEvacuation> &evacs() const { return evacs_; }

    /// Возвращает true, если список активных эвакуаций изменился на последнем тике.
    bool consumeChangedFlag();

private:
    /// Пересобирает QVariantList активных зон эвакуации для QML.
    void rebuildActiveEvacuationsList();

    /// Периодически (1/15 юнитов за тик) призывает жителей в радиусе 250 м к активным точкам.
    void callNearbyCitizens(WorldState &state);

private:
    const MapGenerator *generator_ = nullptr;
    const NavigationService *navigation_ = nullptr;
    std::vector<ActiveEvacuation> evacs_;
    QVariantList activeEvacuationsList_;
    QVariantList evacPointsList_;
    int evacSlice_ = 0;
    bool changed_ = false;
};

#pragma once
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>
#include <vector>
#include "Types.h"
#include "core/IDestructible.h"
#include "core/IGameSystem.h"

class EffectsSystem;

namespace WallHelper {
struct RawSection
{
    float x1 = 0.0F, y1 = 0.0F;
    float x2 = 0.0F, y2 = 0.0F;
    float length = 0.0F;
    int cost = 0;
    int hp = 0;
    float buildTime = 0.0F;
};

/// Нарезает отрезок (x1,y1)->(x2,y2) на валидные проходимые участки улиц, обходя здания и уже стоящие стены.
std::vector<RawSection> extractWallSections(float x1,
                                            float y1,
                                            float x2,
                                            float y2,
                                            const std::vector<int16_t> &walkGrid,
                                            const NavGrid &navGrid);

/// Проверяет пересечение двух отрезков AB и CD.
bool segmentsIntersect(QPointF a, QPointF b, QPointF c, QPointF d);
} // namespace WallHelper

/**
 * @brief Подсистема карантинных стен.
 *
 * Реализует IGameSystem (обновление строительства/сноса, запись в NavGrid и рендер)
 * и IDestructibleProvider (обнаружение и получение урона от зомби без связности с ИИ).
 */
class WallSystem : public IGameSystem, public IDestructibleProvider
{
public:
    /// Устанавливает указатель на EffectsSystem для спавна частиц разрушения и ударов.
    void setEffectsSystem(EffectsSystem *effects) { effects_ = effects; }

    /// Сбрасывает все стены, выделение и геометрию при старте уровня.
    void onLevelReset(WorldState &state) override;

    /// Обновляет таймеры строительства, демонтажа, подсветки ударов и разрушения стен.
    void update(float dt, WorldState &state) override;

    /// Растеризует все активные (достроенные) стены в общую навигационную сетку NavGrid.
    void applyToNavGrid(NavGrid &grid) const override;

    /// Добавляет геометрию стен в слой RenderLayerId::Obstacles.
    void collectRenderData(
        std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const override;

    /// Реализация IDestructibleProvider: ищет ближайшую стену на пути в направлении (dirX, dirY).
    bool findBlockingObstacle(float x,
                              float y,
                              float dirX,
                              float dirY,
                              float maxDist,
                              DestructibleHit &outHit) const override;

    /// Реализация IDestructibleProvider: наносит урон стене и запускает визуальный отклик.
    bool applyDamage(int obstacleId, int damage, QPointF hitPos) override;

    /// Возвращает данные предпросмотра стены для QML при протягивании мышью.
    QVariantMap previewWall(float x1,
                            float y1,
                            float x2,
                            float y2,
                            double budget,
                            const std::vector<int16_t> &walkGrid,
                            const NavGrid &navGrid) const;

    /// Проверяет, можно ли разместить хотя бы одну секцию стены по отрезку.
    bool canPlaceWallSegment(float x1,
                             float y1,
                             float x2,
                             float y2,
                             const std::vector<int16_t> &walkGrid,
                             const NavGrid &navGrid) const;

    /// Размещает секции стены вдоль отрезка (x1,y1)->(x2,y2) и списывает стоимость из бюджета.
    bool placeWall(float x1,
                   float y1,
                   float x2,
                   float y2,
                   const std::vector<int16_t> &walkGrid,
                   WorldState &state);

    /// Выбирает стену по мировым координатам клика и обновляет selectedWallInfo.
    bool selectWallAt(float worldX, float worldY);

    /// Запускает платный демонтаж стены (200 ₽, 10 секунд).
    bool startWallTeardown(int wallId, WorldState &state);

    /// Проверяет, пересекает ли отрезок (a, b) какую-либо активную стену.
    bool blocksSegment(QPointF a, QPointF b) const;

    const QVariantList &progressBarsList() const { return wallProgressBars_; }
    const QVariantMap &selectedWallInfo() const { return selectedWallInfo_; }
    const std::vector<QuarantineWall> &walls() const { return walls_; }
    const std::vector<RenderQuad> &geometry() const { return geometry_; }

    bool consumeMapDirtyFlag();
    bool consumeOverlaysDirtyFlag();
    bool consumeSelectionDirtyFlag();

private:
    /// Пересобирает геометрию тела и полосок HP всех стен.
    void rebuildGeometry();

    /// Пересобирает QVariantList прогресс-баров строительства и демонтажа для QML.
    void rebuildOverlays();

    /// Обновляет QVariantMap информации о текущей выбранной стене.
    void updateSelectedWallInfo();

    /// Выталкивает юнитов за пределы только что достроенной стены.
    void pushUnitsOutOfWall(const QuarantineWall &wall, std::vector<Unit> &units) const;

private:
    EffectsSystem *effects_ = nullptr;
    std::vector<QuarantineWall> walls_;
    std::vector<RenderQuad> geometry_;
    QVariantList wallProgressBars_;
    QVariantMap selectedWallInfo_;
    int selectedWallId_ = -1;
    int nextWallId_ = 1;

    bool mapDirty_ = false;
    bool overlaysDirty_ = false;
    bool selectionDirty_ = false;
};

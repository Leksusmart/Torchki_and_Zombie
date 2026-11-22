#pragma once
#include <cstdint>
#include <vector>
#include "Types.h"
#include "core/IGameSystem.h"

/**
 * @brief Подсистема визуальных эффектов (трассеры пуль, искры, ударные волны).
 *
 * Управляет жизненным циклом частиц EffectParticle и формирует геометрию
 * для слоя RenderLayerId::Particles.
 */
class EffectsSystem : public IGameSystem
{
public:
    /// Сбрасывает все активные частицы при перезапуске уровня.
    void onLevelReset(WorldState &state) override;

    /// Обновляет позиции, прозрачность и время жизни всех частиц за шаг dt.
    void update(float dt, WorldState &state) override;

    /// Записывает геометрию эффектов в слой RenderLayerId::Particles.
    void collectRenderData(
        std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const override;

    /// Создаёт линию-трассер пули от (x1, y1) до (x2, y2) и искры в точке попадания.
    void spawnBulletTracer(float x1, float y1, float x2, float y2);

    /// Создаёт расширяющуюся кольцевую волну с центром в (x, y).
    void spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b);

    /// Создаёт всплеск из count разлетающихся искр в точке (x, y).
    void spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b);

    /// Возвращает актуальный буфер геометрии эффектов (для совместимости с фасадом GameWorld).
    const std::vector<RenderQuad> &geometry() const { return geometry_; }

private:
    std::vector<EffectParticle> particles_;
    std::vector<RenderQuad> geometry_;
};

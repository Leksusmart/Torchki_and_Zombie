#include "systems/EffectsSystem.h"
#include <QRandomGenerator>
#include <cmath>

void EffectsSystem::onLevelReset(WorldState &state)
{
    // Задачи:
    // 1. Очистить список активных частиц и буфер геометрии при старте уровня
    (void) state;
    particles_.clear();
    geometry_.clear();
}

void EffectsSystem::spawnBulletTracer(float x1, float y1, float x2, float y2)
{
    // Задачи:
    // 1. Создать короткоживущую линию-трассер от стрелка к цели
    // 2. Добавить искры попадания в конечной точке

    // 1. Линия-трассер пули
    EffectParticle p;
    p.x = x1;
    p.y = y1;
    p.x2 = x2;
    p.y2 = y2;
    p.size = 1.1F;
    p.lifeSec = 0.09F;
    p.maxLifeSec = 0.09F;
    p.r = 255;
    p.g = 240;
    p.b = 140;
    p.a = 240;
    p.isLine = true;
    particles_.push_back(p);

    // 2. Искры попадания в точке цели
    spawnBurstParticles(x2, y2, 3, 255, 180, 60);
}

void EffectsSystem::spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b)
{
    // Задачи:
    // 1. Создать расширяющуюся частицу-волну до диаметра maxRadius * 2

    EffectParticle p;
    p.x = x;
    p.y = y;
    p.vx = 0.0F;
    p.vy = 0.0F;
    p.size = 4.0F;
    p.endSize = maxRadius * 2.0F;
    p.lifeSec = 0.45F;
    p.maxLifeSec = 0.45F;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 140;
    p.isLine = false;
    particles_.push_back(p);
}

void EffectsSystem::spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b)
{
    // Задачи:
    // 1. Сгенерировать count частиц со случайным углом и скоростью разлёта

    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < count; ++i) {
        const float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        const float speed = 15.0F + static_cast<float>(rng->generateDouble() * 25.0);

        EffectParticle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.size = 2.0F;
        p.endSize = 0.5F;
        p.lifeSec = 0.25F + static_cast<float>(rng->generateDouble() * 0.2);
        p.maxLifeSec = p.lifeSec;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a = 255;
        p.isLine = false;
        particles_.push_back(p);
    }
}

void EffectsSystem::update(float dt, WorldState &state)
{
    // Задачи:
    // 1. Очистить буфер геометрии предыдущего кадра
    // 2. Уменьшить время жизни частиц и удалить истёкшие (через swap-and-pop)
    // 3. Сформировать RenderQuad с плавным затуханием альфа-канала

    (void) state;
    geometry_.clear();

    for (size_t i = 0; i < particles_.size();) {
        auto &p = particles_[i];
        p.lifeSec -= dt;
        if (p.lifeSec <= 0.0F) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }

        const float t = 1.0F - (p.lifeSec / p.maxLifeSec);
        const uint8_t alpha = static_cast<uint8_t>(p.a * (1.0F - t));

        if (p.isLine) {
            const float dx = p.x2 - p.x;
            const float dy = p.y2 - p.y;
            const float len = std::hypot(dx, dy);
            const float angle = std::atan2(dy, dx);
            geometry_.push_back(
                {(p.x + p.x2) * 0.5F, (p.y + p.y2) * 0.5F, len, p.size, angle, p.r, p.g, p.b, alpha});
        } else {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            const float curSize = p.size + (p.endSize - p.size) * t;
            geometry_.push_back({p.x, p.y, curSize, curSize, 0.0F, p.r, p.g, p.b, alpha});
        }
        ++i;
    }
}

void EffectsSystem::collectRenderData(
    std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers) const
{
    // Задачи:
    // 1. Скопировать геометрию частиц в слой RenderLayerId::Particles
    auto &layer = layers[static_cast<size_t>(RenderLayerId::Particles)];
    layer.quads.insert(layer.quads.end(), geometry_.begin(), geometry_.end());
}

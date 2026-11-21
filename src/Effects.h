#pragma once
#include "GameWorld.h"

#include <QRandomGenerator>

void GameWorld::spawnBulletTracer(float x1, float y1, float x2, float y2)
{
    EffectParticle p;
    p.x = x1;
    p.y = y1;
    p.x2 = x2;
    p.y2 = y2;
    p.size = 1.1f; // Толщина трассера
    p.lifeSec = 0.09f;
    p.maxLifeSec = 0.09f;
    p.r = 255;
    p.g = 240;
    p.b = 140;
    p.a = 240;
    p.isLine = true;
    g_particles.push_back(p);

    // Искры попадания в точке зомби
    spawnBurstParticles(x2, y2, 3, 255, 180, 60);
}

void GameWorld::spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b)
{
    EffectParticle p;
    p.x = x;
    p.y = y;
    p.vx = 0.0f;
    p.vy = 0.0f;
    p.size = 4.0f;
    p.endSize = maxRadius * 2.0f;
    p.lifeSec = 0.45f;
    p.maxLifeSec = 0.45f;
    p.r = r;
    p.g = g;
    p.b = b;
    p.a = 140;
    p.isLine = false;
    g_particles.push_back(p);
}

void GameWorld::spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b)
{
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < count; ++i) {
        float angle = static_cast<float>(rng->generateDouble() * 6.28318);
        float speed = 15.0f + static_cast<float>(rng->generateDouble() * 25.0);
        EffectParticle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.size = 2.0f;
        p.endSize = 0.5f;
        p.lifeSec = 0.25f + static_cast<float>(rng->generateDouble() * 0.2);
        p.maxLifeSec = p.lifeSec;
        p.r = r;
        p.g = g;
        p.b = b;
        p.a = 255;
        p.isLine = false;
        g_particles.push_back(p);
    }
}

void GameWorld::updateEffects(float dt)
{
    g_effectsGeometry.clear();
    // Обновляем пули, волны и искры
    for (size_t i = 0; i < g_particles.size();) {
        auto &p = g_particles[i];
        p.lifeSec -= dt;
        if (p.lifeSec <= 0.0f) {
            g_particles[i] = g_particles.back();
            g_particles.pop_back();
            continue;
        }

        float t = 1.0f - (p.lifeSec / p.maxLifeSec);
        uint8_t alpha = static_cast<uint8_t>(p.a * (1.0f - t));

        if (p.isLine) {
            float dx = p.x2 - p.x;
            float dy = p.y2 - p.y;
            float len = distanceBetween(dx, dy);
            float angle = std::atan2(dy, dx);
            g_effectsGeometry.push_back(
                {(p.x + p.x2) * 0.5f, (p.y + p.y2) * 0.5f, len, p.size, angle, p.r, p.g, p.b, alpha});
        } else {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            float curSize = p.size + (p.endSize - p.size) * t;
            g_effectsGeometry.push_back({p.x, p.y, curSize, curSize, 0.0f, p.r, p.g, p.b, alpha});
        }
        ++i;
    }
}
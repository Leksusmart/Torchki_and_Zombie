#include "ai/UnitMovement.h"
#include <QRandomGenerator>
#include <cmath>
#include "MapGenerator.h"
#include "core/NavGrid.h"
#include "systems/NavigationService.h"

void UnitMovement::applyFireAvoidanceAndDamage(Unit &u, float dt)
{
    (void) u;
    (void) dt;
    // Огонь служит только визуальным эффектом разрушенного здания и не наносит урон
}

void UnitMovement::applyWallRepulsion(Unit &u, float dt, float roadWidth, const NavGrid &navGrid)
{
    // Задачи:
    // 1. Прощупать 4 соседние точки вокруг юнита на дистанции probeDist
    // 2. Сформировать вектор отталкивания и скорректировать скорость с сохранением её модуля

    const float probeDist = (u.insideBuildingId != -1) ? 1.2F : (roadWidth * 0.125F);
    const float pushStrength = (u.insideBuildingId != -1) ? 12.0F : (roadWidth * 2.2F);

    float pushX = 0.0F;
    float pushY = 0.0F;
    if (!navGrid.isWalkable(u.x + probeDist, u.y))
        pushX -= pushStrength;
    if (!navGrid.isWalkable(u.x - probeDist, u.y))
        pushX += pushStrength;
    if (!navGrid.isWalkable(u.x, u.y + probeDist))
        pushY -= pushStrength;
    if (!navGrid.isWalkable(u.x, u.y - probeDist))
        pushY += pushStrength;

    if (pushX != 0.0F || pushY != 0.0F) {
        const float targetSpeed = std::hypot(u.vx, u.vy);
        u.vx += pushX * dt;
        u.vy += pushY * dt;
        const float newSpeed = std::hypot(u.vx, u.vy);
        if (newSpeed > 0.01F && targetSpeed > 0.01F) {
            u.vx = (u.vx / newSpeed) * targetSpeed;
            u.vy = (u.vy / newSpeed) * targetSpeed;
        }
    }
}

void UnitMovement::step(Unit &u,
                        float dt,
                        const MapGenerator &generator,
                        const NavigationService &nav,
                        const NavGrid &navGrid)
{
    // Задачи:
    // 1. Применить избегание огня и отталкивание от стен
    // 2. Снизить скорость внутри помещений в состоянии покоя/работы
    // 3. Выполнить шаг по X и Y со скольжением вдоль препятствий
    // 4. Проверить анти-застревание и при необходимости перестроить маршрут

    auto *rng = QRandomGenerator::global();
    const float roadW = generator.roadWidth();

    // 1. Отталкивание от бордюров и стен
    applyFireAvoidanceAndDamage(u, dt);
    applyWallRepulsion(u, dt, roadW, navGrid);

    // 2. Замедление внутри здания
    if (u.insideBuildingId != -1
        && (u.state == UnitState::AtHome || u.state == UnitState::Working)) {
        const float speedFactor = generator.level.speedInBuildingPercent / 100.0F;
        u.vx *= speedFactor;
        u.vy *= speedFactor;
    }

    // 3. Шаг со скольжением вдоль стен
    const float nextX = u.x + u.vx * dt;
    const float nextY = u.y + u.vy * dt;
    bool movedX = false;
    bool movedY = false;

    if (navGrid.isWalkable(nextX, u.y)) {
        u.x = nextX;
        movedX = true;
    }
    if (navGrid.isWalkable(u.x, nextY)) {
        u.y = nextY;
        movedY = true;
    }

    if (!movedX && std::abs(u.vx) > 0.1F) {
        const float slideStep = (u.vy >= 0.0F) ? 1.5F : -1.5F;
        if (navGrid.isWalkable(nextX, u.y + slideStep)) {
            u.x = nextX;
            u.y += slideStep;
            movedX = true;
        } else if (navGrid.isWalkable(nextX, u.y - slideStep)) {
            u.x = nextX;
            u.y -= slideStep;
            movedX = true;
        }
    }
    if (!movedY && std::abs(u.vy) > 0.1F) {
        const float slideStep = (u.vx >= 0.0F) ? (roadW * 0.12F) : (-roadW * 0.12F);
        if (navGrid.isWalkable(u.x + slideStep, nextY)) {
            u.x += slideStep;
            u.y = nextY;
            movedY = true;
        } else if (navGrid.isWalkable(u.x - slideStep, nextY)) {
            u.x -= slideStep;
            u.y = nextY;
            movedY = true;
        }
    }
    if (!movedX && !movedY && u.type != UnitType::Zombie) {
        const float escapeAngle = static_cast<float>(rng->generateDouble() * 6.28318);
        float currentSpeed = std::hypot(u.vx, u.vy);
        if (currentSpeed < 5.0F)
            currentSpeed = BASE_CITIZEN_SPEED;
        u.vx = std::cos(escapeAngle) * currentSpeed;
        u.vy = std::sin(escapeAngle) * currentSpeed;
    }

    // 4. Анти-застревание (не сбрасываем путь зомби, если он целенаправленно крушит барьер)
    const bool zombieAssaultingWall = (u.type == UnitType::Zombie
                                       && std::hypot(u.vx, u.vy) < 0.1F);
    if (!u.waypoints.empty() && !zombieAssaultingWall) {
        u.stuckTimerSec += dt;
        if (u.stuckTimerSec >= 1.2F) {
            const float moved = static_cast<float>(
                std::hypot(u.x - u.lastCheckPos.x(), u.y - u.lastCheckPos.y()));
            u.lastCheckPos = QPointF(u.x, u.y);
            u.stuckTimerSec = 0.0F;

            if (moved < 2.0F) {
                if (u.repathCount < 2) {
                    u.repathCount++;
                    u.waypoints = nav.buildPathBetween(QPointF(u.x, u.y),
                                                       u.waypoints.back(),
                                                       generator,
                                                       navGrid);
                } else {
                    u.waypoints.clear();
                    u.repathCount = 0;
                }
            } else {
                u.repathCount = 0;
            }
        }
    } else {
        u.stuckTimerSec = 0.0F;
        u.lastCheckPos = QPointF(u.x, u.y);
    }
}

#pragma once
#include <QPointF>

/// Результат проверки столкновения юнита с разрушаемым препятствием на пути.
struct DestructibleHit
{
    int obstacleId = -1;       ///< Идентификатор препятствия внутри его системы
    float distToSurface = 0.0F;///< Расстояние до поверхности препятствия
    QPointF hitPoint;          ///< Ближайшая точка контакта (для спавна искр/частиц)
};

/**
 * @brief Контракт источника разрушаемых препятствий (стены, блокпосты, укрепления).
 *
 * Позволяет ИИ (например, ZombieAI) атаковать любое перекрывающее путь препятствие,
 * не зная о его конкретном классе и внутренней структуре хранения.
 */
class IDestructibleProvider
{
public:
    virtual ~IDestructibleProvider() = default;

    /// Ищет ближайшее разрушаемое препятствие впереди по вектору (dirX, dirY) на дистанции maxDist.
    virtual bool findBlockingObstacle(
        float x, float y, float dirX, float dirY, float maxDist, DestructibleHit &outHit) const = 0;

    /// Наносит урон препятствию по его ID и возвращает true, если урон применён.
    virtual bool applyDamage(int obstacleId, int damage, QPointF hitPos) = 0;
};

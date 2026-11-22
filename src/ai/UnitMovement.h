#pragma once
#include "Types.h"

class MapGenerator;
class NavGrid;
class NavigationService;

/**
 * @brief Общая физика перемещения юнитов и предотвращения застревания.
 *
 * Выполняет отталкивание от бордюров/стен, замедление внутри помещений,
 * шаг перемещения со скольжением вдоль препятствий и перестроение маршрута при застревании.
 */
class UnitMovement
{
public:
    static constexpr float BASE_CITIZEN_SPEED = 20.0F;
    static constexpr float RUN_SPEED = 30.0F;
    static constexpr float ZOMBIE_WANDER_SPEED = BASE_CITIZEN_SPEED * 0.5F;
    static constexpr float ZOMBIE_CHASE_SPEED = RUN_SPEED * 1.15F;

    /// Выполняет один шаг физики перемещения юнита со скольжением и анти-застреванием.
    static void step(Unit &u,
                     float dt,
                     const MapGenerator &generator,
                     const NavigationService &nav,
                     const NavGrid &navGrid);

    /// Отталкивает юнита от ближайших непроходимых границ (бордюров и стен).
    static void applyWallRepulsion(Unit &u, float dt, float roadWidth, const NavGrid &navGrid);

    /// Зарезервировано для эффектов/урона от горящих зданий.
    static void applyFireAvoidanceAndDamage(Unit &u, float dt);
};

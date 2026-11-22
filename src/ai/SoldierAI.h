#pragma once
#include "Types.h"
#include "core/NavGrid.h"
#include <vector>

class EffectsSystem;
class MapGenerator;
class NavigationService;
class UnitSpatialGrid;
struct WorldState;

/**
 * @brief ИИ десантников (солдат).
 *
 * Управляет сроком службы (90 с), одиночной эвакуацией к мосту с уклонением от зомби,
 * прицельной стрельбой по зомби и инфицированным, преследованием и патрулированием.
 */
class SoldierAI
{
public:
    /// Обновляет поведение солдата на текущем шаге симуляции.
    static void updateBehavior(Unit &u,
                               float dt,
                               WorldState &state,
                               const std::vector<BuildingObject> &buildings,
                               const UnitSpatialGrid &grid,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EffectsSystem &effects);

private:
    /// Контролирует таймер службы (90 с) и ведёт отступающего солдата к безопасному выходу/мосту.
    static bool updateEvacuation(Unit &u,
                                 float dt,
                                 WorldState &state,
                                 const std::vector<BuildingObject> &buildings,
                                 const UnitSpatialGrid &grid,
                                 const MapGenerator &generator,
                                 const NavigationService &nav,
                                 EffectsSystem &effects);

    /// Двигает солдата по прямой от начала моста до конца карты.
    static bool moveOnEvacBridge(Unit &u, float dt);

    /// Выбирает наиболее безопасный и близкий эвакуационный выход с учётом зомби.
    static int findSafestEvacExit(const Unit &u,
                                  const WorldState &state,
                                  const UnitSpatialGrid &grid,
                                  const MapGenerator &generator,
                                  const NavigationService &nav);

    /// Смешивает курс эвакуации с вектором бокового обхода видимых зомби.
    static void applyEvacAvoidance(Unit &u,
                                   const WorldState &state,
                                   const UnitSpatialGrid &grid,
                                   const NavigationService &nav);

    /// Ищет ближайшую цель в радиусе огня (35 м): сначала зомби, затем инфицированных.
    static int findFireTarget(const Unit &u,
                              const WorldState &state,
                              const UnitSpatialGrid &grid,
                              const NavigationService &nav);

    /// Производит выстрел по цели с учётом перезарядки и создаёт трассер/частицы.
    static void fireAtTarget(Unit &soldier,
                             size_t targetIdx,
                             WorldState &state,
                             EffectsSystem &effects);

    /// Корректирует дистанцию до видимой цели в бою.
    static void adjustCombatPosition(Unit &u, float targetX, float targetY, float dist);

    /// Ищет глобальную цель для преследования (зомби в 300 м, иначе инфицированные).
    static int findHuntTarget(const Unit &u, const WorldState &state, const UnitSpatialGrid &grid);

    /// Строит и обновляет маршрут преследования до цели.
    static void routeToTarget(Unit &u,
                              const Unit &target,
                              const MapGenerator &generator,
                              const NavigationService &nav,
                              const NavGrid &navGrid);

    /// Двигает солдата по текущему списку waypoints.
    static bool followWaypoints(Unit &u, const MapGenerator &generator);

    /// Патрулирует по случайным дорожным узлам, если целей нет.
    static void patrol(Unit &u,
                       const std::vector<BuildingObject> &buildings,
                       const MapGenerator &generator,
                       const NavigationService &nav,
                       const NavGrid &navGrid);
};

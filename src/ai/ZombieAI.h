#pragma once
#include <vector>
#include "Types.h"

class EffectsSystem;
class IDestructibleProvider;
class MapGenerator;
class NavigationService;
class UnitSpatialGrid;
struct WorldState;

/**
 * @brief ИИ зомби.
 *
 * Управляет поиском ближайших жертв, преследованием (в том числе по следу),
 * атакой любых разрушаемых препятствий через IDestructibleProvider,
 * штурмом занятых зданий и свободным бродяжничеством.
 */
class ZombieAI
{
public:
    /// Обновляет поведение зомби на текущем шаге симуляции.
    static void updateBehavior(Unit &u,
                               float dt,
                               WorldState &state,
                               std::vector<BuildingObject> &buildings,
                               const UnitSpatialGrid &grid,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               IDestructibleProvider *obstacles,
                               bool &buildingsVisualDirty,
                               bool &workplaceLabelsDirty);

private:
    /// Ищет ближайшую видимую жертву в соседних ячейках пространственной сетки.
    static int findBestTarget(const Unit &u,
                              const WorldState &state,
                              const UnitSpatialGrid &grid,
                              const NavigationService &nav);

    /// Проверяет наличие разрушаемого препятствия впереди и атакует его через IDestructibleProvider.
    static bool attackBlockingObstacleAhead(Unit &u, IDestructibleProvider *obstacles);

    /// Ищет ближайшее занятое здание и атакует его дверь (или поджигает при 0 HP).
    static bool tryAssaultBuilding(Unit &u,
                                   WorldState &state,
                                   std::vector<BuildingObject> &buildings,
                                   const UnitSpatialGrid &grid,
                                   const NavigationService &nav,
                                   IDestructibleProvider *obstacles,
                                   bool &buildingsVisualDirty,
                                   bool &workplaceLabelsDirty);
};

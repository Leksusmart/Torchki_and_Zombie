#pragma once
#include "Types.h"
#include "core/NavGrid.h"
#include <vector>

class EvacuationSystem;
class MapGenerator;
class NavigationService;
class UnitSpatialGrid;
struct WorldState;

/**
 * @brief ИИ мирных и вакцинированных жителей.
 *
 * Управляет реакцией на угрозы (зомби, пожар, паника толпы), эвакуацией,
 * суточным расписанием (работа/дом/прогулка) и процессом заражения.
 */
class CitizenAI
{
public:
    /// Обновляет поведение жителя на текущем шаге симуляции.
    static void updateBehavior(Unit &u,
                               size_t unitIndex,
                               float dt,
                               WorldState &state,
                               std::vector<BuildingObject> &buildings,
                               const UnitSpatialGrid &grid,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EvacuationSystem &evacSys,
                               bool &statsDirty);

    /// Заражает жителя (мгновенное превращение в зомби или запуск инкубации).
    static void infectUnit(Unit &u, std::vector<Unit> &allUnits, bool immediateZombie = false);

private:
    /// Обрабатывает поведение жителя внутри здания (пожар, зомби, атака двери, расписание).
    static void updateIndoorBehavior(Unit &u,
                                     float dt,
                                     WorldState &state,
                                     std::vector<BuildingObject> &buildings,
                                     const UnitSpatialGrid &grid,
                                     const MapGenerator &generator,
                                     const NavigationService &nav,
                                     EvacuationSystem &evacSys,
                                     bool &statsDirty);

    /// Обрабатывает поведение жителя на улице (обнаружение зомби, паника толпы, расписание).
    static void updateOutdoorBehavior(Unit &u,
                                      size_t unitIndex,
                                      float dt,
                                      WorldState &state,
                                      std::vector<BuildingObject> &buildings,
                                      const UnitSpatialGrid &grid,
                                      const MapGenerator &generator,
                                      const NavigationService &nav,
                                      EvacuationSystem &evacSys,
                                      bool &statsDirty);

    /// Ведёт жителя по суточному расписанию, прогулкам и эвакуации.
    static void updateSchedule(Unit &u,
                               WorldState &state,
                               std::vector<BuildingObject> &buildings,
                               const MapGenerator &generator,
                               const NavigationService &nav,
                               EvacuationSystem &evacSys,
                               bool &statsDirty);

    /// Обрабатывает движение к эвакуационному выходу с уклонением от зомби и посадку.
    static void handleEvacuationMove(Unit &u,
                                     WorldState &state,
                                     const MapGenerator &generator,
                                     const NavigationService &nav,
                                     EvacuationSystem &evacSys,
                                     bool &statsDirty);

    /// Переводит жителя в новое состояние по истечении stateTimerSec.
    static void handleScheduleTransition(Unit &u,
                                         bool shouldBeAtWork,
                                         bool isInsideBuilding,
                                         std::vector<BuildingObject> &buildings,
                                         const MapGenerator &generator,
                                         const NavigationService &nav,
                                         const NavGrid &navGrid);
};

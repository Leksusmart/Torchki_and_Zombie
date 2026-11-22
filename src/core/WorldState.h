#pragma once
#include <vector>
#include "NavGrid.h"
#include "Types.h"

/**
 * @brief Разделяемое состояние симуляции мира.
 *
 * Передаётся подсистемам по ссылке вместо хранения глобальных полей g_*
 * внутри монолита GameWorld.
 */
struct WorldState
{
    std::vector<Unit> units;
    std::vector<BuildingObject> buildings;
    NavGrid navGrid;

    double budget = 300.0;
    double incomePerSec = 0.0;
    float elapsedSimSec = 0.0F;
    float timeOfDayHours = 8.0F;
    int dayNumber = 1;
    int speedMultiplier = 1;
    bool paused = true;

    int nextUnitId = 1;
    int citizensCount = 0;
    int infectedCount = 0;
    int zombiesCount = 0;
    int eliminatedCount = 0;
    int evacuatedTotalCount = 0;
    int lostCount = 0;
};

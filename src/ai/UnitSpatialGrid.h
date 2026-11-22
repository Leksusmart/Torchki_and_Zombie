#pragma once
#include <algorithm>
#include <vector>
#include "Types.h"

/**
 * @brief Пространственная сетка юнитов и зданий для быстрого поиска ближайших целей.
 *
 * Разбивает карту на ячейки размером UNIT_GRID_CELL (80 м), чтобы зомби, солдаты
 * и жители проверяли только соседние 3x3 или 5x5 ячеек вместо полного перебора.
 */
class UnitSpatialGrid
{
public:
    static constexpr int CELL_SIZE = 80;

    /// Инициализирует размеры сетки и размещает статичные здания по ячейкам.
    void initBuildings(int mapW, int mapH, const std::vector<BuildingObject> &buildings)
    {
        // Задачи:
        // 1. Вычислить число колонок и строк сетки
        // 2. Распределить все здания по ячейкам по их центру (cx, cy)
        cols_ = (mapW + CELL_SIZE - 1) / CELL_SIZE;
        rows_ = (mapH + CELL_SIZE - 1) / CELL_SIZE;
        const size_t total = static_cast<size_t>(cols_ * rows_);
        buildingGrid_.assign(total, {});
        targetGrid_.assign(total, {});
        zombieGrid_.assign(total, {});

        for (const auto &b : buildings) {
            const int cx = std::clamp(static_cast<int>(b.cx) / CELL_SIZE, 0, cols_ - 1);
            const int cy = std::clamp(static_cast<int>(b.cy) / CELL_SIZE, 0, rows_ - 1);
            buildingGrid_[static_cast<size_t>(cy * cols_ + cx)].push_back(b.id);
        }
    }

    /// Пересобирает списки активных зомби, целей и паникующих на каждом тике.
    void rebuildUnits(const std::vector<Unit> &units)
    {
        // Задачи:
        // 1. Сформировать списки activeZombies, panickingOutdoors и zombieTargets
        // 2. Разложить индексы юнитов по ячейкам targetGrid_ и zombieGrid_

        activeZombies_.clear();
        panickingOutdoors_.clear();
        zombieTargets_.clear();

        for (size_t i = 0; i < units.size(); ++i) {
            const auto &u = units[i];
            if (u.hp <= 0)
                continue;

            if (u.type == UnitType::Zombie) {
                activeZombies_.push_back(i);
            } else {
                if (u.insideBuildingId == -1 && u.panicTimerSec > 0.0F)
                    panickingOutdoors_.push_back(i);
                if (!u.isInfected)
                    zombieTargets_.push_back(i);
            }
        }

        if (cols_ <= 0 || rows_ <= 0)
            return;

        for (auto &cell : targetGrid_)
            cell.clear();
        for (auto &cell : zombieGrid_)
            cell.clear();

        for (size_t idx : zombieTargets_) {
            const int cx = std::clamp(static_cast<int>(units[idx].x) / CELL_SIZE, 0, cols_ - 1);
            const int cy = std::clamp(static_cast<int>(units[idx].y) / CELL_SIZE, 0, rows_ - 1);
            targetGrid_[static_cast<size_t>(cy * cols_ + cx)].push_back(idx);
        }

        for (size_t idx : activeZombies_) {
            const int cx = std::clamp(static_cast<int>(units[idx].x) / CELL_SIZE, 0, cols_ - 1);
            const int cy = std::clamp(static_cast<int>(units[idx].y) / CELL_SIZE, 0, rows_ - 1);
            zombieGrid_[static_cast<size_t>(cy * cols_ + cx)].push_back(idx);
        }
    }

    int cols() const { return cols_; }
    int rows() const { return rows_; }

    const std::vector<size_t> &activeZombies() const { return activeZombies_; }
    const std::vector<size_t> &panickingOutdoors() const { return panickingOutdoors_; }
    const std::vector<size_t> &zombieTargets() const { return zombieTargets_; }

    const std::vector<std::vector<size_t>> &targetGrid() const { return targetGrid_; }
    const std::vector<std::vector<size_t>> &zombieGrid() const { return zombieGrid_; }
    const std::vector<std::vector<int>> &buildingGrid() const { return buildingGrid_; }

private:
    int cols_ = 0;
    int rows_ = 0;
    std::vector<std::vector<size_t>> targetGrid_;
    std::vector<std::vector<size_t>> zombieGrid_;
    std::vector<std::vector<int>> buildingGrid_;
    std::vector<size_t> activeZombies_;
    std::vector<size_t> panickingOutdoors_;
    std::vector<size_t> zombieTargets_;
};

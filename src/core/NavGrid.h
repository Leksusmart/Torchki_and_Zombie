#pragma once
#include <cstdint>
#include <vector>

/// Битовые флаги проходимости и видимости для клетки навигационной сетки.
enum class NavCellFlag : uint8_t {
    None = 0,
    Walkable = 1 << 0,          ///< Клетка проходима (улица, тропинка или пол здания)
    Road = 1 << 1,              ///< Основная дорога
    Alley = 1 << 2,             ///< Тропинка/переулок к зданию
    BuildingFloor = 1 << 3,     ///< Внутреннее пространство здания
    BlocksMovement = 1 << 4,    ///< Динамическое препятствие движения (стена, блокпост)
    BlocksSight = 1 << 5,       ///< Блокирует линию обзора людей и солдат
    BlocksZombieSight = 1 << 6  ///< Блокирует обзор даже для зомби (стены зданий)
};

inline uint8_t operator|(NavCellFlag a, NavCellFlag b)
{
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}

inline uint8_t operator|(uint8_t a, NavCellFlag b)
{
    return a | static_cast<uint8_t>(b);
}

/**
 * @brief Единая навигационная сетка карты.
 *
 * Изолирует NavigationService от конкретных типов препятствий (стен, блокпостов,
 * укреплений): любые объекты записывают свои флаги в NavGrid, а поиск пути
 * и проверки видимости читают только флаги клеток.
 */
class NavGrid
{
public:
    /// Инициализирует размеры сетки и сбрасывает все флаги.
    void resize(int width, int height)
    {
        // Задачи:
        // 1. Сохранить размеры карты
        // 2. Выделить память под флаги и идентификаторы зданий
        width_ = width;
        height_ = height;
        const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
        flags_.assign(total, 0);
        buildingIds_.assign(total, -1);
    }

    /// Проверяет, находятся ли целочисленные координаты (x, y) в пределах карты.
    bool inBounds(int x, int y) const
    {
        return x >= 0 && x < width_ && y >= 0 && y < height_;
    }

    /// Возвращает ширину сетки в клетках/метрах.
    int width() const { return width_; }

    /// Возвращает высоту сетки в клетках/метрах.
    int height() const { return height_; }

    /// Заполняет базовую проходимость из сгенерированных массивов walkGrid и cityMask.
    void buildBaseFromMap(const std::vector<int16_t> &walkGrid, const std::vector<uint8_t> &cityMask)
    {
        // Задачи:
        // 1. Преобразовать коды walkGrid (1=дорога, 2=тропинка, >=10=здание) в битовые флаги
        // 2. Отметить непросматриваемые корпуса зданий флагом BlocksZombieSight
        const size_t total = flags_.size();
        for (size_t i = 0; i < total; ++i) {
            uint8_t f = 0;
            int16_t wg = walkGrid[i];
            if (wg == 1) {
                f |= NavCellFlag::Walkable | NavCellFlag::Road;
            } else if (wg == 2) {
                f |= NavCellFlag::Walkable | NavCellFlag::Alley;
            } else if (wg >= 10) {
                f |= NavCellFlag::Walkable | NavCellFlag::BuildingFloor;
                buildingIds_[i] = static_cast<int16_t>(wg - 10);
            }

            if (!cityMask.empty() && cityMask[i] == 2 && wg == 0) {
                f |= NavCellFlag::BlocksSight | NavCellFlag::BlocksZombieSight;
            }
            flags_[i] = f;
        }
    }

    /// Сбрасывает все динамические препятствия (стены, блокпосты) перед перерастеризацией.
    void clearDynamicObstacles()
    {
        const uint8_t dynMask = static_cast<uint8_t>(NavCellFlag::BlocksMovement)
                                | static_cast<uint8_t>(NavCellFlag::BlocksSight);
        for (size_t i = 0; i < flags_.size(); ++i) {
            // Не трогаем BlocksSight, если оно принадлежит статической стене здания
            if ((flags_[i] & static_cast<uint8_t>(NavCellFlag::BlocksZombieSight)) == 0) {
                flags_[i] &= static_cast<uint8_t>(~dynMask);
            } else {
                flags_[i] &= static_cast<uint8_t>(~static_cast<uint8_t>(NavCellFlag::BlocksMovement));
            }
        }
    }

    /// Добавляет флаг клетке (x, y), если она в пределах карты.
    void addFlag(int x, int y, NavCellFlag flag)
    {
        if (!inBounds(x, y))
            return;
        flags_[static_cast<size_t>(y) * width_ + x] |= static_cast<uint8_t>(flag);
    }

    /// Проверяет наличие указанного флага в клетке (x, y).
    bool hasFlag(int x, int y, NavCellFlag flag) const
    {
        if (!inBounds(x, y))
            return false;
        return (flags_[static_cast<size_t>(y) * width_ + x] & static_cast<uint8_t>(flag)) != 0;
    }

    /// Проверяет, свободна ли клетка для перемещения (проходима и не перекрыта препятствием).
    bool isWalkable(float x, float y) const
    {
        const int gx = static_cast<int>(x);
        const int gy = static_cast<int>(y);
        if (!inBounds(gx, gy))
            return false;
        const uint8_t f = flags_[static_cast<size_t>(gy) * width_ + gx];
        return (f & static_cast<uint8_t>(NavCellFlag::Walkable)) != 0
               && (f & static_cast<uint8_t>(NavCellFlag::BlocksMovement)) == 0;
    }

    /// Проверяет, перекрывает ли клетка обзор (для зомби можно игнорировать временные стены).
    bool blocksSight(int x, int y, bool ignoreDestructibleWalls = false) const
    {
        if (!inBounds(x, y))
            return true;
        const uint8_t f = flags_[static_cast<size_t>(y) * width_ + x];
        if (ignoreDestructibleWalls)
            return (f & static_cast<uint8_t>(NavCellFlag::BlocksZombieSight)) != 0;
        return (f & static_cast<uint8_t>(NavCellFlag::BlocksSight)) != 0;
    }

    /// Возвращает ID здания в клетке или -1, если клетка на улице.
    int buildingIdAt(int x, int y) const
    {
        if (!inBounds(x, y))
            return -1;
        return buildingIds_[static_cast<size_t>(y) * width_ + x];
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint8_t> flags_;
    std::vector<int16_t> buildingIds_;
};

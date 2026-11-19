#pragma once
#include <QPointF>
#include <QStringList>
#include <cstdint>

struct EffectParticle
{
    float x, y;
    float vx, vy;
    float size;
    float endSize;
    float lifeSec;
    float maxLifeSec;
    uint8_t r, g, b, a;
    bool isLine = false; // true для трассеров пуль
    float x2 = 0.0f, y2 = 0.0f;
};

struct RenderQuad
{
    float cx, cy;
    float w, h;
    float angleRad;
    uint8_t r, g, b, a;
};

enum class UnitType : uint8_t { Citizen = 0, Vaccinated = 1, Zombie = 2, Soldier = 3 };

// В enum class UnitState:
enum class UnitState : uint8_t {
    GoingToWork = 0,
    Working = 1,
    GoingHome = 2,
    AtHome = 3,
    Walking = 4,
    Evacuating = 5,     // Спокойно бежит к точке эвакуации
    PanicEvacuating = 6 // Паникует И одновременно прорывается к точке эвакуации
};

// Активная операция эвакуации на конкретном выходе
struct ActiveEvacuation
{
    int evacNodeId = -1;
    QPointF evacPos;
    float timeLeftSec = 30.0f;
    int boardedCount = 0;
    bool completed = false; // true когда набрано 50 чел или вышли 30 сек

    // Параметры конвоя на воде/мосту
    QPointF convoyStart;
    QPointF convoyEnd;
    QPointF convoyPos;
    std::vector<QPointF> passengerOffsets; // Локальные координаты людей в круге 15м
};

struct DropHelicopter
{
    enum class Phase : uint8_t { Approaching = 0, Hovering = 1, Departing = 2 };

    QPointF spawnPos;
    QPointF targetPos;
    QPointF pos;
    float dirX = 0.0F;
    float dirY = 0.0F;
    float angleRad = 0.0F;
    float rotorAngleRad = 0.0F;
    float hoverTimerSec = 2.0F;
    float traveledAfterDrop = 0.0F;
    Phase phase = Phase::Approaching;
};

struct OutbreakEvent
{
    float triggerTimeSec = 2.0f;
    int initialInfected = 1;
    bool triggered = false;
};
struct EvacRoute
{
    int group; // 1 или 2
    QPointF startPos;
    QPointF endPos;
};

struct RoadNode
{
    int id;
    QPointF pos;
    bool isEvacExit = false;
    QString evacName; // Название выхода
};

struct RoadEdge
{
    int fromId;
    int toId;
};

enum class BuildingType : uint8_t {
    Residential = 0, // Жилой дом (базовый размер 1x)
    Office = 1,      // Офисное здание (размер 1x - 3x)
    Factory = 2      // Завод / промзона (размер 4x - 6x)
};

struct LevelConfig
{
    QString mapPngPath = ":/Torchki_and_Zombie/levels/Level1.png";
    int initialCitizens = 2000;
    double initialBudget = 300.0;
    float startHour = 8.0F;
    float gameHoursPerSec = 0.15F; // 0.15F 1 день = 2,6 минут
    std::vector<OutbreakEvent> outbreaks;
};

// Статическое описание уровня (не зависящее от уровня сложности)
struct LevelDefinition
{
    QString levelName = "Manhattan";
    QString mapPngPath = ":/Torchki_and_Zombie/levels/Level1.png";
    float roadWidthMeters = 13.0f;
    float roadWidthMetersToBuilding = 4.0F;

    // Вместимость эвакуационной точки
    int evac_capacity = 50;

    // Скорость в помещении
    int speedInBuildingPercent = 40;

    // Процент ночных смен
    int nightWorkPercent = 0;

    // Вместимость зданий - м² на 1 человека
    float factoryCapacity = 15;
    float officeCapacity = 6;
    float houseCapacity = 20;

    // Проценты плотности застройки (0..100%)
    // если сумма меньше 100, то остальное пустота
    float factoriesPercent = 1; // Макс % заводов
    float officesPercent = 10;  // Макс % офисов
    float housesPercent = 100;  // Макс % жилых домов

    // Настройки размера жилого здания
    std::vector<unsigned short int> house = {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    5, // Отступ от края карты
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    12, // Расстояние между точками спавна без наложения
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    10, // Мин ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    50, // Макс ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    10, // Мин длина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    10 // Макс длина
    };
    // Настройки размера офиса
    std::vector<unsigned short int> office = {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    5, // Отступ от края карты
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    40, // Расстояние между точками спавна без наложения
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    20, // Мин ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    40, // Макс ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    10, // Мин длина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    20 // Макс длина
    };
    // Настройки размера завода
    std::vector<unsigned short int> factory = {
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           10, // Отступ от края карты
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           100, // Расстояние между точками спавна без наложения
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           30, // Мин ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           70, // Макс ширина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           24, // Мин длина
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           50 // Макс длина
    };

    // Приоритет прямых дорог (кратных 90 градусам) от 0
    unsigned char preferStraightRoads = 30;

    // Объединять ли все дороги в единую связную сеть (MST)
    bool shouldUnionRoads = true;

    // Проценты количества дорог на перекрёстках 0-100%
    unsigned char chance1 = 5;
    unsigned char chance2 = 5;
    unsigned char chance3 = 55;
    unsigned char chance4 = 35;
    unsigned char chance5 = 0;

    // Палитра возможных цветов крыш (0xRRGGBB)
    std::vector<uint32_t> roofColors = {
                                                                                                                                                                                                                                    0x555B66, // Графитово-серый
                                                                                                                                                                                                                                    0x646A75, // Светло-серый шифер
                                                                                                                                                                                                                                    0x7A4B42, // Терракотовый / черепица
                                                                                                                                                                                                                                    0x4A5D6E, // Серо-синий металл
                                                                                                                                                                                                                                    0x6E655A // Песочно-коричневый
    };

    // Цвета полов внутри зданий (0xRRGGBB)
    uint32_t COLOR_FLOOR_HOUSE = 0x777777;
    uint32_t COLOR_FLOOR_OFFICE = 0x777777;
    uint32_t COLOR_FLOOR_FACTORY = 0x777777;

    // Цвета карты
    uint32_t COLOR_CITY = 0x3A3E48;
    uint32_t COLOR_NODE = 0xFFF200;
    uint32_t COLOR_EVAC = 0xED1C24;

    // Метки маршрутов эвакуации
    uint32_t COLOR_G1_START = 0x10FFFF;
    uint32_t COLOR_G1_END = 0xA0FFFF;
    uint32_t COLOR_G2_START = 0x1077FF;
    uint32_t COLOR_G2_END = 0x5077FF;

    // Названия для эвакуационных точек по порядку сканирования (сверху вниз, слева направо)
    QStringList evacNames = {"Мост верхний 1",
                             "Мост побегушка",
                             "Мост нижний 2",
                             "Мост нижний 3",
                             "Мост нижний 4",
                             "Мост нижний 5"};
};

struct BuildingObject
{
    int id;
    BuildingType type = BuildingType::Residential;
    float cx, cy;
    float width, height;
    float angleRad;
    float area;

    int capacity = 0;
    int currentOccupants = 0;
    int reservedOccupants = 0;
    int hourlyMaxWorkers[24] = {0};
    int maxHealth = 100;
    int health = 100;

    int nearestNodeId = -1;
    float distToNode = 1e9F;

    float burnTimerSec = 0.0F;        // Таймер горения (area / 15.0f сек после падения HP до 0)
    bool isBurning = false;           // Сейчас охвачено пламенем
    bool isBurnedOut = false;         // Полностью обуглилось
    float underAttackTimerSec = 0.0F; // Таймер активной атаки зомби (3.0 сек после последнего удара)

    uint32_t floorColor = 0xD6C29E; // Цвет пола при снятой крыше (0xRRGGBB)

    std::vector<QPointF>
        pathFromRoad; // Ломаная тропинка от дороги (pathFromRoad.front()) до порога двери (pathFromRoad.back())

    QPointF doorCenter; // Центр проёма в стене (для отрисовки разрыва стены при снятой крыше)
    float doorAngleRad = 0.0F; // Угол ориентации проёма
    uint8_t r, g, b;
};

struct Unit
{
    int id = -1;
    float x, y;
    float vx, vy;
    UnitType type;
    UnitState state = UnitState::Walking;

    int hp = 2; // Жители: 2, Солдаты: 3, Зомби: 5
    bool isInfected = false;
    float infectionHoursLeft = 0.0F;

    float panicTimerSec = 0.0F;
    float attackCooldownSec = 0.0F; // Перезарядка укуса (0.5с) или выстрела (1.0с)

    bool hasLastKnownTarget = false; // Помнит ли последнюю позицию цели
    QPointF lastKnownTargetPos;      // Координаты, где цель скрылась за углом

    bool isNightWorker = false;
    int targetBuildingId = -1;
    int insideBuildingId = -1;

    float stateTimerSec = 0.0F;
    std::vector<QPointF> waypoints;
    float laneOffset = 0.0F; // Смещение по ширине дороги в долях от roadWidth (-0.375 .. +0.375)
    float fireDamageCooldownSec = 0.0F; // Перезарядка урона от огня (0.5 сек)
    float stuckTimerSec = 0.0F;
    QPointF lastCheckPos;

    float lifeTimerSec = 0.0F; // Таймер службы десантника (90 сек)
    bool isRetreating = false; // True, когда 90 сек истекли и солдат идёт на эвакуацию
    QPointF evacBridgeEnd;     // Финишная точка моста для одиночного ухода солдата
    bool onEvacBridge = false; // True, когда солдат добежал до выхода и идёт по мосту
};
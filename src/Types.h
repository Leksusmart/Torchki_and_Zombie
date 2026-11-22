#pragma once
#include <QPointF>
#include <QStringList>
#include <cstdint>
#include <vector>

/// Визуальная частица эффекта (трассер пули, искра или ударная волна).
struct EffectParticle
{
    float x, y;
    float vx, vy;
    float size;
    float endSize;
    float lifeSec;
    float maxLifeSec;
    uint8_t r, g, b, a;
    bool isLine = false; ///< true для трассеров пуль (отрезок от (x, y) до (x2, y2))
    float x2 = 0.0F, y2 = 0.0F;
};

/// Прямоугольник или круг для пакетной отправки в вершинный буфер Renderer.
struct RenderQuad
{
    float cx, cy;
    float w, h;
    float angleRad;
    uint8_t r, g, b, a;
};

/// Роль юнита в симуляции.
enum class UnitType : uint8_t { Citizen = 0, Vaccinated = 1, Zombie = 2, Soldier = 3 };

/// Текущее состояние конечного автомата жителя.
enum class UnitState : uint8_t {
    GoingToWork = 0,
    Working = 1,
    GoingHome = 2,
    AtHome = 3,
    Walking = 4,        ///< Свободная прогулка по городу
    Evacuating = 5,     ///< Спокойный бег к точке эвакуации
    PanicEvacuating = 6 ///< Паника с уклонением от зомби по пути к эвакуации
};

/// Активная операция эвакуации на конкретном выходе карты.
struct ActiveEvacuation
{
    int evacNodeId = -1;
    QPointF evacPos;
    float timeLeftSec = 30.0F;
    int boardedCount = 0;
    bool completed = false; ///< true при заполнении конвоя или истечении таймера

    QPointF convoyStart;
    QPointF convoyEnd;
    QPointF convoyPos;
    std::vector<QPointF> passengerOffsets; ///< Смещения пассажиров внутри круга/овала конвоя
};

/// Состояние десантного вертолёта (подлёт -> зависание -> отлёт).
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

/// Запланированная вспышка заражения на уровне.
struct OutbreakEvent
{
    float triggerTimeSec = 2.0F;
    int initialInfected = 1;
    bool triggered = false;
};

/// Маршрут движения эвакуационного конвоя и уходящих десантников.
struct EvacRoute
{
    int group; ///< Группа маршрута (1 или 2)
    QPointF startPos;
    QPointF endPos;
};

/// Узел дорожного графа (перекрёсток, точка эвакуации или изгиб тропинки).
struct RoadNode
{
    int id;
    QPointF pos;
    bool isEvacExit = false;
    bool isAlleyNode = false; ///< true для узлов переулков и тропинок к зданиям
    QString evacName;
};

/// Ребро дорожного графа между двумя узлами.
struct RoadEdge
{
    int fromId;
    int toId;
};

/// Секция карантинной стены, перекрывающая улицу.
struct QuarantineWall
{
    int id = 0;
    float x1 = 0.0F, y1 = 0.0F;
    float x2 = 0.0F, y2 = 0.0F;
    float cx = 0.0F, cy = 0.0F;
    float length = 0.0F;
    float thickness = 9.0F;
    float angleRad = 0.0F;

    int hp = 100;
    int maxHp = 100;
    float underAttackTimerSec = 0.0F; ///< Белый контур при атаке (3.0 с после удара)

    bool isConstructing = true;
    float buildTimeLeft = 0.0F;
    float buildTimeTotal = 1.0F;

    bool isTearingDown = false;
    float teardownTimeLeft = 0.0F;
    float teardownTimeTotal = 10.0F;
};

/// Тип городского здания.
enum class BuildingType : uint8_t {
    Residential = 0, ///< Жилой дом
    Office = 1,      ///< Офисное здание
    Factory = 2      ///< Завод / промзона
};

/// Параметры запускаемого уровня (население, бюджет, скорость времени, вспышки).
struct LevelConfig
{
    QString mapPngPath = ":/qt/qml/TorchkiAndZombie/levels/Level1.png";
    int initialCitizens = 2000;
    double initialBudget = 300.0;
    float startHour = 8.0F;
    float gameHoursPerSec = 0.15F;
    std::vector<OutbreakEvent> outbreaks;
};

/// Статическое описание геометрии и правил генерации уровня из PNG-маски.
struct LevelDefinition
{
    QString levelName = "Manhattan";
    QString mapPngPath = ":/qt/qml/TorchkiAndZombie/levels/Level1.png";
    float roadWidthMeters = 13.0F;
    float roadWidthMetersToBuilding = 4.0F;

    int evac_capacity = 50;
    int speedInBuildingPercent = 40;
    int nightWorkPercent = 0;

    float factoryCapacity = 15;
    float officeCapacity = 6;
    float houseCapacity = 20;

    float factoriesPercent = 1;
    float officesPercent = 10;
    float housesPercent = 100;

    std::vector<unsigned short int> house = {5, 12, 10, 50, 10, 10};
    std::vector<unsigned short int> office = {5, 40, 20, 40, 10, 20};
    std::vector<unsigned short int> factory = {10, 100, 30, 70, 24, 50};

    unsigned char preferStraightRoads = 30;
    bool shouldUnionRoads = true;

    unsigned char chance1 = 5;
    unsigned char chance2 = 5;
    unsigned char chance3 = 55;
    unsigned char chance4 = 35;
    unsigned char chance5 = 0;

    std::vector<uint32_t> roofColors = {0x555B66, 0x646A75, 0x7A4B42, 0x4A5D6E, 0x6E655A};

    uint32_t COLOR_FLOOR_HOUSE = 0x777777;
    uint32_t COLOR_FLOOR_OFFICE = 0x777777;
    uint32_t COLOR_FLOOR_FACTORY = 0x777777;

    uint32_t COLOR_CITY = 0x3A3E48;
    uint32_t COLOR_NODE = 0xFFF200;
    uint32_t COLOR_EVAC = 0xED1C24;

    uint32_t COLOR_G1_START = 0x10FFFF;
    uint32_t COLOR_G1_END = 0xA0FFFF;
    uint32_t COLOR_G2_START = 0x1077FF;
    uint32_t COLOR_G2_END = 0x5077FF;

    QStringList evacNames = {"Мост верхний 1",
                             "Мост побегушка",
                             "Мост нижний 2",
                             "Мост нижний 3",
                             "Мост нижний 4",
                             "Мост нижний 5"};
};

/// Описание здания на карте (позиция, вместимость, здоровье, вход с дороги).
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

    float burnTimerSec = 0.0F;
    bool isBurning = false;
    bool isBurnedOut = false;
    float underAttackTimerSec = 0.0F;

    uint32_t floorColor = 0xD6C29E;
    std::vector<QPointF> pathFromRoad;
    QPointF doorCenter;
    float doorAngleRad = 0.0F;
    uint8_t r, g, b;
};

/// Данные отдельного юнита (житель, вакцинированный, зомби или солдат).
struct Unit
{
    int id = -1;
    int targetUnitId = -1;

    float x, y;
    float vx, vy;
    UnitType type;
    UnitState state = UnitState::Walking;
    bool isEvacuated = false;

    int hp = 2;
    bool isInfected = false;
    float infectionHoursLeft = 0.0F;

    float panicTimerSec = 0.0F;
    float attackCooldownSec = 0.0F;
    float repathCooldownSec = 0.0F;

    bool hasLastKnownTarget = false;
    QPointF lastKnownTargetPos;

    bool isNightWorker = false;
    int targetBuildingId = -1;
    int insideBuildingId = -1;

    float stateTimerSec = 0.0F;
    std::vector<QPointF> waypoints;
    float laneOffset = 0.0F;
    float fireDamageCooldownSec = 0.0F;
    float stuckTimerSec = 0.0F;
    QPointF lastCheckPos;
    int repathCount = 0;

    float lifeTimerSec = 0.0F;
    bool isRetreating = false;
    QPointF evacBridgeEnd;
    bool onEvacBridge = false;
};

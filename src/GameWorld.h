#pragma once
#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include "MapGenerator.h"
#include <cstdint>
#include <vector>

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
    static constexpr int MAX_CAPACITY = 50;
    bool completed = false; // true когда набрано 50 чел или вышли 30 сек

    // Параметры конвоя на воде/мосту
    QPointF convoyStart;
    QPointF convoyEnd;
    QPointF convoyPos;
    std::vector<QPointF> passengerOffsets; // Локальные координаты людей в круге 15м
};

struct Unit
{
    float x, y;
    float vx, vy;
    UnitType type;
    UnitState state = UnitState::Walking;

    int hp = 2; // Жители: 2, Солдаты: 3, Зомби: 5
    bool isInfected = false;
    float infectionHoursLeft = 0.0f;

    float panicTimerSec = 0.0f;
    float attackCooldownSec = 0.0f; // Перезарядка укуса (0.5с) или выстрела (1.0с)

    bool hasLastKnownTarget = false; // Помнит ли последнюю позицию цели
    QPointF lastKnownTargetPos;      // Координаты, где цель скрылась за углом

    bool isNightShift = false;
    int targetBuildingId = -1;
    int insideBuildingId = -1;

    float stateTimerSec = 0.0f;
    std::vector<QPointF> waypoints;
    float laneOffset = 0.0f; // Смещение по ширине дороги в долях от roadWidth (-0.375 .. +0.375)
    float fireDamageCooldownSec = 0.0f; // Перезарядка урона от огня (0.5 сек)
    float stuckTimerSec = 0.0f;
    QPointF lastCheckPos;
};

struct OutbreakEvent
{
    float triggerTimeSec = 2.0f;
    int initialInfected = 1;
    bool triggered = false;
};

struct LevelConfig
{
    QString mapPngPath = ":/Torchki_and_Zombie/levels/Level1.png";
    int initialCitizens = 2000;
    double initialBudget = 300.0;
    float startHour = 7.5f;
    float gameHoursPerSec = 0.25f;
    std::vector<OutbreakEvent> outbreaks;
};

class GameWorld : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList activeEvacuations READ activeEvacuations NOTIFY evacuationsChanged)
    Q_PROPERTY(int evacuatedTotalCount READ evacuatedTotalCount NOTIFY statsChanged)
    Q_PROPERTY(QVariantList evacPoints READ evacPoints NOTIFY mapUpdated)
    Q_PROPERTY(QVariantList burningLights READ burningLights NOTIFY burningLightsChanged)
    Q_PROPERTY(QVariantList workplaceLabels READ workplaceLabels NOTIFY statsChanged)
    Q_PROPERTY(QColor waterColor READ waterColor NOTIFY mapUpdated)
    Q_PROPERTY(int budget READ budget NOTIFY statsChanged)
    Q_PROPERTY(double incomePerSec READ incomePerSec NOTIFY statsChanged)
    Q_PROPERTY(int citizensCount READ citizensCount NOTIFY statsChanged)
    Q_PROPERTY(int zombiesCount READ zombiesCount NOTIFY statsChanged)
    Q_PROPERTY(int eliminatedCount READ eliminatedCount NOTIFY statsChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(int speedMultiplier READ speedMultiplier NOTIFY stateChanged)
    Q_PROPERTY(int selectedTool READ selectedTool WRITE setSelectedTool NOTIFY stateChanged)
    Q_PROPERTY(int mapWidth READ mapWidth NOTIFY mapUpdated)
    Q_PROPERTY(int mapHeight READ mapHeight NOTIFY mapUpdated)

    Q_PROPERTY(QString formattedTime READ formattedTime NOTIFY timeChanged)
    Q_PROPERTY(int dayNumber READ dayNumber NOTIFY timeChanged)
    Q_PROPERTY(bool isNight READ isNight NOTIFY timeChanged)
    Q_PROPERTY(float nightDarkness READ nightDarkness NOTIFY timeChanged)

public:
    explicit GameWorld(QObject *parent = nullptr);

    QVariantList activeEvacuations() const { return m_activeEvacuationsList; }
    int evacuatedTotalCount() const { return m_evacuatedTotalCount; }
    Q_INVOKABLE QVariantMap snapEvacPoint(float worldX,
                                          float worldY,
                                          float snapRadius = 20.0f) const;
    QVariantList evacPoints() const { return m_evacPoints; }
    void startLevel(const LevelDefinition &levelDef);
    QVariantList burningLights() const { return m_burningLights; }
    QVariantList workplaceLabels() const { return m_workplaceLabels; }
    const std::vector<RenderQuad> &effectsGeometry() const { return m_effectsGeometry; }
    QColor waterColor() const { return m_generator.waterColor(); }
    int budget() const { return static_cast<int>(m_budget); }
    double incomePerSec() const { return m_incomePerSec; }
    int citizensCount() const { return m_citizensCount; }
    int zombiesCount() const { return m_zombiesCount; }
    int eliminatedCount() const { return m_eliminatedCount; }
    bool paused() const { return m_paused; }
    int speedMultiplier() const { return m_speedMultiplier; }
    int selectedTool() const { return m_selectedTool; }
    void setSelectedTool(int tool);

    int mapWidth() const { return m_generator.width(); }
    int mapHeight() const { return m_generator.height(); }

    QString formattedTime() const;
    int dayNumber() const { return m_dayNumber; }
    bool isNight() const;
    float nightDarkness() const;

    void startLevel(const LevelConfig &config);
    Q_INVOKABLE void startCustomLevel(int citizens, const QVariantList &outbreakDelaysSec);
    Q_INVOKABLE void regenerateMap();

    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void toggleSpeed();
    Q_INVOKABLE void useToolAt(float worldX, float worldY);

    const QImage &backgroundImage() const { return m_generator.renderedBackground(); }
    const std::vector<RenderQuad> &buildingsGeometry() const { return m_buildingsGeometry; }
    const std::vector<RenderQuad> &buildingOverlaysGeometry() const
    {
        return m_buildingOverlaysGeometry;
    }
    const std::vector<RenderQuad> &unitGeometry() const { return m_unitGeometry; }

signals:
    void pulseWaveTriggered(float x, float y, float radius, const QString &colorHex);
    void evacuationsChanged();
    void mapUpdated();
    void unitsUpdated();
    void statsChanged();
    void stateChanged();
    void timeChanged();
    void outbreakOccurred(float x, float y, int outbreakIndex);
    void burningLightsChanged();

private slots:
    void tick();

private:
    void updateEvacuations(float dt);
    void rebuildActiveEvacuationsList();
    std::vector<ActiveEvacuation> m_evacs;
    QVariantList m_activeEvacuationsList;
    int m_evacuatedTotalCount = 0;
    void rebuildEvacPoints();
    LevelDefinition m_currentLevel;
    QVariantList m_evacPoints;
    bool hasLineOfSight(float x1, float y1, float x2, float y2) const;

    void buildRoadGraphCache();
    int findNearestRoadNode(float x, float y) const;
    std::vector<QPointF> buildPathBetween(QPointF startPos, QPointF endPos) const;
    int findNearestAvailableBuilding(float x, float y, bool wantWorkplace) const;

    void exitBuilding(Unit &u, std::vector<BuildingObject> &buildings);
    void enterBuilding(Unit &u, BuildingObject &b);
    void updateCitizenSchedule(Unit &u, float dt, std::vector<BuildingObject> &buildings);
    void infectUnit(Unit &u, bool immediateZombie = false);

    void buildBuildingsGeometry();
    void spawnInitialUnits(int citizenCount);
    void processOutbreaks();
    void triggerSingleOutbreak(int count, int index);
    void rebuildUnitRenderData();
    bool isWalkable(float x, float y) const;
    bool isCityArea(float x, float y) const;
    QPointF findNearestWalkable(float x, float y, float maxRadius = 40.0f) const;
    float roadWidth() const { return m_generator.roadWidth(); }
    void applyFireAvoidanceAndDamage(Unit &u,
                                     float dt,
                                     const std::vector<BuildingObject> &buildings);

    void updateHourlyTaxes(int newHour, bool isInitialSetup = false);
    void applyWallRepulsion(Unit &u, float dt);
    void rebuildBurningLights();
    QVariantList m_burningLights;

    int m_lastTrackedHour = -1;

    void rebuildWorkplaceLabels();
    void updateEffects(float dt);
    void spawnBulletTracer(float x1, float y1, float x2, float y2);
    void spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b);
    void spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b);

    std::vector<EffectParticle> m_particles;
    std::vector<RenderQuad> m_effectsGeometry;
    QVariantList m_workplaceLabels;

    MapGenerator m_generator;
    LevelConfig m_currentConfig;
    std::vector<Unit> m_units;
    void rebuildBuildingOverlays();
    std::vector<RenderQuad> m_buildingOverlaysGeometry;
    std::vector<RenderQuad> m_buildingsGeometry;
    std::vector<RenderQuad> m_unitGeometry;

    std::vector<std::vector<std::pair<int, float>>> m_roadAdj;

    QTimer m_timer;
    float m_elapsedSimSec = 0.0f;
    float m_timeOfDayHours = 7.5f;
    int m_dayNumber = 1;

    static constexpr float BASE_CITIZEN_SPEED = 14.0f;  // Базовая скорость шага жителя
    static constexpr float RUN_SPEED = 21.0f;           // Скорость бега жителя в панике
    static constexpr float ZOMBIE_WANDER_SPEED = 7.0f;  // 50% от базовой скорости жителя
    static constexpr float ZOMBIE_CHASE_SPEED = 24.15f; // 115% от скорости бега жителя (21 * 1.15)
    static constexpr double TAX_PER_WORKER_SEC = 0.005;

    double m_budget = 300.0;
    double m_incomePerSec = 0.0;
    int m_citizensCount = 0;
    int m_zombiesCount = 0;
    int m_eliminatedCount = 0;
    bool m_paused = false;
    int m_speedMultiplier = 1;
    int m_selectedTool = 0;
};

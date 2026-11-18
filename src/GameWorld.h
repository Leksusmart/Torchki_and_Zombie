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

#include <vector>

#include "Types.h"

class GameWorld : public QObject
{
public:
    explicit GameWorld(QObject *parent = nullptr);
    void startLevel(const LevelConfig &config);

    const QImage &backgroundImage() const { return g_generator.renderedBackground(); }
    const std::vector<RenderQuad> &buildingsGeometry() const { return g_buildingsGeometry; }
    const std::vector<RenderQuad> &unitGeometry() const { return g_unitGeometry; }
    const std::vector<RenderQuad> &buildingOverlaysGeometry() const
    {
        return g_buildingOverlaysGeometry;
    }
    const std::vector<RenderQuad> &effectsGeometry() const { return g_effectsGeometry; }

signals:
    void outbreakOccurred(float x, float y, int outbreakIndex);
    void unitsUpdated();
    void pulseWaveTriggered(float x, float y, float radius, const QString &colorHex);

private: // функции
    void tick();
    void buildBuildingsGeometry();
    void spawnInitialUnits(int citizenCount);
    void processOutbreaks();
    void triggerSingleOutbreak(int count, int index);
    void rebuildUnitRenderData();
    void updateHourlyTaxes(int newHour, bool isInitialSetup = false);
    void rebuildBurningLights();
    void rebuildWorkplaceLabels();
    void rebuildBuildingOverlays();
    float roadWidth() const { return g_generator.roadWidth(); }
    void updateSelectedBuildingInfo();
    // Поведение юнитов (разбито из бывшего монолита peopleUpdateActions)
    void peopleUpdateActions(float dt,
                             std::vector<BuildingObject> &buildings,
                             const std::vector<size_t> &activeZombies,
                             const std::vector<size_t> &panickingOutdoors,
                             const std::vector<size_t> &zombieTargets,
                             bool &buildingsVisualDirty);
    void updateCitizenBehavior(Unit &u,
                               size_t unitIndex,
                               float dt,
                               std::vector<BuildingObject> &buildings,
                               const std::vector<size_t> &activeZombies,
                               const std::vector<size_t> &panickingOutdoors);
    void updateSoldierBehavior(Unit &u,
                               float dt,
                               std::vector<BuildingObject> &buildings,
                               const std::vector<size_t> &activeZombies);
    void updateZombieBehavior(Unit &u,
                              float dt,
                              std::vector<BuildingObject> &buildings,
                              const std::vector<size_t> &zombieTargets,
                              bool &buildingsVisualDirty);
    void stepUnitMovement(Unit &u, float dt, std::vector<BuildingObject> &buildings);

    // AI.h
    void updateCitizenSchedule(Unit &u, float dt, std::vector<BuildingObject> &buildings);
    void infectUnit(Unit &u, bool immediateZombie = false);
    void applyWallRepulsion(Unit &u, float dt);
    void applyFireAvoidanceAndDamage(Unit &u,
                                     float dt,
                                     const std::vector<BuildingObject> &buildings);

    // Effects.h
    void updateEffects(float dt);
    void spawnBulletTracer(float x1, float y1, float x2, float y2);
    void spawnPulseWave(float x, float y, float maxRadius, uint8_t r, uint8_t g, uint8_t b);
    void spawnBurstParticles(float x, float y, int count, uint8_t r, uint8_t g, uint8_t b);

    // Evacuation.h
    void updateEvacuations(float dt);
    void rebuildActiveEvacuationsList();
    void rebuildEvacPoints();

    // Navigation.h
    bool hasLineOfSight(float x1, float y1, float x2, float y2) const;
    std::vector<QPointF> buildPathBetween(QPointF startPos, QPointF endPos) const;
    std::vector<QPointF> buildPathToBuilding(QPointF startPos, const BuildingObject &b) const;
    int findNearestAvailableBuilding(float x, float y, bool wantWorkplace) const;
    int findNearestRoadNode(float x, float y) const;
    bool isWalkable(float x, float y) const;
    bool isCityArea(float x, float y) const;
    QPointF findNearestWalkable(float x, float y, float maxRadius = 40.0F) const;
    void buildRoadGraphCache();

private: // переменные
    LevelDefinition g_currentLevel;

    LevelConfig g_currentConfig;

    MapGenerator g_generator;

    QTimer g_timer;

    bool g_paused = false;

    int g_selectedBuildingId = -1;
    int g_lastTrackedHour = -1;
    int g_evacuatedTotalCount = 0;
    int g_dayNumber = 1;
    int g_citizensCount = 0;
    int g_zombiesCount = 0;
    int g_eliminatedCount = 0;
    int g_gridCols = 0;
    int g_gridRows = 0;

    int g_speedMultiplier = 1;
    int g_selectedTool = 0;
    float g_elapsedSimSec = 0.0f;
    float g_timeOfDayHours = 7.5f;
    double g_budget = 300.0;
    double g_incomePerSec = 0.0;

    static constexpr int GRID_CELL_SIZE = 64;

    static constexpr float BASE_CITIZEN_SPEED = 20.0F; // Базовая скорость шага жителя
    static constexpr float RUN_SPEED = 30.0F;          // Скорость бега жителя в панике
    static constexpr float ZOMBIE_WANDER_SPEED = BASE_CITIZEN_SPEED
                                                 / 2; // 50% от базовой скорости жителя
    static constexpr float ZOMBIE_CHASE_SPEED = RUN_SPEED
                                                * 1.15F; // 115% от скорости бега жителя (21 * 1.15)
    static constexpr double TAX_PER_WORKER_SEC = 0.005F;

    QVariantList g_activeEvacuationsList;
    QVariantList g_evacPoints;
    QVariantList g_burningLights;
    QVariantList g_workplaceLabels;

    QVariantMap g_selectedBuildingInfo;

    std::vector<std::vector<int>>
        g_nodeSpatialGrid; // Пространственная сетка узлов дорог для мгновенного поиска startNode
    std::vector<std::vector<float>>
        g_allPairsDist; // Кэш кратчайших расстояний между всеми узлами графа: [fromNode][toNode]
    std::vector<Unit> g_units;
    std::vector<EffectParticle> g_particles;
    std::vector<RenderQuad> g_effectsGeometry;
    std::vector<ActiveEvacuation> g_evacs;
    std::vector<RenderQuad> g_buildingOverlaysGeometry;
    std::vector<RenderQuad> g_buildingsGeometry;
    std::vector<RenderQuad> g_unitGeometry;
    std::vector<std::vector<std::pair<int, float>>> g_roadAdj;

private:
    // QML BEGIN
    // QML связи
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap selectedBuildingInfo
               READ selectedBuildingInfo
               NOTIFY selectedBuildingChanged)
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
    // QML свойства
    /*Q_PROPERTY*/ QVariantMap selectedBuildingInfo() const { return g_selectedBuildingInfo; }
    /*Q_PROPERTY*/ QVariantList activeEvacuations() const { return g_activeEvacuationsList; }
    /*Q_PROPERTY*/ int evacuatedTotalCount() const { return g_evacuatedTotalCount; }
    /*Q_PROPERTY*/ QVariantList evacPoints() const { return g_evacPoints; }
    /*Q_PROPERTY*/ QVariantList burningLights() const { return g_burningLights; }
    /*Q_PROPERTY*/ QVariantList workplaceLabels() const { return g_workplaceLabels; }
    /*Q_PROPERTY*/ QColor waterColor() const { return g_generator.waterColor(); }
    /*Q_PROPERTY*/ int budget() const { return static_cast<int>(g_budget); }
    /*Q_PROPERTY*/ double incomePerSec() const { return g_incomePerSec; }
    /*Q_PROPERTY*/ int citizensCount() const { return g_citizensCount; }
    /*Q_PROPERTY*/ int zombiesCount() const { return g_zombiesCount; }
    /*Q_PROPERTY*/ int eliminatedCount() const { return g_eliminatedCount; }
    /*Q_PROPERTY*/ bool paused() const { return g_paused; }
    /*Q_PROPERTY*/ int speedMultiplier() const { return g_speedMultiplier; }
    /*Q_PROPERTY*/ int selectedTool() const { return g_selectedTool; }
    /*Q_PROPERTY*/ void setSelectedTool(int tool);
    /*Q_PROPERTY*/ int mapWidth() const { return g_generator.width(); }
    /*Q_PROPERTY*/ int mapHeight() const { return g_generator.height(); }
    /*Q_PROPERTY*/ QString formattedTime() const;
    /*Q_PROPERTY*/ int dayNumber() const { return g_dayNumber; }
    /*Q_PROPERTY*/ bool isNight() const;
    /*Q_PROPERTY*/ float nightDarkness() const;

    // QML вызовы
    Q_INVOKABLE void regenerateMap();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void toggleSpeed();
    Q_INVOKABLE void useToolAt(float worldX, float worldY);
    Q_INVOKABLE QVariantMap snapEvacPoint(float worldX,
                                          float worldY,
                                          float snapRadius = 20.0f) const;
    Q_INVOKABLE void selectBuildingAt(float worldX, float worldY);
signals:
    // QML свойства изменились
    /*Q_PROPERTY*/ void evacuationsChanged();
    /*Q_PROPERTY*/ void mapUpdated();
    /*Q_PROPERTY*/ void statsChanged();
    /*Q_PROPERTY*/ void stateChanged();
    /*Q_PROPERTY*/ void timeChanged();
    /*Q_PROPERTY*/ void burningLightsChanged();
    /*Q_PROPERTY*/ void selectedBuildingChanged();
    // QML END
};

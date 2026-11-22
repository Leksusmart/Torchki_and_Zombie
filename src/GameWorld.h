#pragma once
#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <vector>

#include "MapGenerator.h"
#include "Types.h"
#include "ai/UnitSpatialGrid.h"
#include "core/SimProfiler.h"
#include "core/WorldState.h"
#include "systems/EffectsSystem.h"
#include "systems/EvacuationSystem.h"
#include "systems/HelicopterSystem.h"
#include "systems/NavigationService.h"
#include "systems/ToolRegistry.h"
#include "systems/WallSystem.h"

/**
 * @brief Фасад игрового мира для QML и координатор подсистем симуляции.
 *
 * Владеет WorldState и независимыми C++-подсистемами (NavigationService, WallSystem,
 * EffectsSystem, HelicopterSystem, EvacuationSystem), запуская их в цикле tick().
 */
class GameWorld : public QObject
{
public:
    explicit GameWorld(QObject *parent = nullptr);

    /// Инициализирует или перезапускает уровень с указанной конфигурацией.
    void startLevel(const LevelConfig &config);

    // Геттеры геометрии для Renderer
    const QImage &backgroundImage() const { return g_generator.renderedBackground(); }
    const std::vector<RenderQuad> &buildingsGeometry() const { return g_buildingsGeometry; }
    const std::vector<RenderQuad> &unitGeometry() const { return g_unitGeometry; }
    const std::vector<RenderQuad> &wallsGeometry() const { return wallSystem_.geometry(); }
    const std::vector<RenderQuad> &wallsCapsGeometry() const { return emptyCapsGeometry_; }
    const std::vector<RenderQuad> &buildingOverlaysGeometry() const
    {
        return g_buildingOverlaysGeometry;
    }
    const std::vector<RenderQuad> &effectsGeometry() const { return effectsSystem_.geometry(); }
    const std::vector<RenderQuad> &selectedPathGeometry() const { return g_selectedPathGeometry; }

signals:
    void outbreakOccurred(float x, float y, int outbreakIndex);
    void unitsUpdated();
    void pulseWaveTriggered(float x, float y, float radius, const QString &colorHex);

private: // функции
    /// Главный шаг симуляции (~60 FPS).
    void tick();

    /// Обновляет поведение и физику всех живых юнитов за шаг dt.
    void updateAllUnits(float dt, bool &buildingsVisualDirty, bool &workplaceLabelsDirty, bool &statsDirty);

    /// Пересчитывает занятость зданий, сгоревшие постройки и удаляет погибших юнитов.
    void postUpdateUnitsAndBuildings(bool buildingsVisualDirty);

    /// Заполняет геометрию всех зданий с учётом состояния (пустое, занятое, горящее, сгоревшее).
    void buildBuildingsGeometry();

    /// Спавнит начальных мирных жителей на проходимых дорогах карты.
    void spawnInitialUnits(int citizenCount);

    /// Проверяет расписание вспышек заражения и запускает их по времени.
    void processOutbreaks();

    /// Заражает count случайных жителей на улице (нулевые пациенты).
    void triggerSingleOutbreak(int count, int index);

    /// Пересобирает буфер геометрии юнитов и пассажиров эвакуации для рендера.
    void rebuildUnitRenderData();

    /// Пересчитывает почасовой доход с офисов и заводов.
    void updateHourlyTaxes(int newHour, bool isInitialSetup = false);

    /// Пересобирает список источников света от горящих зданий для ночного оверлея.
    void rebuildBurningLights();

    /// Пересобирает подписи дохода над производственными зданиями.
    void rebuildWorkplaceLabels();

    /// Пересобирает полоски здоровья и заполненности над зданиями.
    void rebuildBuildingOverlays();

    /// Обновляет QVariantMap информации о выбранном здании.
    void updateSelectedBuildingInfo();

    /// Обновляет QVariantMap информации о выбранном юните и строит линию его маршрута.
    void updateSelectedUnitInfo(bool logDebug = false);

    /// Строит геометрию маршрута выбранного юнита для визуализации.
    void rebuildSelectedUnitPathGeometry();

    /// Синхронизирует NavGrid и веса рёбер дорожного графа с текущими стенами.
    void syncNavigationObstacles();

    QString buildingDisplayName(int buildingId) const;
    QString unitStateDisplayName(const Unit &u) const;
    QString unitDisplayName(int unitId) const;

private: // переменные и подсистемы
    static constexpr double TAX_PER_WORKER_SEC = 0.005;

    LevelDefinition g_currentLevel;
    LevelConfig g_currentConfig;
    MapGenerator g_generator;
    QTimer g_timer;

    WorldState state_;
    NavigationService navService_;
    EffectsSystem effectsSystem_;
    HelicopterSystem helicopterSystem_;
    EvacuationSystem evacuationSystem_;
    WallSystem wallSystem_;
    ToolRegistry toolRegistry_;
    UnitSpatialGrid spatialGrid_;
    SimProfiler profiler_;

    int g_selectedBuildingId = -1;
    int g_selectedUnitId = -1;
    int g_lastTrackedHour = -1;
    int g_selectedTool = 0;

    QVariantList g_burningLights;
    QVariantList g_workplaceLabels;
    QVariantList g_equippedTools = {1, 2, 3, 4};

    QVariantMap g_selectedUnitInfo;
    QVariantMap g_selectedBuildingInfo;

    std::vector<uint8_t> prevRoofOpen;
    std::vector<RenderQuad> g_selectedPathGeometry;
    std::vector<RenderQuad> g_buildingOverlaysGeometry;
    std::vector<RenderQuad> g_buildingsGeometry;
    std::vector<RenderQuad> g_unitGeometry;
    std::vector<RenderQuad> emptyCapsGeometry_;

private:
    // QML BEGIN
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList wallProgressBars READ wallProgressBars NOTIFY wallsChanged)
    Q_PROPERTY(QVariantMap selectedWallInfo READ selectedWallInfo NOTIFY selectedWallChanged)
    Q_PROPERTY(QVariantMap selectedUnitInfo READ selectedUnitInfo NOTIFY selectedUnitChanged)
    Q_PROPERTY(QVariantList helicopters READ helicopters NOTIFY helicoptersChanged)
    Q_PROPERTY(QVariantList dropZones READ dropZones NOTIFY helicoptersChanged)
    Q_PROPERTY(QVariantList toolCategories READ toolCategories NOTIFY toolCatalogChanged)
    Q_PROPERTY(QVariantList availableTools READ availableTools NOTIFY equippedToolsChanged)
    Q_PROPERTY(QVariantList equippedTools READ equippedTools WRITE setEquippedTools NOTIFY equippedToolsChanged)
    Q_PROPERTY(QVariantMap selectedBuildingInfo READ selectedBuildingInfo NOTIFY selectedBuildingChanged)
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
    Q_PROPERTY(int infectedCount READ infectedCount NOTIFY statsChanged)
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
    QVariantList wallProgressBars() const { return wallSystem_.progressBarsList(); }
    QVariantMap selectedWallInfo() const { return wallSystem_.selectedWallInfo(); }
    QVariantList helicopters() const { return helicopterSystem_.helicoptersList(); }
    QVariantList dropZones() const { return helicopterSystem_.dropZonesList(); }
    QVariantList toolCategories() const { return toolRegistry_.categoriesModel(); }
    QVariantList availableTools() const { return toolRegistry_.availableToolsModel(); }
    QVariantList equippedTools() const { return g_equippedTools; }
    void setEquippedTools(const QVariantList &tools);
    QVariantMap selectedBuildingInfo() const { return g_selectedBuildingInfo; }
    QVariantList activeEvacuations() const { return evacuationSystem_.activeEvacuationsList(); }
    int evacuatedTotalCount() const { return state_.evacuatedTotalCount; }
    QVariantList evacPoints() const { return evacuationSystem_.evacPointsList(); }
    QVariantList burningLights() const { return g_burningLights; }
    QVariantList workplaceLabels() const { return g_workplaceLabels; }
    QColor waterColor() const { return g_generator.waterColor(); }
    int budget() const { return static_cast<int>(state_.budget); }
    double incomePerSec() const { return state_.incomePerSec; }
    int citizensCount() const { return state_.citizensCount; }
    int zombiesCount() const { return state_.zombiesCount; }
    int infectedCount() const { return state_.infectedCount; }
    int eliminatedCount() const { return state_.eliminatedCount; }
    bool paused() const { return state_.paused; }
    int speedMultiplier() const { return state_.speedMultiplier; }
    int selectedTool() const { return g_selectedTool; }
    void setSelectedTool(int tool);
    int mapWidth() const { return g_generator.width(); }
    int mapHeight() const { return g_generator.height(); }
    QString formattedTime() const;
    int dayNumber() const { return state_.dayNumber; }
    bool isNight() const;
    float nightDarkness() const;
    QVariantMap selectedUnitInfo() const { return g_selectedUnitInfo; }

    // QML вызовы
    Q_INVOKABLE QVariantMap previewWall(float x1, float y1, float x2, float y2) const;
    Q_INVOKABLE bool placeWall(float x1, float y1, float x2, float y2);
    Q_INVOKABLE bool selectWallAt(float worldX, float worldY);
    Q_INVOKABLE bool startWallTeardown(int wallId);
    Q_INVOKABLE bool selectUnitAt(float worldX, float worldY, float radius = 8.0f);
    Q_INVOKABLE void regenerateMap();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void toggleSpeedTo1();
    Q_INVOKABLE void toggleSpeedTo2();
    Q_INVOKABLE void toggleSpeedTo3();
    Q_INVOKABLE void toggleSpeedTo4();
    Q_INVOKABLE void selectAt(float worldX, float worldY);
    Q_INVOKABLE QVariantMap previewTool(int toolId, float x1, float y1, float x2, float y2) const;
    Q_INVOKABLE bool useToolSegment(int toolId, float x1, float y1, float x2, float y2);
    Q_INVOKABLE void useToolAt(float worldX, float worldY);
    Q_INVOKABLE QVariantMap snapEvacPoint(float worldX, float worldY, float snapRadius = 20.0f) const;
    Q_INVOKABLE void selectBuildingAt(float worldX, float worldY);

signals:
    void wallsChanged();
    void selectedWallChanged();
    void selectedUnitChanged();
    void helicoptersChanged();
    void equippedToolsChanged();
    void toolCatalogChanged();
    void evacuationsChanged();
    void mapUpdated();
    void statsChanged();
    void stateChanged();
    void timeChanged();
    void burningLightsChanged();
    void selectedBuildingChanged();
    // QML END
};

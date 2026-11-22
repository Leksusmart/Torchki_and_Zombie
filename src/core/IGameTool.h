#pragma once
#include <QString>
#include <QVariantMap>
#include <cstdint>
#include <functional>

class EvacuationSystem;
class EffectsSystem;
class HelicopterSystem;
class MapGenerator;
class NavigationService;
class WallSystem;
struct WorldState;

/// Режим взаимодействия инструмента с картой в QML.
enum class ToolInputMode : uint8_t {
    Click = 0,     ///< Одиночный клик по карте (вакцина, десант)
    DragLine = 1,  ///< Протяжка отрезка мышью (карантинная стена)
    SnapClick = 2  ///< Клик с привязкой к узлам карты (эвакуация)
};

/// Метаданные инструмента для отображения в ToolkitMenu и нижней панели ToolBar.
struct ToolDescriptor
{
    int id = 0;
    QString category;
    QString title;
    QString desc;
    int cost = 0;
    QString costText;
    QString accent = "#4299e1";
    bool locked = false;
    QString lockText = "ЗАБЛОКИРОВАНО";
    QString slotsTag;
    ToolInputMode inputMode = ToolInputMode::Click;
    float previewRadius = 0.0F;
    QString hintText;

    /// Преобразует метаданные в QVariantMap для передачи в QML.
    QVariantMap toVariantMap() const
    {
        QVariantMap m;
        m["id"] = id;
        m["category"] = category;
        m["title"] = title;
        m["desc"] = desc;
        m["cost"] = costText;
        m["costValue"] = cost;
        m["accent"] = accent;
        m["locked"] = locked;
        m["lockText"] = lockText;
        if (!slotsTag.isEmpty())
            m["slotsTag"] = slotsTag;
        m["inputMode"] = static_cast<int>(inputMode);
        m["previewRadius"] = previewRadius;
        m["hintText"] = hintText;
        return m;
    }
};

/// Контекст выполнения инструмента с доступом ко всем нужным подсистемам мира.
struct ToolExecutionContext
{
    WorldState &state;
    const MapGenerator &generator;
    const NavigationService &nav;
    EffectsSystem &effects;
    HelicopterSystem &helicopters;
    EvacuationSystem &evacuation;
    WallSystem &walls;

    std::function<void(float x, float y, float radius, const QString &color)> emitPulseWave;
    std::function<void()> rebuildUnitRender;
    bool statsChanged = false;
    bool unitsChanged = false;
    bool helicoptersChanged = false;
    bool evacuationsChanged = false;
    bool wallsChanged = false;
    bool mapUpdated = false;
};

/**
 * @brief Базовый интерфейс игрового инструмента (паттерн Strategy).
 *
 * Изолирует логику каждого инструмента от GameWorld::useToolAt и QML:
 * добавление новой способности сводится к созданию класса-наследника IGameTool
 * и его регистрации в ToolRegistry.
 */
class IGameTool
{
public:
    virtual ~IGameTool() = default;

    /// Возвращает метаданные инструмента для UI.
    virtual const ToolDescriptor &descriptor() const = 0;

    /// Формирует данные предпросмотра для QML (актуально для DragLine и SnapClick).
    virtual QVariantMap preview(float x1,
                                float y1,
                                float x2,
                                float y2,
                                const WorldState &state,
                                const MapGenerator &generator,
                                const NavigationService &nav,
                                const WallSystem &walls) const
    {
        (void) x1;
        (void) y1;
        (void) x2;
        (void) y2;
        (void) state;
        (void) generator;
        (void) nav;
        (void) walls;
        return {};
    }

    /// Применяет инструмент по координатам (x1, y1)->(x2, y2) и возвращает true при успехе.
    virtual bool activate(float x1, float y1, float x2, float y2, ToolExecutionContext &ctx) = 0;
};

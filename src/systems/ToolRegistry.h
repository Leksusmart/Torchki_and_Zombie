#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>
#include "core/IGameTool.h"

/**
 * @brief Единый реестр всех игровых инструментов (активных и заблокированных).
 *
 * Служит единственным источником правды для GameWorld, ToolBar.qml и ToolkitMenu.qml:
 * устраняет дублирование списков инструментов и ветвления if/else по toolId.
 */
class ToolRegistry
{
public:
    ToolRegistry();

    /// Возвращает список категорий с инструментами для меню арсенала (ToolkitMenu.qml).
    QVariantList categoriesModel() const { return categoriesModel_; }

    /// Возвращает плоский список активных (разблокированных) инструментов для нижней панели.
    QVariantList availableToolsModel() const { return availableToolsModel_; }

    /// Ищет зарегистрированный инструмент по его ID (или возвращает nullptr).
    const IGameTool *findTool(int toolId) const;
    IGameTool *findTool(int toolId);

    /// Возвращает данные предпросмотра для инструмента toolId.
    QVariantMap previewTool(int toolId,
                            float x1,
                            float y1,
                            float x2,
                            float y2,
                            const WorldState &state,
                            const MapGenerator &generator,
                            const NavigationService &nav,
                            const WallSystem &walls) const;

    /// Применяет инструмент toolId в указанных координатах через ToolExecutionContext.
    bool executeTool(int toolId, float x1, float y1, float x2, float y2, ToolExecutionContext &ctx);

private:
    /// Регистрирует активный инструмент и добавляет его в список доступных.
    void registerTool(std::unique_ptr<IGameTool> tool);

    /// Формирует структуру категорий (активные + заблокированные) для QML.
    void buildCategoryModels();

private:
    std::vector<std::unique_ptr<IGameTool>> tools_;
    std::vector<ToolDescriptor> catalog_;
    QVariantList categoriesModel_;
    QVariantList availableToolsModel_;
};

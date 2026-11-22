#include "systems/ToolRegistry.h"
#include "MapGenerator.h"
#include "core/WorldState.h"
#include "systems/EffectsSystem.h"
#include "systems/EvacuationSystem.h"
#include "systems/HelicopterSystem.h"
#include "systems/NavigationService.h"
#include "systems/WallSystem.h"

namespace {

/// Инструмент 1: Вакцинирующая капсула (лечит и вакцинирует в радиусе 45 м).
class VaccineTool final : public IGameTool
{
public:
    VaccineTool()
    {
        desc_.id = 1;
        desc_.category = "МЕДИЦИНА И РАЗВЕДКА";
        desc_.title = "Вакцинирующая капсула";
        desc_.desc = "Лечит и вакцинирует в радиусе 45м";
        desc_.cost = 90;
        desc_.costText = "90 ₽";
        desc_.accent = "#4299e1";
        desc_.inputMode = ToolInputMode::Click;
    }

    const ToolDescriptor &descriptor() const override { return desc_; }

    bool activate(float x1, float y1, float x2, float y2, ToolExecutionContext &ctx) override
    {
        // Задачи:
        // 1. Проверить попадание в городскую зону и достаточность бюджета (90 ₽)
        // 2. Вакцинировать всех жителей и зомби в радиусе 45 м
        (void) x2;
        (void) y2;
        if (!ctx.nav.isCityArea(x1, y1, ctx.generator) || ctx.state.budget < desc_.cost)
            return false;

        ctx.state.budget -= desc_.cost;
        for (auto &u : ctx.state.units) {
            const float dx = u.x - x1;
            const float dy = u.y - y1;
            if (dx * dx + dy * dy <= 45.0F * 45.0F
                && (u.type == UnitType::Zombie || u.type == UnitType::Citizen)) {
                u.type = UnitType::Vaccinated;
                u.isInfected = false;
                u.hp = 2;
            }
        }
        ctx.emitPulseWave(x1, y1, 45.0F, desc_.accent);
        ctx.effects.spawnBurstParticles(x1, y1, 16, 120, 220, 255);
        ctx.rebuildUnitRender();
        ctx.unitsChanged = true;
        ctx.statsChanged = true;
        return true;
    }

private:
    ToolDescriptor desc_;
};

/// Инструмент 2: Отряд десанта (вызывает вертолёт с 3 солдатами).
class ParatrooperTool final : public IGameTool
{
public:
    ParatrooperTool()
    {
        desc_.id = 2;
        desc_.category = "ВОЙСКА";
        desc_.title = "Отряд десанта";
        desc_.desc = "Высадка солдат в точку";
        desc_.cost = 200;
        desc_.costText = "200 ₽";
        desc_.accent = "#48bb78";
        desc_.inputMode = ToolInputMode::Click;
    }

    const ToolDescriptor &descriptor() const override { return desc_; }

    bool activate(float x1, float y1, float x2, float y2, ToolExecutionContext &ctx) override
    {
        // Задачи:
        // 1. Проверить городскую зону, бюджет (200 ₽) и ближайшую проходимую точку высадки
        // 2. Вызвать вертолёт в найденную точку
        (void) x2;
        (void) y2;
        if (!ctx.nav.isCityArea(x1, y1, ctx.generator) || ctx.state.budget < desc_.cost)
            return false;

        const QPointF dropPos = ctx.nav.findNearestWalkable(x1, y1, 40.0F, ctx.state.navGrid);
        if (dropPos.x() < 0.0F)
            return false;

        ctx.state.budget -= desc_.cost;
        ctx.helicopters.callDropHelicopter(static_cast<float>(dropPos.x()),
                                           static_cast<float>(dropPos.y()));
        ctx.helicoptersChanged = ctx.helicopters.consumeChangedFlag();
        ctx.statsChanged = true;
        return true;
    }

private:
    ToolDescriptor desc_;
};

/// Инструмент 3: Точка эвакуации (инициирует эвакуацию на выходе с привязкой 20 м).
class EvacuationTool final : public IGameTool
{
public:
    EvacuationTool()
    {
        desc_.id = 3;
        desc_.category = "ЭВАКУАЦИЯ";
        desc_.title = "Точка эвакуации";
        desc_.desc = "Инициация эвакуации на выходе";
        desc_.cost = 75;
        desc_.costText = "75 ₽";
        desc_.accent = "#38b2ac";
        desc_.inputMode = ToolInputMode::SnapClick;
        desc_.previewRadius = 250.0F;
    }

    const ToolDescriptor &descriptor() const override { return desc_; }

    QVariantMap preview(float x1,
                        float y1,
                        float x2,
                        float y2,
                        const WorldState &state,
                        const MapGenerator &generator,
                        const NavigationService &nav,
                        const WallSystem &walls) const override
    {
        (void) x2;
        (void) y2;
        (void) state;
        (void) walls;
        return nav.snapEvacPoint(x1, y1, 20.0F, generator.nodes());
    }

    bool activate(float x1, float y1, float x2, float y2, ToolExecutionContext &ctx) override
    {
        // Задачи:
        // 1. Привязать клик к ближайшему эвакуационному выходу в радиусе 20 м
        // 2. Запустить операцию эвакуации и волну оповещения
        (void) x2;
        (void) y2;
        if (!ctx.nav.isCityArea(x1, y1, ctx.generator) || ctx.state.budget < desc_.cost)
            return false;

        const QVariantMap snap = ctx.nav.snapEvacPoint(x1, y1, 20.0F, ctx.generator.nodes());
        if (!snap["snapped"].toBool())
            return false;

        const int nodeId = snap["nodeId"].toInt();
        const QPointF evacPos(snap["x"].toDouble(), snap["y"].toDouble());
        if (!ctx.evacuation.startEvacuation(nodeId, evacPos, ctx.generator.evacRoutes(), ctx.state))
            return false;

        ctx.emitPulseWave(static_cast<float>(evacPos.x()),
                          static_cast<float>(evacPos.y()),
                          100.0F,
                          desc_.accent);
        ctx.evacuationsChanged = ctx.evacuation.consumeChangedFlag();
        ctx.statsChanged = true;
        return true;
    }

private:
    ToolDescriptor desc_;
};

/// Инструмент 4: Карантинная стена (протяжка отрезка вдоль улицы).
class QuarantineWallTool final : public IGameTool
{
public:
    QuarantineWallTool()
    {
        desc_.id = 4;
        desc_.category = "ОБОРОНА";
        desc_.title = "Карантинная стена";
        desc_.desc = "Перекрывает улицу";
        desc_.cost = 25;
        desc_.costText = "25 ₽+";
        desc_.accent = "#fc8181";
        desc_.inputMode = ToolInputMode::DragLine;
        desc_.hintText = "Зажмите ЛКМ и протяните вдоль улицы";
    }

    const ToolDescriptor &descriptor() const override { return desc_; }

    QVariantMap preview(float x1,
                        float y1,
                        float x2,
                        float y2,
                        const WorldState &state,
                        const MapGenerator &generator,
                        const NavigationService &nav,
                        const WallSystem &walls) const override
    {
        (void) nav;
        return walls.previewWall(x1, y1, x2, y2, state.budget, generator.walkGrid(), state.navGrid);
    }

    bool activate(float x1, float y1, float x2, float y2, ToolExecutionContext &ctx) override
    {
        // Задачи:
        // 1. Разместить секции карантинной стены вдоль отрезка (x1,y1)->(x2,y2)
        if (!ctx.walls.placeWall(x1, y1, x2, y2, ctx.generator.walkGrid(), ctx.state))
            return false;

        ctx.mapUpdated = ctx.walls.consumeMapDirtyFlag();
        ctx.wallsChanged = ctx.walls.consumeOverlaysDirtyFlag();
        ctx.statsChanged = true;
        return true;
    }

private:
    ToolDescriptor desc_;
};

} // namespace

ToolRegistry::ToolRegistry()
{
    // Задачи:
    // 1. Зарегистрировать все активные инструменты симуляции
    // 2. Сформировать единый каталог (активные + заблокированные) для QML

    registerTool(std::make_unique<VaccineTool>());
    registerTool(std::make_unique<ParatrooperTool>());
    registerTool(std::make_unique<EvacuationTool>());
    registerTool(std::make_unique<QuarantineWallTool>());

    buildCategoryModels();
}

void ToolRegistry::registerTool(std::unique_ptr<IGameTool> tool)
{
    availableToolsModel_.append(tool->descriptor().toVariantMap());
    tools_.push_back(std::move(tool));
}

const IGameTool *ToolRegistry::findTool(int toolId) const
{
    for (const auto &t : tools_) {
        if (t->descriptor().id == toolId)
            return t.get();
    }
    return nullptr;
}

IGameTool *ToolRegistry::findTool(int toolId)
{
    for (auto &t : tools_) {
        if (t->descriptor().id == toolId)
            return t.get();
    }
    return nullptr;
}

QVariantMap ToolRegistry::previewTool(int toolId,
                                      float x1,
                                      float y1,
                                      float x2,
                                      float y2,
                                      const WorldState &state,
                                      const MapGenerator &generator,
                                      const NavigationService &nav,
                                      const WallSystem &walls) const
{
    if (const auto *tool = findTool(toolId))
        return tool->preview(x1, y1, x2, y2, state, generator, nav, walls);
    return {};
}

bool ToolRegistry::executeTool(
    int toolId, float x1, float y1, float x2, float y2, ToolExecutionContext &ctx)
{
    if (auto *tool = findTool(toolId))
        return tool->activate(x1, y1, x2, y2, ctx);
    return false;
}

void ToolRegistry::buildCategoryModels()
{
    // Задачи:
    // 1. Сгруппировать активные и запланированные (заблокированные) инструменты по категориям
    // 2. Сформировать QVariantList для прямого биндинга в ToolkitMenu.qml

    auto makeLocked = [](int id,
                         const QString &cat,
                         const QString &title,
                         const QString &costText,
                         const QString &accent,
                         const QString &slotsTag = QString()) -> ToolDescriptor {
        ToolDescriptor d;
        d.id = id;
        d.category = cat;
        d.title = title;
        d.costText = costText;
        d.accent = accent;
        d.locked = true;
        d.lockText = "ЗАБЛОКИРОВАНО";
        d.slotsTag = slotsTag;
        return d;
    };

    const QStringList categoryOrder = {"ОБОРОНА",
                                       "ВОЙСКА",
                                       "АТАКИ",
                                       "МЕДИЦИНА И РАЗВЕДКА",
                                       "ЭКОНОМИКА",
                                       "ЭВАКУАЦИЯ"};

    std::vector<ToolDescriptor> allItems;
    for (const auto &t : tools_)
        allItems.push_back(t->descriptor());

    // Заблокированные инструменты арсенала
    allItems.push_back(makeLocked(102, "ОБОРОНА", "Блокпост", "120 ₽+", "#38b2ac"));
    allItems.push_back(makeLocked(103, "ОБОРОНА", "Укрепление", "250 ₽", "#ecc94b"));
    allItems.push_back(makeLocked(104, "ОБОРОНА", "Снайперское гнездо", "450 ₽", "#fc8181"));
    allItems.push_back(makeLocked(105, "ОБОРОНА", "Минное поле", "550 ₽", "#f6ad55"));

    allItems.push_back(makeLocked(201, "ВОЙСКА", "Чинук", "900 ₽", "#63b3ed", "2 СЛОТА"));
    allItems.push_back(makeLocked(202, "ВОЙСКА", "Сигнальная ракета", "350 ₽", "#f56565"));
    allItems.push_back(makeLocked(203, "ВОЙСКА", "Отряд зачистки", "400 ₽", "#a0aec0"));

    allItems.push_back(makeLocked(301, "АТАКИ", "Ковровая бомбардировка", "300 ₽", "#ed8936"));
    allItems.push_back(makeLocked(302, "АТАКИ", "Авиаудар", "700 ₽", "#f6ad55"));
    allItems.push_back(makeLocked(303, "АТАКИ", "Напалмовый удар", "800 ₽", "#dd6b20"));
    allItems.push_back(makeLocked(304, "АТАКИ", "Боевой вертолёт", "650 ₽", "#cbd5e0", "2 СЛОТА"));
    allItems.push_back(makeLocked(305, "АТАКИ", "Апач", "850 ₽", "#ecc94b", "2 СЛОТА"));
    allItems.push_back(makeLocked(306, "АТАКИ", "Миномётный обстрел", "900 ₽", "#fc8181"));
    allItems.push_back(makeLocked(307, "АТАКИ", "Звуковая приманка", "300 ₽", "#b794f4"));
    allItems.push_back(makeLocked(308, "АТАКИ", "Дымовая завеса", "450 ₽", "#a0aec0"));

    allItems.push_back(makeLocked(401, "МЕДИЦИНА И РАЗВЕДКА", "Разведдрон", "40 ₽", "#38b2ac"));
    allItems.push_back(makeLocked(402, "МЕДИЦИНА И РАЗВЕДКА", "Вышка разведки", "250 ₽", "#4fd1c5"));
    allItems.push_back(makeLocked(403, "МЕДИЦИНА И РАЗВЕДКА", "Палатка вакцинации", "600 ₽", "#63b3ed"));

    allItems.push_back(makeLocked(501, "ЭКОНОМИКА", "Склад припасов", "250 ₽", "#ecc94b"));
    allItems.push_back(makeLocked(502, "ЭКОНОМИКА", "Продовольственный склад", "400 ₽", "#d69e2e"));

    allItems.push_back(makeLocked(601, "ЭВАКУАЦИЯ", "Эвакуация с крыши", "150 ₽", "#38b2ac"));
    allItems.push_back(makeLocked(602, "ЭВАКУАЦИЯ", "Полевое убежище", "500 ₽", "#68d391"));
    allItems.push_back(makeLocked(603, "ЭВАКУАЦИЯ", "Зона эвакуации", "ВСЕГДА", "#ecc94b"));

    categoriesModel_.clear();
    for (const QString &cat : categoryOrder) {
        QVariantList items;
        for (const auto &d : allItems) {
            if (d.category == cat)
                items.append(d.toVariantMap());
        }
        QVariantMap catMap;
        catMap["name"] = cat;
        catMap["items"] = items;
        categoriesModel_.append(catMap);
    }
}

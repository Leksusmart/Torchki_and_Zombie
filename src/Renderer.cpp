#include "Renderer.h"
#include <QQuickWindow>
#include <cmath>

namespace {
constexpr size_t LAYER_COUNT = static_cast<size_t>(RenderLayerId::Count);

struct LayerNodePair
{
    QSGGeometryNode *quads = nullptr;
    QSGGeometryNode *circles = nullptr;
};

/// Корневой узел сцены с прямыми ссылками на слои вместо обращения по числовым индексам.
struct SceneRootNode : public QSGNode
{
    QSGSimpleTextureNode *bgNode = nullptr;
    std::array<LayerNodePair, LAYER_COUNT> layers{};
};

/// Создаёт стандартный QSGGeometryNode для треугольной геометрии с вершинными цветами.
QSGGeometryNode *createColoredTriangleNode()
{
    auto *node = new QSGGeometryNode();
    auto *geom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
    geom->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geom);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    node->setMaterial(new QSGVertexColorMaterial());
    return node;
}

/// Возвращает true для слоёв, которые перестраиваются только при изменении карты/стен/зданий.
bool isStaticLayer(RenderLayerId id)
{
    return id == RenderLayerId::Ground || id == RenderLayerId::Buildings
           || id == RenderLayerId::Obstacles || id == RenderLayerId::ObstacleCaps;
}
} // namespace

Renderer::Renderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

/*Q_PROPERTY*/ void Renderer::setWorld(GameWorld *world)
{
    // Задачи:
    // 1. Отписаться от старого экземпляра GameWorld
    // 2. Подписаться на сигналы mapUpdated и unitsUpdated нового мира
    // 3. Пометить буферы как грязные и запросить перерисовку

    if (g_world == world)
        return;
    if (g_world)
        disconnect(g_world, nullptr, this, nullptr);

    g_world = world;
    if (g_world) {
        connect(g_world, &GameWorld::mapUpdated, this, [this]() {
            g_mapDirty = true;
            update();
        });
        connect(g_world, &GameWorld::unitsUpdated, this, [this]() {
            g_unitsDirty = true;
            update();
        });
        g_mapDirty = true;
        g_unitsDirty = true;
        update();
    }
    emit worldChanged();
}

void Renderer::collectLayersFromWorld(
    std::array<RenderLayerData, LAYER_COUNT> &layers,
    bool includeStatic,
    bool includeDynamic) const
{
    // Задачи:
    // 1. Заполнить статические слои (здания, препятствия) при обновлении карты
    // 2. Заполнить динамические слои (пути, юниты, оверлеи, эффекты) на каждом тике

    if (!g_world)
        return;

    // 1. Статические слои карты
    if (includeStatic) {
        auto &buildings = layers[static_cast<size_t>(RenderLayerId::Buildings)];
        buildings.isStaticMapLayer = true;
        buildings.quads = g_world->buildingsGeometry();

        auto &obstacles = layers[static_cast<size_t>(RenderLayerId::Obstacles)];
        obstacles.isStaticMapLayer = true;
        obstacles.quads = g_world->wallsGeometry();

        auto &caps = layers[static_cast<size_t>(RenderLayerId::ObstacleCaps)];
        caps.isStaticMapLayer = true;
        caps.circles = g_world->wallsCapsGeometry();
        caps.circleSegments = 16;
    }

    // 2. Динамические слои симуляции
    if (includeDynamic) {
        layers[static_cast<size_t>(RenderLayerId::Paths)].quads = g_world->selectedPathGeometry();

        auto &units = layers[static_cast<size_t>(RenderLayerId::Units)];
        units.circles = g_world->unitGeometry();
        units.circleSegments = 10;

        layers[static_cast<size_t>(RenderLayerId::Overlays)].quads = g_world->buildingOverlaysGeometry();
        layers[static_cast<size_t>(RenderLayerId::Particles)].quads = g_world->effectsGeometry();
    }
}

QSGNode *Renderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    // Задачи:
    // 1. При первом вызове создать корневой узел SceneRootNode и все слои в порядке RenderLayerId
    // 2. Обновить фоновую текстуру и статические слои, если установлен g_mapDirty
    // 3. Обновить динамические слои (юниты, пути, оверлеи, эффекты), если установлен g_unitsDirty

    if (!g_world || !window())
        return oldNode;

    auto *rootNode = static_cast<SceneRootNode *>(oldNode);

    // 1. Инициализация дерева узлов сцены
    if (!rootNode) {
        rootNode = new SceneRootNode();

        rootNode->bgNode = new QSGSimpleTextureNode();
        rootNode->bgNode->setOwnsTexture(true);
        rootNode->appendChildNode(rootNode->bgNode);

        for (size_t i = 0; i < LAYER_COUNT; ++i) {
            rootNode->layers[i].quads = createColoredTriangleNode();
            rootNode->layers[i].circles = createColoredTriangleNode();
            rootNode->appendChildNode(rootNode->layers[i].quads);
            rootNode->appendChildNode(rootNode->layers[i].circles);
        }
    }

    if (!g_mapDirty && !g_unitsDirty)
        return rootNode;

    // 2. Обновление фоновой текстуры карты
    if (g_mapDirty) {
        const QImage &bgImg = g_world->backgroundImage();
        if (!bgImg.isNull()) {
            rootNode->bgNode->setTexture(window()->createTextureFromImage(bgImg));
            rootNode->bgNode->setRect(0, 0, bgImg.width(), bgImg.height());
        }
    }

    // 3. Сбор и загрузка геометрии по слоям
    std::array<RenderLayerData, LAYER_COUNT> layerData;
    collectLayersFromWorld(layerData, g_mapDirty, g_unitsDirty);

    for (size_t i = 0; i < LAYER_COUNT; ++i) {
        const auto layerId = static_cast<RenderLayerId>(i);
        const bool shouldUpdate = isStaticLayer(layerId) ? g_mapDirty : g_unitsDirty;
        if (!shouldUpdate)
            continue;

        const auto &data = layerData[i];
        auto &nodes = rootNode->layers[i];

        fillGeometry(nodes.quads->geometry(), data.quads);
        nodes.quads->markDirty(QSGNode::DirtyGeometry);

        fillCircleGeometry(nodes.circles->geometry(), data.circles, data.circleSegments);
        nodes.circles->markDirty(QSGNode::DirtyGeometry);
    }

    g_mapDirty = false;
    g_unitsDirty = false;
    return rootNode;
}

void Renderer::fillCircleGeometry(QSGGeometry *geometry,
                                  const std::vector<RenderQuad> &circles,
                                  int segments)
{
    // Задачи:
    // 1. Выделить вершинный буфер под segments треугольников на каждый круг
    // 2. Пересчитать кэш sin/cos при смене числа сегментов
    // 3. Сформировать треугольники веером от центра круга к его границе

    // 1. Выделение памяти под вершины
    geometry->allocate(static_cast<int>(circles.size() * segments * 3));
    auto *v = geometry->vertexDataAsColoredPoint2D();

    // 2. Предвычисленная таблица sin/cos для сегментов круга
    static std::vector<float> cosTable;
    static std::vector<float> sinTable;
    if (static_cast<int>(cosTable.size()) != segments + 1) {
        cosTable.resize(segments + 1);
        sinTable.resize(segments + 1);
        for (int s = 0; s <= segments; ++s) {
            float angle = (6.2831853F * s) / segments;
            cosTable[s] = std::cos(angle);
            sinTable[s] = std::sin(angle);
        }
    }

    // 3. Построение треугольников
    int idx = 0;
    for (const auto &c : circles) {
        const float radius = c.w * 0.5F;

        for (int s = 0; s < segments; ++s) {
            const float x1 = c.cx + radius * cosTable[s];
            const float y1 = c.cy + radius * sinTable[s];
            const float x2 = c.cx + radius * cosTable[s + 1];
            const float y2 = c.cy + radius * sinTable[s + 1];

            v[idx++].set(c.cx, c.cy, c.r, c.g, c.b, c.a);
            v[idx++].set(x1, y1, c.r, c.g, c.b, c.a);
            v[idx++].set(x2, y2, c.r, c.g, c.b, c.a);
        }
    }
}

void Renderer::fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads)
{
    // Задачи:
    // 1. Выделить память под 6 вершин (2 треугольника) на каждый RenderQuad
    // 2. Повернуть локальные углы прямоугольника на angleRad и записать в буфер

    geometry->allocate(static_cast<int>(quads.size() * 6));
    auto *v = geometry->vertexDataAsColoredPoint2D();

    int idx = 0;
    for (const auto &q : quads) {
        const float hw = q.w * 0.5F;
        const float hh = q.h * 0.5F;
        const float c = std::cos(q.angleRad);
        const float s = std::sin(q.angleRad);

        auto rotX = [&](float lx, float ly) { return q.cx + (lx * c - ly * s); };
        auto rotY = [&](float lx, float ly) { return q.cy + (lx * s + ly * c); };

        const float x1 = rotX(-hw, -hh), y1 = rotY(-hw, -hh);
        const float x2 = rotX(hw, -hh), y2 = rotY(hw, -hh);
        const float x3 = rotX(-hw, hh), y3 = rotY(-hw, hh);
        const float x4 = rotX(hw, hh), y4 = rotY(hw, hh);

        v[idx++].set(x1, y1, q.r, q.g, q.b, q.a);
        v[idx++].set(x2, y2, q.r, q.g, q.b, q.a);
        v[idx++].set(x3, y3, q.r, q.g, q.b, q.a);

        v[idx++].set(x2, y2, q.r, q.g, q.b, q.a);
        v[idx++].set(x4, y4, q.r, q.g, q.b, q.a);
        v[idx++].set(x3, y3, q.r, q.g, q.b, q.a);
    }
}

#include "Renderer.h"
#include <QQuickWindow>
#include <cmath>

Renderer::Renderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

/*Q_PROPERTY*/ void Renderer::setWorld(GameWorld *world)
{
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

QSGNode *Renderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!g_world || !window())
        return oldNode;

    QSGNode *rootNode = oldNode;
    QSGSimpleTextureNode *bgNode = nullptr;
    QSGGeometryNode *buildingsNode = nullptr;
    QSGGeometryNode *wallsNode = nullptr;
    QSGGeometryNode *wallsCapsNode = nullptr;
    QSGGeometryNode *pathNode = nullptr;
    QSGGeometryNode *unitsNode = nullptr;
    QSGGeometryNode *overlaysNode = nullptr;
    QSGGeometryNode *effectsNode = nullptr;

    if (!rootNode) {
        rootNode = new QSGNode();

        // Узел 0: Текстура фона
        bgNode = new QSGSimpleTextureNode();
        bgNode->setOwnsTexture(true);
        rootNode->appendChildNode(bgNode);

        // Узел 1: Повернутые здания
        buildingsNode = new QSGGeometryNode();
        auto *bGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        bGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        buildingsNode->setGeometry(bGeom);
        buildingsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        buildingsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(buildingsNode);

        // Узел 2: Карантинные стены (тело)
        wallsNode = new QSGGeometryNode();
        auto *wGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        wGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        wallsNode->setGeometry(wGeom);
        wallsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        wallsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(wallsNode);

        // Узел 3: Закруглённые торцы карантинных стен
        wallsCapsNode = new QSGGeometryNode();
        auto *wcGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        wcGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        wallsCapsNode->setGeometry(wcGeom);
        wallsCapsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        wallsCapsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(wallsCapsNode);

        // Узел 4: Маршрут выбранного юнита
        pathNode = new QSGGeometryNode();
        auto *pGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        pGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        pathNode->setGeometry(pGeom);
        pathNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        pathNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(pathNode);

        // Узел 5: Юниты
        unitsNode = new QSGGeometryNode();
        auto *uGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        uGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        unitsNode->setGeometry(uGeom);
        unitsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        unitsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(unitsNode);

        // Узел 6: Оверлеи
        overlaysNode = new QSGGeometryNode();
        auto *oGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        oGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        overlaysNode->setGeometry(oGeom);
        overlaysNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        overlaysNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(overlaysNode);

        // Узел 7: Партиклы
        effectsNode = new QSGGeometryNode();
        auto *eGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        eGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        effectsNode->setGeometry(eGeom);
        effectsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        effectsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(effectsNode);
    } else {
        bgNode = static_cast<QSGSimpleTextureNode *>(rootNode->childAtIndex(0));
        buildingsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(1));
        wallsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(2));
        wallsCapsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(3));
        pathNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(4));
        unitsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(5));
        overlaysNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(6));
        effectsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(7));
    }

    if (g_mapDirty) {
        const QImage &bgImg = g_world->backgroundImage();
        if (!bgImg.isNull()) {
            bgNode->setTexture(window()->createTextureFromImage(bgImg));
            bgNode->setRect(0, 0, bgImg.width(), bgImg.height());
        }
        fillGeometry(buildingsNode->geometry(), g_world->buildingsGeometry());
        buildingsNode->markDirty(QSGNode::DirtyGeometry);

        fillGeometry(wallsNode->geometry(), g_world->wallsGeometry());
        wallsNode->markDirty(QSGNode::DirtyGeometry);

        fillCircleGeometry(wallsCapsNode->geometry(), g_world->wallsCapsGeometry(), 16);
        wallsCapsNode->markDirty(QSGNode::DirtyGeometry);

        g_mapDirty = false;
    }

    if (g_unitsDirty) {
        fillGeometry(pathNode->geometry(), g_world->selectedPathGeometry());
        pathNode->markDirty(QSGNode::DirtyGeometry);

        fillCircleGeometry(unitsNode->geometry(), g_world->unitGeometry(), 10);
        unitsNode->markDirty(QSGNode::DirtyGeometry);

        fillGeometry(overlaysNode->geometry(), g_world->buildingOverlaysGeometry());
        overlaysNode->markDirty(QSGNode::DirtyGeometry);

        fillGeometry(effectsNode->geometry(), g_world->effectsGeometry());
        effectsNode->markDirty(QSGNode::DirtyGeometry);

        g_unitsDirty = false;
    }

    return rootNode;
}

void Renderer::fillCircleGeometry(QSGGeometry *geometry,
                                  const std::vector<RenderQuad> &circles,
                                  int segments)
{
    // Каждый сегмент круга — это 1 треугольник от центра к краю (3 вершины)
    geometry->allocate(static_cast<int>(circles.size() * segments * 3));
    auto *v = geometry->vertexDataAsColoredPoint2D();

    // Предвычисляем таблицу sin/cos для сегментов круга один раз
    static std::vector<float> cosTable;
    static std::vector<float> sinTable;
    if (static_cast<int>(cosTable.size()) != segments + 1) {
        cosTable.resize(segments + 1);
        sinTable.resize(segments + 1);
        for (int s = 0; s <= segments; ++s) {
            float angle = (6.2831853f * s) / segments;
            cosTable[s] = std::cos(angle);
            sinTable[s] = std::sin(angle);
        }
    }

    int idx = 0;
    for (const auto &c : circles) {
        float radius = c.w * 0.5f;

        for (int s = 0; s < segments; ++s) {
            float x1 = c.cx + radius * cosTable[s];
            float y1 = c.cy + radius * sinTable[s];
            float x2 = c.cx + radius * cosTable[s + 1];
            float y2 = c.cy + radius * sinTable[s + 1];

            // Треугольник: центр круга -> точка s -> точка s+1
            v[idx++].set(c.cx, c.cy, c.r, c.g, c.b, c.a);
            v[idx++].set(x1, y1, c.r, c.g, c.b, c.a);
            v[idx++].set(x2, y2, c.r, c.g, c.b, c.a);
        }
    }
}

void Renderer::fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads)
{
    geometry->allocate(static_cast<int>(quads.size() * 6));
    auto *v = geometry->vertexDataAsColoredPoint2D();

    int idx = 0;
    for (const auto &q : quads) {
        float hw = q.w * 0.5f;
        float hh = q.h * 0.5f;
        float c = std::cos(q.angleRad);
        float s = std::sin(q.angleRad);

        auto rotX = [&](float lx, float ly) { return q.cx + (lx * c - ly * s); };
        auto rotY = [&](float lx, float ly) { return q.cy + (lx * s + ly * c); };

        float x1 = rotX(-hw, -hh), y1 = rotY(-hw, -hh);
        float x2 = rotX(hw, -hh), y2 = rotY(hw, -hh);
        float x3 = rotX(-hw, hh), y3 = rotY(-hw, hh);
        float x4 = rotX(hw, hh), y4 = rotY(hw, hh);

        v[idx++].set(x1, y1, q.r, q.g, q.b, q.a);
        v[idx++].set(x2, y2, q.r, q.g, q.b, q.a);
        v[idx++].set(x3, y3, q.r, q.g, q.b, q.a);

        v[idx++].set(x2, y2, q.r, q.g, q.b, q.a);
        v[idx++].set(x4, y4, q.r, q.g, q.b, q.a);
        v[idx++].set(x3, y3, q.r, q.g, q.b, q.a);
    }
}

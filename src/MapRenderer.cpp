#include "MapRenderer.h"
#include <QQuickWindow>
#include <cmath>

MapRenderer::MapRenderer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

void MapRenderer::setWorld(GameWorld *world)
{
    if (m_world == world)
        return;
    if (m_world)
        disconnect(m_world, nullptr, this, nullptr);

    m_world = world;
    if (m_world) {
        connect(m_world, &GameWorld::mapUpdated, this, [this]() {
            m_mapDirty = true;
            update();
        });
        connect(m_world, &GameWorld::unitsUpdated, this, [this]() {
            m_unitsDirty = true;
            update();
        });
        m_mapDirty = true;
        m_unitsDirty = true;
        update();
    }
    emit worldChanged();
}

QSGNode *MapRenderer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!m_world || !window())
        return oldNode;

    QSGNode *rootNode = oldNode;
    QSGSimpleTextureNode *bgNode = nullptr;
    QSGGeometryNode *buildingsNode = nullptr;
    QSGGeometryNode *unitsNode = nullptr;
    QSGGeometryNode *overlaysNode = nullptr;
    QSGGeometryNode *effectsNode = nullptr;

    if (!rootNode) {
        rootNode = new QSGNode();

        // Узел 0: Текстура фона (море, парки, проложенные дороги)
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

        // Узел 2: Юниты
        unitsNode = new QSGGeometryNode();
        auto *uGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        uGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        unitsNode->setGeometry(uGeom);
        unitsNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        unitsNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(unitsNode);

        // Узел 3: Оверлеи (полоски заполненности и здоровья зданий)
        overlaysNode = new QSGGeometryNode();
        auto *oGeom = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        oGeom->setDrawingMode(QSGGeometry::DrawTriangles);
        overlaysNode->setGeometry(oGeom);
        overlaysNode->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
        overlaysNode->setMaterial(new QSGVertexColorMaterial());
        rootNode->appendChildNode(overlaysNode);

        // Узел 4: Партиклы
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
        unitsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(2));
        overlaysNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(3));
        effectsNode = static_cast<QSGGeometryNode *>(rootNode->childAtIndex(4));
    }

    if (m_mapDirty) {
        const QImage &bgImg = m_world->backgroundImage();
        if (!bgImg.isNull()) {
            bgNode->setTexture(window()->createTextureFromImage(bgImg));
            bgNode->setRect(0, 0, bgImg.width(), bgImg.height());
        }
        fillGeometry(buildingsNode->geometry(), m_world->buildingsGeometry());
        buildingsNode->markDirty(QSGNode::DirtyGeometry);
        m_mapDirty = false;
    }

    if (m_unitsDirty) {
        fillCircleGeometry(unitsNode->geometry(), m_world->unitGeometry(), 7);
        unitsNode->markDirty(QSGNode::DirtyGeometry);

        fillGeometry(overlaysNode->geometry(), m_world->buildingOverlaysGeometry());
        overlaysNode->markDirty(QSGNode::DirtyGeometry);

        fillGeometry(effectsNode->geometry(), m_world->effectsGeometry());
        effectsNode->markDirty(QSGNode::DirtyGeometry);

        m_unitsDirty = false;
    }

    return rootNode;
}

void MapRenderer::fillCircleGeometry(QSGGeometry *geometry,
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

void MapRenderer::fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads)
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

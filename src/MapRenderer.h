#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>
#include <QtQml/qqmlregistration.h>
#include "GameWorld.h"

class MapRenderer : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(GameWorld *world READ world WRITE setWorld NOTIFY worldChanged)

public:
    explicit MapRenderer(QQuickItem *parent = nullptr);

    GameWorld *world() const { return m_world; }
    void setWorld(GameWorld *world);

signals:
    void worldChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    void fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads);
    void fillCircleGeometry(QSGGeometry *geometry,
                            const std::vector<RenderQuad> &circles,
                            int segments = 12);

    GameWorld *m_world = nullptr;
    bool m_mapDirty = true;
    bool m_unitsDirty = true;
};

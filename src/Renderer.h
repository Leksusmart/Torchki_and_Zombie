#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>
#include <QtQml/qqmlregistration.h>
#include "GameWorld.h"

class Renderer : public QQuickItem
{
public:
    explicit Renderer(QQuickItem *parent = nullptr);
    GameWorld *world() const { return g_world; }

private: // функции
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads);
    void fillCircleGeometry(QSGGeometry *geometry,
                            const std::vector<RenderQuad> &circles,
                            int segments = 7);

private: // переменные
    GameWorld *g_world = nullptr;
    bool g_mapDirty = true;
    bool g_unitsDirty = true;

private:
    // QML BEGIN
    // QML связи
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(GameWorld *world READ world WRITE setWorld NOTIFY worldChanged)
public:
    // QML свойства
    /*Q_PROPERTY*/ void setWorld(GameWorld *world);
signals:
    // QML свойства изменились
    /*Q_PROPERTY*/ void worldChanged();
    // QML END
};

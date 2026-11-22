#pragma once
#include <QQuickItem>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>
#include <QtQml/qqmlregistration.h>
#include <array>
#include "GameWorld.h"
#include "core/RenderLayers.h"

/**
 * @brief QQuickItem-рендерер игровой сцены через Qt Scene Graph.
 *
 * Строит дерево узлов в фиксированном Z-порядке слоёв (RenderLayerId),
 * разделяя статическую геометрию карты (mapUpdated) и динамические объекты (unitsUpdated).
 */
class Renderer : public QQuickItem
{
public:
    explicit Renderer(QQuickItem *parent = nullptr);

    /// Возвращает текущий объект игрового мира.
    GameWorld *world() const { return g_world; }

private: // функции
    /// Создаёт и обновляет дерево узлов Qt Scene Graph при перерисовке кадра.
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

    /// Собирает геометрию всех слоёв из текущего состояния GameWorld.
    void collectLayersFromWorld(
        std::array<RenderLayerData, static_cast<size_t>(RenderLayerId::Count)> &layers,
        bool includeStatic,
        bool includeDynamic) const;

    /// Заполняет вершинный буфер прямоугольниками (2 треугольника на каждый RenderQuad).
    void fillGeometry(QSGGeometry *geometry, const std::vector<RenderQuad> &quads);

    /// Заполняет вершинный буфер аппроксимированными кругами из треугольников.
    void fillCircleGeometry(QSGGeometry *geometry,
                            const std::vector<RenderQuad> &circles,
                            int segments = 12);

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
    /// Привязывает экземпляр GameWorld и подписывается на сигналы обновления карты и юнитов.
    /*Q_PROPERTY*/ void setWorld(GameWorld *world);
signals:
    // QML свойства изменились
    /*Q_PROPERTY*/ void worldChanged();
    // QML END
};

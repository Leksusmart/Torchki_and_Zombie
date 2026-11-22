import QtQuick 2.15
import QtQuick.Controls 2.15

import TorchkiAndZombie

Window {
    id: rootWindow
    width: 1280
    height: 720
    visible: true
    title: "Torchki and Zombie"
    color: gameWorld.waterColor

    GameWorld {
        id: gameWorld
    }

    // Игровая область с камерой (перетаскивание ПКМ + зум колёсиком / жестом)
    Item {
        id: viewport
        anchors.fill: parent
        clip: true

        Item {
            id: cameraContainer
            width: gameWorld.mapWidth > 0 ? gameWorld.mapWidth : 1600
            height: gameWorld.mapHeight > 0 ? gameWorld.mapHeight : 800
            x: (viewport.width - width * scale) / 2
            y: (viewport.height - height * scale) / 2
            transformOrigin: Item.TopLeft
            scale: 1.0

            Renderer {
                anchors.fill: parent
                world: gameWorld
            }

            WorldOverlays {
                anchors.fill: parent
                world: gameWorld
                cameraScale: cameraContainer.scale
            }

            SelectionOverlays {
                anchors.fill: parent
                world: gameWorld
                cameraScale: cameraContainer.scale
            }

            // Превью протяжки линии для инструментов с inputMode === 1 (стена, блокпост и т.д.)
            Rectangle {
                visible: toolBar.currentToolMeta !== null && toolBar.currentToolMeta.inputMode === 1 && mapMouseArea.containsMouse && !mapMouseArea.isDraggingLine
                width: 24; height: 24; radius: 12
                x: mapMouseArea.hoverX - 12
                y: mapMouseArea.hoverY - 12
                color: "#22fc8181"
                border.color: "#fc8181"
                border.width: 1.5
                z: 14
            }

            Rectangle {
                id: lineDragPreview
                visible: toolBar.currentToolMeta !== null && toolBar.currentToolMeta.inputMode === 1 && mapMouseArea.isDraggingLine
                property var info: ({ valid: false, cx: 0, cy: 0, length: 10, angleDeg: 0, cost: 25, minHp: 0, maxHp: 100, buildTime: 1.0 })

                width: info.length || 10
                height: 9
                radius: 2.2
                x: (info.cx || 0) - width * 0.5
                y: (info.cy || 0) - height * 0.5
                rotation: info.angleDeg || 0
                color: info.valid ? "#55e53e3e" : "#44718096"
                border.color: info.valid ? "#fc8181" : "#a0aec0"
                border.width: 1.5
                z: 14
            }

            Text {
                visible: lineDragPreview.visible
                text: {
                    var inf = lineDragPreview.info
                    var cnt = inf.sectionCount || 0
                    if (cnt === 0)
                        return "Нет места для секций стены"
                    var hpStr = (cnt > 1 && inf.minHp !== inf.maxHp)
                                ? (inf.minHp + "-" + inf.maxHp + " HP")
                                : (inf.maxHp + " HP")
                    var secWord = (cnt === 1) ? "1 секция" : (cnt + " секц.")
                    return (inf.cost || 0) + " ₽   •   " + secWord + ", " + hpStr
                           + "   •   " + Number(inf.buildTime || 1.0).toFixed(1) + "с"
                }
                color: lineDragPreview.info.valid ? "#fc8181" : "#cbd5e0"
                font.pixelSize: 10
                font.bold: true
                style: Text.Outline
                styleColor: "#0f141d"
                transformOrigin: Item.Bottom
                scale: Math.min(2.0, Math.max(0.6, 0.9 / cameraContainer.scale))
                x: (lineDragPreview.info.cx || 0) - width * 0.5
                y: (lineDragPreview.info.cy || 0) - 22 - height
                z: 15
            }

            // Прицел радиуса при выборе инструмента с привязкой к узлу (эвакуация)
            Rectangle {
                id: snapPreviewCircle
                visible: toolBar.currentToolMeta !== null && toolBar.currentToolMeta.inputMode === 2 && mapMouseArea.containsMouse
                width: 500; height: 500; radius: 250
                color: mapMouseArea.snappedPoint ? "#1848bb78" : "#10fc8181"
                border.color: mapMouseArea.snappedPoint ? "#68d391" : "#fc8181"
                border.width: 1.5
                z: 6
            }

            // Универсальный обработчик кликов и протяжки по карте
            MouseArea {
                id: mapMouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                property bool snappedPoint: false
                property bool isDraggingLine: false
                property real dragStartX: 0
                property real dragStartY: 0
                property real hoverX: 0
                property real hoverY: 0

                onPressed: (mouse) => {
                    if (toolBar.currentToolMeta && toolBar.currentToolMeta.inputMode === 1) {
                        isDraggingLine = true
                        dragStartX = mouse.x
                        dragStartY = mouse.y
                        lineDragPreview.info = gameWorld.previewTool(gameWorld.selectedTool, dragStartX, dragStartY, mouse.x, mouse.y)
                    }
                }

                onPositionChanged: (mouse) => {
                    hoverX = mouse.x
                    hoverY = mouse.y
                    if (!toolBar.currentToolMeta) return

                    if (toolBar.currentToolMeta.inputMode === 2) {
                        let snap = gameWorld.previewTool(gameWorld.selectedTool, mouse.x, mouse.y, mouse.x, mouse.y)
                        snappedPoint = snap.snapped === true
                        snapPreviewCircle.x = (snap.x || mouse.x) - 250
                        snapPreviewCircle.y = (snap.y || mouse.y) - 250
                    } else if (toolBar.currentToolMeta.inputMode === 1 && isDraggingLine) {
                        lineDragPreview.info = gameWorld.previewTool(gameWorld.selectedTool, dragStartX, dragStartY, mouse.x, mouse.y)
                    }
                }

                onReleased: (mouse) => {
                    if (isDraggingLine) {
                        gameWorld.useToolSegment(gameWorld.selectedTool, dragStartX, dragStartY, mouse.x, mouse.y)
                        isDraggingLine = false
                    }
                }

                onCanceled: isDraggingLine = false

                onClicked: (mouse) => {
                    if (toolBar.currentToolMeta && toolBar.currentToolMeta.inputMode === 1)
                        return
                    if (gameWorld.selectedTool === 0) {
                        gameWorld.selectAt(mouse.x, mouse.y)
                    } else {
                        gameWorld.selectWallAt(-1000, -1000)
                        gameWorld.useToolAt(mouse.x, mouse.y)
                    }
                }
            }
        }
    }

    NightOverlay {
        anchors.fill: parent
        world: gameWorld
        camX: cameraContainer.x
        camY: cameraContainer.y
        camScale: cameraContainer.scale
    }

    DragHandler {
        target: cameraContainer
        acceptedButtons: Qt.RightButton
    }

    WheelHandler {
        onWheel: (event) => {
            let oldScale = cameraContainer.scale
            let factor = event.angleDelta.y > 0 ? 1.15 : 0.85
            let newScale = Math.max(0.25, Math.min(5.0, oldScale * factor))
            if (newScale === oldScale) return

            let centerX = viewport.width * 0.5
            let centerY = viewport.height * 0.5
            let worldCenterX = (centerX - cameraContainer.x) / oldScale
            let worldCenterY = (centerY - cameraContainer.y) / oldScale

            cameraContainer.scale = newScale
            cameraContainer.x = centerX - worldCenterX * newScale
            cameraContainer.y = centerY - worldCenterY * newScale
        }
    }

    PinchHandler {
        target: null
        minimumScale: 0.10
        maximumScale: 5.0
        property real startScale: 1.0

        onActiveChanged: { if (active) startScale = cameraContainer.scale }
        onScaleChanged: (delta) => {
            let oldScale = cameraContainer.scale
            let newScale = Math.max(0.25, Math.min(5.0, startScale * activeScale))
            if (newScale === oldScale) return

            let pivotX = centroid.position.x
            let pivotY = centroid.position.y
            let worldPivotX = (pivotX - cameraContainer.x) / oldScale
            let worldPivotY = (pivotY - cameraContainer.y) / oldScale

            cameraContainer.scale = newScale
            cameraContainer.x = pivotX - worldPivotX * newScale
            cameraContainer.y = pivotY - worldPivotY * newScale
        }
    }

    TopHud {
        id: topHud
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        z: 20
        world: gameWorld
        onRestartRequested: {
            toolkitMenu.visible = true
            alertsPanel.resetAlerts()
            gameWorld.regenerateMap()

        }
    }

    LegendPanel {
        anchors.left: parent.left
        anchors.top: topHud.bottom
        anchors.topMargin: 8
        anchors.leftMargin: 10
        z: 18
    }

    AlertsPanel {
        id: alertsPanel
        anchors.right: parent.right
        anchors.top: topHud.bottom
        anchors.topMargin: 8
        anchors.rightMargin: 10
        z: 18
        world: gameWorld
    }

    ToolBar {
        id: toolBar
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 14
        z: 20
        world: gameWorld
    }

    ToolkitMenu {
        id: toolkitMenu
        world: gameWorld
        visible: true
    }
}

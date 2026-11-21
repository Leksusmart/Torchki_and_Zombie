import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import TorchkiAndZombie

Window {
    id: rootWindow
    width: 1280
    height: 720
    visible: true
    title: "Torchki and Zombie"
    // Фон вокруг карты автоматически берет цвет воды из крайнего пикселя PNG
    color: gameWorld.waterColor

    // Игровая логика на C++
    GameWorld {
        id: gameWorld
    }

    // Игровая область с камерой (перетаскивание правой кнопкой + зум колесиком)
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
            // Расширяющееся круглое кольцо при применении любого инструмента
            Rectangle {
                id: toolPulseRing
                width: 90
                height: 90
                radius: width * 0.5
                color: "transparent"
                border.color: "#4299e1"
                border.width: 2.5
                visible: false
                z: 7

                Connections {
                    target: gameWorld
                    function onPulseWaveTriggered(wx, wy, rad, clr) {
                        toolPulseRing.width = rad * 2
                        toolPulseRing.height = rad * 2
                        toolPulseRing.radius = rad
                        toolPulseRing.x = wx - rad
                        toolPulseRing.y = wy - rad
                        toolPulseRing.border.color = clr
                        toolPulseRing.visible = true
                        toolRingAnim.restart()
                    }
                }

                ParallelAnimation {
                    id: toolRingAnim
                    NumberAnimation { target: toolPulseRing; property: "scale"; from: 0.1; to: 1.05; duration: 450; easing.type: Easing.OutQuad }
                    NumberAnimation { target: toolPulseRing; property: "opacity"; from: 0.95; to: 0.0; duration: 450 }
                    onFinished: toolPulseRing.visible = false
                }
            }

            // ореол пламени вокруг горящих зданий
            Repeater {
                model: gameWorld.burningLights
                delegate: Item {
                    x: modelData.cx - modelData.radius
                    y: modelData.cy - modelData.radius
                    width: modelData.radius * 2
                    height: modelData.radius * 2
                    enabled: true

                    // 1. Внешний широкий ореол (мягкий красно-оранжевый свет)
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "#1aff4500"
                    }

                    // 2. Средний слой (насыщенный огонь)
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.65
                        height: width
                        radius: width / 2
                        color: "#2eff6a00"
                    }

                    // 3. Внутренний слой (яркий жар)
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width * 0.35
                        height: width
                        radius: width / 2
                        color: "#4dffae00"
                    }
                }
            }

            Text {
                visible: true
                text: parent.scale
            }

            // Сумма дохода в центре производственных зданий (офисов и заводов)
            Repeater {
                model: gameWorld.workplaceLabels
                delegate: Text {
                    x: modelData.cx - width * 0.5
                    y: modelData.cy - height * 0.5
                    visible: modelData.incomeText != "+0.00₽"
                    text: modelData.incomeText
                    color: modelData.active ? "#fefcbf" : "#718096"
                    font.pixelSize: 5
                    font.bold: true
                    style: Text.Outline
                    styleColor: "#1a202c"
                }
            }



            // Пульсирующий маркер места последней вспышки заражения
            Rectangle {
                id: outbreakPing
                width: 60; height: 60
                radius: 30
                color: "transparent"
                border.color: "#ff3333"
                border.width: 3
                visible: false

                Connections {
                    target: gameWorld
                    function onOutbreakOccurred(x, y, index) {
                        outbreakPing.x = x - outbreakPing.width / 2
                        outbreakPing.y = y - outbreakPing.height / 2
                        outbreakPing.visible = true
                        pingAnim.restart()
                    }
                }

                SequentialAnimation {
                    id: pingAnim
                    NumberAnimation { target: outbreakPing; property: "scale"; from: 0.2; to: 2.5; duration: 1200 }
                    ScriptAction { script: outbreakPing.visible = false }
                }
            }
            // Эвакуационные выходы (6 точек из LevelDefinition)
            Repeater {
                model: gameWorld.evacPoints
                delegate: Item {
                    x: modelData.x
                    y: modelData.y
                    z: 5

                    // 1. Зеленый круг со стрелкой (масштабируется вместе с картой)
                    Rectangle {
                        id: evacCircle
                        width: 24
                        height: 24
                        radius: 12
                        x: -width * 0.5
                        y: -height * 0.5
                        color: "#3348bb78"
                        border.color: "#48bb78"
                        border.width: 1.5

                        // Детальная пиксельная стрелка (шаг 1 px, в 2 раза плотнее)
                        Item {
                            anchors.centerIn: parent
                            width: 14
                            height: 15
                            smooth: false
                            antialiasing: false

                            // 1. Тёмная пиксельная обводка (толщина 1 px)
                            Rectangle { x: 6; y: 0; width: 2;  height: 1;  color: "#0f141d" }
                            Rectangle { x: 5; y: 1; width: 4;  height: 1;  color: "#0f141d" }
                            Rectangle { x: 4; y: 2; width: 6;  height: 1;  color: "#0f141d" }
                            Rectangle { x: 3; y: 3; width: 8;  height: 1;  color: "#0f141d" }
                            Rectangle { x: 2; y: 4; width: 10; height: 1;  color: "#0f141d" }
                            Rectangle { x: 1; y: 5; width: 12; height: 1;  color: "#0f141d" }
                            Rectangle { x: 0; y: 6; width: 14; height: 3;  color: "#0f141d" }
                            Rectangle { x: 4; y: 9; width: 6;  height: 6;  color: "#0f141d" }

                            // 2. Основное зелёное тело стрелки (ступеньки по 1 px)
                            Rectangle { x: 6; y: 1; width: 2;  height: 1;  color: "#68d391" }
                            Rectangle { x: 5; y: 2; width: 4;  height: 1;  color: "#68d391" }
                            Rectangle { x: 4; y: 3; width: 6;  height: 1;  color: "#68d391" }
                            Rectangle { x: 3; y: 4; width: 8;  height: 1;  color: "#68d391" }
                            Rectangle { x: 2; y: 5; width: 10; height: 1;  color: "#68d391" }
                            Rectangle { x: 1; y: 6; width: 12; height: 2;  color: "#68d391" }
                            Rectangle { x: 5; y: 8; width: 4;  height: 6;  color: "#68d391" }

                            // 3. Лёгкий светлый блик по левой грани для объёма (как на референсе)
                            Rectangle { x: 5; y: 8; width: 1;  height: 5;  color: "#9ae6b4" }
                        }

                    }


                    // 2. Адаптивная подпись названия выхода над кругом
                    Text {
                        text: modelData.name
                        color: "#9ae6b4"
                        font.pixelSize: 11
                        font.bold: true
                        style: Text.Outline
                        styleColor: "#0f141d"

                        // Компенсация масштаба: при отдалении карты надпись не превращается в кашу
                        transformOrigin: Item.Bottom
                        scale: Math.min(2.2, Math.max(0.55, 0.85 / cameraContainer.scale))

                        x: -width * 0.5
                        y: -evacCircle.height * 0.5 - height - 2
                    }
                }
            }

            // Информационная карточка (overlay) над выбранным зданием
            Rectangle {
                id: buildingInfoOverlay
                visible: gameWorld.selectedBuildingInfo.valid === true
                width: infoCol.implicitWidth + 14
                height: infoCol.implicitHeight + 10
                color: "#ee141923"
                border.color: "#63b3ed"
                border.width: 1
                radius: 4
                z: 9

                // Компенсация масштаба камеры, чтобы текст читался при любом зуме
                transformOrigin: Item.Bottom
                scale: Math.min(2.0, Math.max(0.55, 0.9 / cameraContainer.scale))

                x: (gameWorld.selectedBuildingInfo.cx || 0) - width * 0.5
                y: (gameWorld.selectedBuildingInfo.topY || 0) - height - 10

                Column {
                    id: infoCol
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        text: (gameWorld.selectedBuildingInfo.type || "") + " #"+gameWorld.selectedBuildingInfo.id
                        color: "#63b3ed"
                        font.pixelSize: 10
                        font.bold: true
                    }
                    Text {
                        text: "Люди: " + (gameWorld.selectedBuildingInfo.occupants || 0)
                              + " / " + (gameWorld.selectedBuildingInfo.capacity || 0)
                        color: "#e2e8f0"
                        font.pixelSize: 9
                    }
                    Text {
                        text: "Здоровье: " + (gameWorld.selectedBuildingInfo.health || 0)
                              + " / " + (gameWorld.selectedBuildingInfo.maxHealth || 0)
                        color: "#68d391"
                        font.pixelSize: 9
                    }
                }
            }

            // Информационная карточка (overlay) над выбранным жителем / солдатом / зомби
            Rectangle {
                id: unitInfoOverlay
                visible: gameWorld.selectedUnitInfo.valid === true
                width: unitInfoCol.implicitWidth + 14
                height: unitInfoCol.implicitHeight + 10
                color: "#ee141923"
                border.color: "#f6ad55"
                border.width: 1
                radius: 4
                z: 11

                transformOrigin: Item.Bottom
                scale: Math.min(2.0, Math.max(0.55, 0.9 / cameraContainer.scale))

                x: (gameWorld.selectedUnitInfo.x || 0) - width * 0.5
                y: (gameWorld.selectedUnitInfo.y || 0) - height - 10

                Column {
                    id: unitInfoCol
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        text: (gameWorld.selectedUnitInfo.role || "") + " #" + (gameWorld.selectedUnitInfo.id || 0)
                              + "  (HP: " + (gameWorld.selectedUnitInfo.hp || 0) + ")"
                        color: "#f6ad55"
                        font.pixelSize: 10
                        font.bold: true
                    }
                    Text {
                        text: "Статус: " + (gameWorld.selectedUnitInfo.state || "")
                        color: "#e2e8f0"
                        font.pixelSize: 9
                    }
                    Text {
                        text: "Цель: " + (gameWorld.selectedUnitInfo.target || "Нет")
                        color: "#68d391"
                        font.pixelSize: 9
                    }
                    Text {
                        text: "Локация: " + (gameWorld.selectedUnitInfo.inside || "Улица")
                        color: "#a0aec0"
                        font.pixelSize: 8
                    }
                }
            }


            // Прицел радиуса 250м с автопривязкой (20м) при выборе инструмента Эвакуации
            Rectangle {
                id: evacPreviewCircle
                visible: gameWorld.selectedTool === 3 && mapMouseArea.containsMouse
                width: 500  // Радиус 250м -> диаметр 500м
                height: 500
                radius: 250
                color: mapMouseArea.snappedEvac ? "#1848bb78" : "#10fc8181"
                border.color: mapMouseArea.snappedEvac ? "#68d391" : "#fc8181"
                border.width: 1.5
                z: 6
            }

            // Активные зоны эвакуации с таймером наверху круга (как на 2-м скриншоте)
            Repeater {
                model: gameWorld.activeEvacuations
                delegate: Item {
                    x: modelData.x
                    y: modelData.y
                    z: 6

                    Rectangle {
                        width: 500
                        height: 500
                        radius: 250
                        x: -250
                        y: -250
                        color: "#1448bb78"
                        border.color: "#48bb78"
                        border.width: 1.5
                    }

                    Text {
                        text: "Эвакуация  " + modelData.timeLeft + "сек  (" + modelData.boarded + "/50)"
                        color: "#9ae6b4"
                        font.pixelSize: 10
                        font.bold: true
                        style: Text.Outline
                        styleColor: "#0f141d"
                        transformOrigin: Item.Bottom
                        scale: Math.min(2.0, Math.max(0.6, 0.85 / cameraContainer.scale))
                        x: -width * 0.5
                        y: -250 - height - 4
                    }
                }
            }

            // Зоны посадки десанта (синий круг с таймером, как на 1-3 скриншотах)
            Repeater {
                model: gameWorld.dropZones
                delegate: Item {
                    x: modelData.x
                    y: modelData.y
                    z: 8

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        x: -17
                        y: -17
                        color: "#2263b3ed"
                        border.color: "#63b3ed"
                        border.width: 2
                    }

                    Text {
                        text: "Активная высадка " + modelData.eta + "с"
                        color: "#90cdf4"
                        font.pixelSize: 10
                        font.bold: true
                        style: Text.Outline
                        styleColor: "#0f141d"
                        transformOrigin: Item.Bottom
                        scale: Math.min(2.0, Math.max(0.6, 0.85 / cameraContainer.scale))
                        x: -width * 0.5
                        y: -22 - height
                    }
                }
            }

            // Десантные вертолёты с тенью и вращающимися лопастями (как на 2-5 скриншотах)
            Repeater {
                model: gameWorld.helicopters
                delegate: Item {
                    x: modelData.x
                    y: modelData.y
                    z: 12

                    // Тень вертолёта со смещением
                    Item {
                        x: 14
                        y: 16
                        rotation: modelData.angleDeg
                        opacity: 0.32

                        Rectangle {
                            width: 26; height: 11; radius: 5
                            x: -11; y: -5.5
                            color: "#000000"
                        }
                        Rectangle {
                            width: 14; height: 3
                            x: -23; y: -1.5
                            color: "#000000"
                        }
                        // Тень лопастей
                        Item {
                            x: 2; y: 0
                            rotation: modelData.rotorDeg
                            Rectangle { width: 42; height: 2.5; x: -21; y: -1.25; color: "#000000" }
                            Rectangle { width: 2.5; height: 42; x: -1.25; y: -21; color: "#000000" }
                        }
                    }

                    // Корпус вертолёта
                    Item {
                        rotation: modelData.angleDeg

                        // Хвостовая балка и стабилизатор
                        Rectangle {
                            width: 14; height: 3.5
                            x: -23; y: -1.75
                            color: "#5f6f52"
                            border.color: "#2d3748"
                            border.width: 0.5
                        }
                        Rectangle {
                            width: 3; height: 10
                            x: -24; y: -5
                            color: "#5f6f52"
                        }

                        // Основной фюзеляж
                        Rectangle {
                            width: 26; height: 11; radius: 5.5
                            x: -11; y: -5.5
                            color: "#78866b"
                            border.color: "#2d3748"
                            border.width: 1
                        }

                        // Кабина пилота (стекло спереди)
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            x: 5; y: -4
                            color: "#90cdf4"
                        }

                        // Полупрозрачный диск от вращения винта + 4 лопасти
                        Rectangle {
                            width: 44; height: 44; radius: 22
                            x: 2 - 22; y: -22
                            color: "#18ffffff"
                            border.color: "#33ffffff"
                            border.width: 0.8

                            Item {
                                anchors.centerIn: parent
                                rotation: modelData.rotorDeg

                                Rectangle { width: 42; height: 2.5; x: -21; y: -1.25; color: "#2d3748" }
                                Rectangle { width: 2.5; height: 42; x: -1.25; y: -21; color: "#2d3748" }
                                Rectangle { width: 4; height: 4; radius: 2; x: -2; y: -2; color: "#1a202c" }
                            }
                        }
                    }
                }
            }


            MouseArea {
                id: mapMouseArea
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                property bool snappedEvac: false

                onPositionChanged: (mouse) => {
                                       if (gameWorld.selectedTool === 3) {
                                           let snap = gameWorld.snapEvacPoint(mouse.x, mouse.y, 20.0)
                                           snappedEvac = snap.snapped
                                           evacPreviewCircle.x = snap.x - 250
                                           evacPreviewCircle.y = snap.y - 250
                                       }
                                   }

                onClicked: (mouse) => {
                               if (gameWorld.selectedTool === 0) {
                                   let hitUnit = gameWorld.selectUnitAt(mouse.x, mouse.y, 7.0)
                                   if (hitUnit) {
                                       gameWorld.selectBuildingAt(-1000, -1000) // Скрываем оверлей здания
                                   } else {
                                       gameWorld.selectBuildingAt(mouse.x, mouse.y)
                                   }
                               } else {
                                   gameWorld.useToolAt(mouse.x, mouse.y)
                               }
                           }


            }

        }

        // Слой ночного затемнения с вырезанием круглых зон освещения (70м) вокруг пожаров
        Canvas {
            id: nightOverlay
            anchors.fill: parent
            enabled: false
            visible: gameWorld.nightDarkness > 0.001

            property real darkness: gameWorld.nightDarkness
            property var lights: gameWorld.burningLights
            property real camX: cameraContainer.x
            property real camY: cameraContainer.y
            property real camScale: cameraContainer.scale

            onDarknessChanged: requestPaint()
            onLightsChanged: requestPaint()
            onCamXChanged: requestPaint()
            onCamYChanged: requestPaint()
            onCamScaleChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                if (darkness <= 0.001) return

                // Заливаем весь экран ночной темнотой
                ctx.globalCompositeOperation = "source-over"
                ctx.fillStyle = Qt.rgba(0.024, 0.043, 0.098, darkness)
                ctx.fillRect(0, 0, width, height)

                // Вырезаем круглые зоны света (70м) вокруг горящих зданий
                if (lights && lights.length > 0) {
                    ctx.globalCompositeOperation = "destination-out"
                    for (var i = 0; i < lights.length; ++i) {
                        var sx = camX + lights[i].cx * camScale
                        var sy = camY + lights[i].cy * camScale
                        var sr = lights[i].radius * camScale

                        var grad = ctx.createRadialGradient(sx, sy, sr * 0.25, sx, sy, sr)
                        grad.addColorStop(0.0, "rgba(0, 0, 0, 1.0)") // Внутри светло как днём
                        grad.addColorStop(0.7, "rgba(0, 0, 0, 0.85)")
                        grad.addColorStop(1.0, "rgba(0, 0, 0, 0.0)") // Плавный переход в ночь

                        ctx.fillStyle = grad
                        ctx.beginPath()
                        ctx.arc(sx, sy, sr, 0, Math.PI * 2)
                        ctx.fill()
                    }
                }
            }
        }


        // Перетаскивание карты правой кнопкой мыши
        DragHandler {
            target: cameraContainer
            acceptedButtons: Qt.RightButton
        }

        // Масштабирование колесиком мыши относительно центра экрана
        WheelHandler {
            onWheel: (event) => {
                         let oldScale = cameraContainer.scale
                         let factor = event.angleDelta.y > 0 ? 1.15 : 0.85
                         let newScale = Math.max(0.25, Math.min(5.0, oldScale * factor))
                         if (newScale === oldScale) return

                         // Координаты центра экрана
                         let centerX = viewport.width * 0.5
                         let centerY = viewport.height * 0.5

                         // Вычисляем мировую точку карты, которая сейчас находится ровно в центре экрана
                         let worldCenterX = (centerX - cameraContainer.x) / oldScale
                         let worldCenterY = (centerY - cameraContainer.y) / oldScale

                         // Применяем новый масштаб и возвращаем эту мировую точку в центр экрана
                         cameraContainer.scale = newScale
                         cameraContainer.x = centerX - worldCenterX * newScale
                         cameraContainer.y = centerY - worldCenterY * newScale
                     }
        }

        // Масштабирование двумя пальцами (pinch-to-zoom) на сенсорном экране
        PinchHandler {
            id: pinchHandler
            target: null // Управляем масштабом и позицией вручную, чтобы сохранить центрирование как у колёсика мыши
            minimumScale: 0.10
            maximumScale: 5.0

            property real startScale: 1.0

            onActiveChanged: {
                if (active) {
                    startScale = cameraContainer.scale
                }
            }

            onScaleChanged: (delta) => {
                                let oldScale = cameraContainer.scale
                                let newScale = Math.max(0.25, Math.min(5.0, startScale * activeScale))
                                if (newScale === oldScale) return

                                // Точка между двумя пальцами в координатах viewport
                                let pivotX = centroid.position.x
                                let pivotY = centroid.position.y

                                // Переводим эту точку в мировые координаты карты до изменения масштаба
                                let worldPivotX = (pivotX - cameraContainer.x) / oldScale
                                let worldPivotY = (pivotY - cameraContainer.y) / oldScale

                                // Применяем новый масштаб и удерживаем точку под пальцами на месте
                                cameraContainer.scale = newScale
                                cameraContainer.x = pivotX - worldPivotX * newScale
                                cameraContainer.y = pivotY - worldPivotY * newScale
                            }
        }

    }

    // Верхняя панель статистики (HUD)
    Rectangle {
        id: topHud
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 58
        color: "#ee0d1117"
        border.color: "#262f3f"
        border.width: 1
        z: 20

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8

            // 1. Левый блок: Название уровня + время / день / сводка
            Column {
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 8
                spacing: 2

                Row {
                    spacing: 6
                    Text {
                        text: "МАНХЭТТЕН"
                        color: "#f7fafc"
                        font.pixelSize: 13
                        font.bold: true
                        font.letterSpacing: 0.8
                    }
                    Text {
                        text: "•"
                        color: "#4a5568"
                        font.pixelSize: 13
                    }
                    Text {
                        text: gameWorld.isNight ? "НОЧЬ" : "ДЕНЬ"
                        color: gameWorld.isNight ? "#63b3ed" : "#ecc94b"
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                Text {
                    text: {
                        var status = "День " + gameWorld.dayNumber + "  •  " + gameWorld.formattedTime
                        if (gameWorld.zombiesCount > 0)
                            status += "  •  Активных зомби: " + gameWorld.zombiesCount
                        if (gameWorld.infectedCount > 0)
                            status += "  •  Инфицированных: " + gameWorld.infectedCount
                        if (gameWorld.zombiesCount === 0 && gameWorld.infectedCount === 0)
                            status += "  •  Угроз не обнаружено (▀̿Ĺ̯▀̿ ̿)"
                        return status
                    }
                    color: gameWorld.zombiesCount > 0 ? "#fc8181"
                         : (gameWorld.infectedCount > 0 ? "#f6ad55" : "#9ae6b4")
                    font.pixelSize: 10
                    font.bold: true
                }

            }

            // 2. Центральные карточки статистики
            Repeater {
                model: [
                    { title: "ЖИТЕЛИ", val: gameWorld.citizensCount, clr: "#e2e8f0" },
                    { title: "ЗОМБИ", val: gameWorld.zombiesCount, clr: "#48bb78" },
                    { title: "УБИТО", val: gameWorld.eliminatedCount, clr: "#ecc94b" },
                    { title: "ЭВАКУИРОВАНО", val: gameWorld.evacuatedTotalCount, clr: "#4fd1c5" }
                ]

                delegate: Rectangle {
                    Layout.preferredWidth: 104
                    Layout.preferredHeight: 42
                    color: "#141922"
                    border.color: "#283244"
                    border.width: 1
                    radius: 2

                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Row {
                            spacing: 5
                            Rectangle {
                                width: 6; height: 6
                                color: modelData.clr
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: modelData.title
                                color: "#8b9bb4"
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }

                        Text {
                            text: modelData.val
                            color: "#f7fafc"
                            font.pixelSize: 14
                            font.bold: true
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // 3. Кнопка "ЗАНОВО"
            Rectangle {
                Layout.preferredWidth: 86
                Layout.preferredHeight: 36
                color: restartArea.containsMouse ? "#252e3f" : "#161b24"
                border.color: "#3b475e"
                border.width: 1
                radius: 2

                Text {
                    anchors.centerIn: parent
                    text: "ЗАНОВО"
                    color: "#e2e8f0"
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: restartArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        alertsModel.clear()
                        alertsPanel.unreadCount = 0
                        alertsPanel.lastIsNight = gameWorld.isNight
                        gameWorld.regenerateMap()
                        toolkitMenu.visible = true
                    }

                }
            }

            // 4. Кнопка "Улучшения"
            Rectangle {
                Layout.preferredWidth: 108
                Layout.preferredHeight: 36
                color: shopArea.containsMouse ? "#22543d" : "#17382b"
                border.color: "#48bb78"
                border.width: 1
                radius: 2

                Text {
                    anchors.centerIn: parent
                    text: "УЛУЧШЕНИЯ"
                    color: "#9ae6b4"
                    font.pixelSize: 11
                    font.bold: true
                }

                MouseArea {
                    id: shopArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!gameWorld.paused) {
                            gameWorld.togglePause()
                        }
                        //toolkitMenu.visible = true TODO сделать магазин с улучшениями
                    }
                }
            }

            // 5. Блок Бюджета и дохода
            Column {
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 6
                Layout.rightMargin: 4
                spacing: 0

                Text {
                    text: "БЮДЖЕТ"
                    color: "#8b9bb4"
                    font.pixelSize: 9
                    font.bold: true
                    anchors.right: parent.right
                }
                Text {
                    text: gameWorld.budget.toLocaleString(Qt.locale("ru_RU"), "f", 0) + " ₽"
                    color: "#68d391"
                    font.pixelSize: 15
                    font.bold: true
                    anchors.right: parent.right
                }
                Text {
                    text: "+" + gameWorld.incomePerSec.toFixed(0) + " ₽ / сек"
                    color: "#68d391"
                    font.pixelSize: 9
                    anchors.right: parent.right
                }
            }

            // 6. Компактный блок управления временем (Пауза | 1x | 2x)
            Row {
                spacing: 2
                Layout.alignment: Qt.AlignVCenter

                // Пауза
                Rectangle {
                    width: 30; height: 32
                    color: gameWorld.paused ? "#2d3748" : "#141922"
                    border.color: gameWorld.paused ? "#ecc94b" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: "🛑"
                        color: gameWorld.paused ? "#ecc94b" : "#a0aec0"
                        font.pixelSize: 12
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (!gameWorld.paused) gameWorld.togglePause()
                        }
                    }
                }

                // Скорость 1x
                Rectangle {
                    width: 30; height: 32
                    color: (!gameWorld.paused && gameWorld.speedMultiplier === 1) ? "#2b3648" : "#141922"
                    border.color: (!gameWorld.paused && gameWorld.speedMultiplier === 1) ? "#63b3ed" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: "Ⅰ"
                        color: (!gameWorld.paused && gameWorld.speedMultiplier === 1) ? "#ffffff" : "#a0aec0"
                        font.pixelSize: 11
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (gameWorld.paused) gameWorld.togglePause()
                            if (gameWorld.speedMultiplier !== 1) gameWorld.toggleSpeedTo1()
                        }
                    }
                }

                // Скорость 2x
                Rectangle {
                    width: 30; height: 32
                    color: (!gameWorld.paused && gameWorld.speedMultiplier === 2) ? "#2b3648" : "#141922"
                    border.color: (!gameWorld.paused && gameWorld.speedMultiplier === 2) ? "#63b3ed" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: "Ⅱ"
                        color: (!gameWorld.paused && gameWorld.speedMultiplier === 2) ? "#ffffff" : "#a0aec0"
                        font.pixelSize: 11
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (gameWorld.paused) gameWorld.togglePause()
                            if (gameWorld.speedMultiplier !== 2) gameWorld.toggleSpeedTo2()
                        }
                    }
                }
                // Скорость 3x
                Rectangle {
                    width: 30; height: 32
                    color: (!gameWorld.paused && gameWorld.speedMultiplier === 3) ? "#2b3648" : "#141922"
                    border.color: (!gameWorld.paused && gameWorld.speedMultiplier === 3) ? "#63b3ed" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: "Ⅲ"
                        color: (!gameWorld.paused && gameWorld.speedMultiplier === 3) ? "#ffffff" : "#a0aec0"
                        font.pixelSize: 11
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (gameWorld.paused) gameWorld.togglePause()
                            if (gameWorld.speedMultiplier !== 3) gameWorld.toggleSpeedTo3()
                        }
                    }
                }
                // Скорость 4x
                Rectangle {
                    width: 30; height: 32
                    color: (!gameWorld.paused && gameWorld.speedMultiplier === 4) ? "#2b3648" : "#141922"
                    border.color: (!gameWorld.paused && gameWorld.speedMultiplier === 4) ? "#63b3ed" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: "Ⅳ"
                        color: (!gameWorld.paused && gameWorld.speedMultiplier === 4) ? "#ffffff" : "#a0aec0"
                        font.pixelSize: 11
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (gameWorld.paused) gameWorld.togglePause()
                            if (gameWorld.speedMultiplier !== 4) gameWorld.toggleSpeedTo4()
                        }
                    }
                }
            }
        }
    }

    // Левая панель легенды юнитов
    Rectangle {
        id: legendPanel
        anchors.left: parent.left
        anchors.top: topHud.bottom
        anchors.topMargin: 8
        anchors.leftMargin: 10
        z: 18
        property bool expanded: true
        width: expanded ? 168 : 102
        height: expanded ? (legendCol.implicitHeight + 16) : 28
        color: "#e610151d"
        border.color: "#2d3748"
        border.width: 1
        radius: 2
        Column {
            id: legendCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 7
            // Заголовок-кнопка сворачивания
            Item {
                width: parent.width
                height: 14
                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: "ЛЕГЕНДА"
                    color: "#cbd5e0"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 0.6
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: legendPanel.expanded ? "−" : "+"
                    color: "#90cdf4"
                    font.pixelSize: 13
                    font.bold: true
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: legendPanel.expanded = !legendPanel.expanded
                }
            }
            Rectangle {
                visible: legendPanel.expanded
                width: parent.width
                height: 1
                color: "#262f3f"
            }
            Repeater {
                model: legendPanel.expanded ? [
                                                  { label: "Житель", clr: "#f0f0f0" },
                                                  { label: "В панике", clr: "#ff7373" },
                                                  { label: "Инфицирован", clr: "#ffaf2d" },
                                                  { label: "Вакцинированный", clr: "#50b4ff" },
                                                  { label: "Зомби", clr: "#46e678" },
                                                  { label: "Солдаты", clr: "#4191ff" }
                                              ] : []
                delegate: Row {
                    spacing: 8
                    Rectangle {
                        width: 9; height: 9
                        color: modelData.clr
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: modelData.label
                        color: "#cbd5e0"
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
    Rectangle {
        id: alertsPanel
        anchors.right: parent.right
        anchors.top: topHud.bottom
        anchors.topMargin: 8
        anchors.rightMargin: 10
        z: 18
        property bool expanded: true
        property int unreadCount: 0
        property bool lastIsNight: gameWorld.isNight

        Connections {
            target: gameWorld
            function onOutbreakOccurred(x, y, index) {
                alertsModel.insert(0, {
                    text: "ВСПЫШКА ЗАРАЖЕНИЯ #" + index + ": зафиксирован нулевой пациент в секторе ("
                          + Math.round(x) + ", " + Math.round(y) + ").",
                    accent: "#fc8181"
                })
                if (alertsModel.count > 6) alertsModel.remove(6)
                if (!alertsPanel.expanded) alertsPanel.unreadCount++
            }
            function onIsNightChanged() {
                if (alertsPanel.lastIsNight === gameWorld.isNight) return
                alertsPanel.lastIsNight = gameWorld.isNight

                var msg = gameWorld.isNight
                    ? "Наступила ночь. Видимость снижена, следите за очагами пожаров."
                    : "Наступил день " + gameWorld.dayNumber + ". Жители направляются на работу."
                var clr = gameWorld.isNight ? "#63b3ed" : "#ecc94b"

                alertsModel.insert(0, { text: msg, accent: clr })
                if (alertsModel.count > 6) alertsModel.remove(6)
                if (!alertsPanel.expanded) alertsPanel.unreadCount++
            }
        }

        function clearAlerts() {
            alertsModel.clear()
            unreadCount = 0
        }
        ListModel {
            id: alertsModel
            ListElement {
                text: "Оперативный штаб развёрнут. Отслеживайте вспышки инфекции и защищайте жителей."
                accent: "#48bb78"
            }
        }

        width: expanded ? 290 : 142
        height: expanded ? (alertsCol.implicitHeight + 16) : 28
        color: "#e610151d"
        border.color: (!expanded && unreadCount > 0) ? "#fc8181" : "#2d3748"
        border.width: 1
        radius: 2
        Column {
            id: alertsCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            spacing: 6
            // Заголовок-кнопка сворачивания
            Item {
                width: parent.width
                height: 14
                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Text {
                        text: "УВЕДОМЛЕНИЯ"
                        color: "#cbd5e0"
                        font.pixelSize: 10
                        font.bold: true
                        font.letterSpacing: 0.6
                    }
                    Rectangle {
                        visible: !alertsPanel.expanded && alertsPanel.unreadCount > 0
                        width: 16; height: 14
                        radius: 2
                        color: "#e53e3e"
                        Text {
                            anchors.centerIn: parent
                            text: alertsPanel.unreadCount
                            color: "#ffffff"
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: alertsPanel.expanded ? "−" : "+"
                    color: "#90cdf4"
                    font.pixelSize: 13
                    font.bold: true
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        alertsPanel.expanded = !alertsPanel.expanded
                        if (alertsPanel.expanded) {
                            alertsPanel.unreadCount = 0
                        }
                    }
                }
            }
            Rectangle {
                visible: alertsPanel.expanded
                width: parent.width
                height: 1
                color: "#262f3f"
            }
            // Список карточек уведомлений
            Repeater {
                model: alertsPanel.expanded ? alertsModel : null
                delegate: Rectangle {
                    width: alertsCol.width
                    height: alertTxt.implicitHeight + 10
                    color: "#151c26"
                    border.color: "#283244"
                    border.width: 1
                    radius: 2
                    Rectangle {
                        width: 3
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: model.accent
                    }
                    Text {
                        id: alertTxt
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        text: model.text
                        color: "#e2e8f0"
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // Нижняя панель выбора способностей
    Row {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 14
        spacing: 12

        Repeater {
            model: [
                { id: 1, title: "Вакцинирующая капсула", desc: "Лечит в радиусе", cost: "90 ₽", accent: "#4299e1" },
                { id: 2, title: "Отряд десанта", desc: "Высадка солдат в точку", cost: "200 ₽", accent: "#48bb78" },
                { id: 3, title: "Точка эвакуации", desc: "Инициация эвакуации", cost: "75 ₽", accent: "#38b2ac" }
            ]

            delegate: Rectangle {
                visible: gameWorld.equippedTools.indexOf(modelData.id) !== -1
                width: 190
                height: 82
                color: gameWorld.selectedTool === modelData.id ? "#2b3648" : "#dd18202c"
                border.color: gameWorld.selectedTool === modelData.id ? modelData.accent : "#4a5568"
                border.width: gameWorld.selectedTool === modelData.id ? 2 : 1
                radius: 4

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    Text { text: modelData.title; color: modelData.accent; font.bold: true; font.pixelSize: 13 }
                    Text { text: modelData.desc; color: "#a0aec0"; font.pixelSize: 11; wrapMode: Text.WordWrap; width: parent.width }
                    Text { text: modelData.cost; color: "#e2e8f0"; font.bold: true; font.pixelSize: 12 }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        gameWorld.selectedTool = (gameWorld.selectedTool === modelData.id) ? 0 : modelData.id
                    }
                }
            }
        }
    }
    ToolkitMenu {
        id: toolkitMenu
        world: gameWorld
        visible: true // Открывается сразу при запуске и при рестарте
    }

}

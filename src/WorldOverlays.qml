import QtQuick 2.15

// Мировые оверлеи поверх карты: импульсы инструментов, пожары, доходы, эвакуация, вертолёты и прогресс стен
Item {
    id: root
    required property var world
    required property real cameraScale

    // 1. Расширяющееся кольцо при применении инструмента
    Rectangle {
        id: toolPulseRing
        width: 90; height: 90; radius: 45
        color: "transparent"
        border.color: "#4299e1"
        border.width: 2.5
        visible: false
        z: 7

        Connections {
            target: root.world
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

    // 2. Ореол пламени вокруг горящих зданий
    Repeater {
        model: root.world.burningLights
        delegate: Item {
            x: modelData.cx - modelData.radius
            y: modelData.cy - modelData.radius
            width: modelData.radius * 2
            height: modelData.radius * 2
            Rectangle { anchors.fill: parent; radius: width / 2; color: "#1aff4500" }
            Rectangle { anchors.centerIn: parent; width: parent.width * 0.65; height: width; radius: width / 2; color: "#2eff6a00" }
            Rectangle { anchors.centerIn: parent; width: parent.width * 0.35; height: width; radius: width / 2; color: "#4dffae00" }
        }
    }

    // 3. Подписи дохода в центре офисов и заводов
    Repeater {
        model: root.world.workplaceLabels
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

    // 4. Пульсирующий маркер последней вспышки заражения
    Rectangle {
        id: outbreakPing
        width: 60; height: 60; radius: 30
        color: "transparent"
        border.color: "#ff3333"
        border.width: 3
        visible: false

        Connections {
            target: root.world
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

    // 5. Эвакуационные выходы на карте
    Repeater {
        model: root.world.evacPoints
        delegate: Item {
            x: modelData.x
            y: modelData.y
            z: 5

            Rectangle {
                id: evacCircle
                width: 24; height: 24; radius: 12
                x: -12; y: -12
                color: "#3348bb78"
                border.color: "#48bb78"
                border.width: 1.5

                Item {
                    anchors.centerIn: parent
                    width: 14; height: 15
                    smooth: false; antialiasing: false
                    Rectangle { x: 6; y: 0; width: 2; height: 1; color: "#0f141d" }
                    Rectangle { x: 5; y: 1; width: 4; height: 1; color: "#0f141d" }
                    Rectangle { x: 4; y: 2; width: 6; height: 1; color: "#0f141d" }
                    Rectangle { x: 3; y: 3; width: 8; height: 1; color: "#0f141d" }
                    Rectangle { x: 2; y: 4; width: 10; height: 1; color: "#0f141d" }
                    Rectangle { x: 1; y: 5; width: 12; height: 1; color: "#0f141d" }
                    Rectangle { x: 0; y: 6; width: 14; height: 3; color: "#0f141d" }
                    Rectangle { x: 4; y: 9; width: 6; height: 6; color: "#0f141d" }
                    Rectangle { x: 6; y: 1; width: 2; height: 1; color: "#68d391" }
                    Rectangle { x: 5; y: 2; width: 4; height: 1; color: "#68d391" }
                    Rectangle { x: 4; y: 3; width: 6; height: 1; color: "#68d391" }
                    Rectangle { x: 3; y: 4; width: 8; height: 1; color: "#68d391" }
                    Rectangle { x: 2; y: 5; width: 10; height: 1; color: "#68d391" }
                    Rectangle { x: 1; y: 6; width: 12; height: 2; color: "#68d391" }
                    Rectangle { x: 5; y: 8; width: 4; height: 6; color: "#68d391" }
                    Rectangle { x: 5; y: 8; width: 1; height: 5; color: "#9ae6b4" }
                }
            }

            Text {
                text: modelData.name
                color: "#9ae6b4"
                font.pixelSize: 11
                font.bold: true
                style: Text.Outline
                styleColor: "#0f141d"
                transformOrigin: Item.Bottom
                scale: Math.min(2.2, Math.max(0.55, 0.85 / root.cameraScale))
                x: -width * 0.5
                y: -evacCircle.height * 0.5 - height - 2
            }
        }
    }

    // 6. Прогресс-бары строительства и демонтажа стен
    Repeater {
        model: root.world.wallProgressBars
        delegate: Item {
            x: modelData.cx
            y: modelData.cy
            z: 13

            Text {
                text: (modelData.isConstructing ? "СТРОИТЕЛЬСТВО  " : "ДЕМОНТАЖ  ") + modelData.timeLeft + "с"
                color: modelData.isConstructing ? "#fc8181" : "#f7fafc"
                font.pixelSize: 9
                font.bold: true
                style: Text.Outline
                styleColor: "#0f141d"
                transformOrigin: Item.Bottom
                scale: Math.min(2.0, Math.max(0.6, 0.85 / root.cameraScale))
                x: -width * 0.5
                y: -14 - height
            }

            Rectangle {
                width: 48; height: 4.5; x: -24; y: 10
                color: "#ee141923"
                border.color: "#2d3748"
                border.width: 0.5
                Rectangle {
                    width: Math.max(1, (parent.width - 1) * Math.min(1, Math.max(0, modelData.progress)))
                    height: parent.height - 1
                    x: 0.5; y: 0.5
                    color: modelData.isConstructing ? "#fc8181" : "#e2e8f0"
                }
            }
        }
    }

    // 7. Активные зоны эвакуации
    Repeater {
        model: root.world.activeEvacuations
        delegate: Item {
            x: modelData.x
            y: modelData.y
            z: 6

            Rectangle {
                width: 500; height: 500; radius: 250; x: -250; y: -250
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
                scale: Math.min(2.0, Math.max(0.6, 0.85 / root.cameraScale))
                x: -width * 0.5
                y: -254 - height
            }
        }
    }

    // 8. Зоны посадки десанта
    Repeater {
        model: root.world.dropZones
        delegate: Item {
            x: modelData.x
            y: modelData.y
            z: 8

            Rectangle {
                width: 34; height: 34; radius: 17; x: -17; y: -17
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
                scale: Math.min(2.0, Math.max(0.6, 0.85 / root.cameraScale))
                x: -width * 0.5
                y: -22 - height
            }
        }
    }

    // 9. Десантные вертолёты с тенью и вращающимся винтом
    Repeater {
        model: root.world.helicopters
        delegate: Item {
            x: modelData.x
            y: modelData.y
            z: 12

            Item {
                x: 14; y: 16
                rotation: modelData.angleDeg
                opacity: 0.32
                Rectangle { width: 26; height: 11; radius: 5; x: -11; y: -5.5; color: "#000000" }
                Rectangle { width: 14; height: 3; x: -23; y: -1.5; color: "#000000" }
                Item {
                    x: 2; y: 0
                    rotation: modelData.rotorDeg
                    Rectangle { width: 42; height: 2.5; x: -21; y: -1.25; color: "#000000" }
                    Rectangle { width: 2.5; height: 42; x: -1.25; y: -21; color: "#000000" }
                }
            }

            Item {
                rotation: modelData.angleDeg
                Rectangle { width: 14; height: 3.5; x: -23; y: -1.75; color: "#5f6f52"; border.color: "#2d3748"; border.width: 0.5 }
                Rectangle { width: 3; height: 10; x: -24; y: -5; color: "#5f6f52" }
                Rectangle { width: 26; height: 11; radius: 5.5; x: -11; y: -5.5; color: "#78866b"; border.color: "#2d3748"; border.width: 1 }
                Rectangle { width: 8; height: 8; radius: 4; x: 5; y: -4; color: "#90cdf4" }
                Rectangle {
                    width: 44; height: 44; radius: 22; x: -20; y: -22
                    color: "#18ffffff"; border.color: "#33ffffff"; border.width: 0.8
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
}

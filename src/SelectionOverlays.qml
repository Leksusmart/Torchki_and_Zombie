import QtQuick 2.15

// Карточки информации над выбранным зданием, юнитом или карантинной стеной
Item {
    id: root
    required property var world
    required property real cameraScale

    // 1. Карточка выбранного здания
    Rectangle {
        visible: root.world.selectedBuildingInfo.valid === true
        width: infoCol.implicitWidth + 14
        height: infoCol.implicitHeight + 10
        color: "#ee141923"
        border.color: "#63b3ed"
        border.width: 1
        radius: 4
        z: 9

        transformOrigin: Item.Bottom
        scale: Math.min(2.0, Math.max(0.55, 0.9 / root.cameraScale))

        x: (root.world.selectedBuildingInfo.cx || 0) - width * 0.5
        y: (root.world.selectedBuildingInfo.topY || 0) - height - 10

        Column {
            id: infoCol
            anchors.centerIn: parent
            spacing: 2

            Text {
                text: (root.world.selectedBuildingInfo.type || "") + " #" + root.world.selectedBuildingInfo.id
                color: "#63b3ed"
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                text: "Люди: " + (root.world.selectedBuildingInfo.occupants || 0)
                      + " / " + (root.world.selectedBuildingInfo.capacity || 0)
                color: "#e2e8f0"
                font.pixelSize: 9
            }
            Text {
                text: "Здоровье: " + (root.world.selectedBuildingInfo.health || 0)
                      + " / " + (root.world.selectedBuildingInfo.maxHealth || 0)
                color: "#68d391"
                font.pixelSize: 9
            }
        }
    }

    // 2. Карточка выбранного юнита
    Rectangle {
        visible: root.world.selectedUnitInfo.valid === true
        width: unitInfoCol.implicitWidth + 14
        height: unitInfoCol.implicitHeight + 10
        color: "#ee141923"
        border.color: "#f6ad55"
        border.width: 1
        radius: 4
        z: 11

        transformOrigin: Item.Bottom
        scale: Math.min(2.0, Math.max(0.55, 0.9 / root.cameraScale))

        x: (root.world.selectedUnitInfo.x || 0) - width * 0.5
        y: (root.world.selectedUnitInfo.y || 0) - height - 10

        Column {
            id: unitInfoCol
            anchors.centerIn: parent
            spacing: 2

            Text {
                text: (root.world.selectedUnitInfo.role || "") + " #" + (root.world.selectedUnitInfo.id || 0)
                      + "  (HP: " + (root.world.selectedUnitInfo.hp || 0) + ")"
                color: "#f6ad55"
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                text: "Статус: " + (root.world.selectedUnitInfo.state || "")
                color: "#e2e8f0"
                font.pixelSize: 9
            }
            Text {
                text: "Цель: " + (root.world.selectedUnitInfo.target || "Нет")
                color: "#68d391"
                font.pixelSize: 9
            }
            Text {
                text: "Локация: " + (root.world.selectedUnitInfo.inside || "Улица")
                color: "#a0aec0"
                font.pixelSize: 8
            }
        }
    }

    // 3. Карточка выбранной карантинной стены с кнопкой сноса
    Rectangle {
        visible: root.world.selectedWallInfo.valid === true
        width: 196
        height: 76
        color: "#f2111620"
        border.color: "#fc8181"
        border.width: 1
        radius: 2
        z: 15

        transformOrigin: Item.TopLeft
        scale: Math.min(2.0, Math.max(0.6, 0.9 / root.cameraScale))

        x: (root.world.selectedWallInfo.cx || 0) + 12
        y: (root.world.selectedWallInfo.cy || 0) - 10

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 5

            Text {
                text: "КАРАНТИННАЯ СТЕНА #" + (root.world.selectedWallInfo.id || 0)
                color: "#f7fafc"
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 0.5
            }
            Text {
                text: "Прочность: " + (root.world.selectedWallInfo.hp || 0)
                      + " / " + (root.world.selectedWallInfo.maxHp || 0)
                color: "#a0aec0"
                font.pixelSize: 9
            }
            Rectangle {
                width: parent.width
                height: 24
                color: teardownBtnArea.containsMouse ? "#4a1d24" : "#26161c"
                border.color: root.world.budget >= 200 ? "#fc8181" : "#4a5568"
                border.width: 1
                radius: 2

                Text {
                    anchors.centerIn: parent
                    text: "СНЕСТИ   200 ₽   (10с)"
                    color: root.world.budget >= 200 ? "#fc8181" : "#718096"
                    font.pixelSize: 10
                    font.bold: true
                }

                MouseArea {
                    id: teardownBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.world.startWallTeardown(root.world.selectedWallInfo.id)
                }
            }
        }
    }
}

import QtQuick 2.15
import QtQuick.Layouts 1.15

// Верхняя панель статистики, бюджета и управления скоростью симуляции
Rectangle {
    id: root
    required property var world
    signal restartRequested()

    height: 58
    color: "#ee0d1117"
    border.color: "#262f3f"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        // 1. Левый блок: Название уровня + время / день / сводка угроз
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
                Text { text: "•"; color: "#4a5568"; font.pixelSize: 13 }
                Text {
                    text: root.world.isNight ? "НОЧЬ" : "ДЕНЬ"
                    color: root.world.isNight ? "#63b3ed" : "#ecc94b"
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            Text {
                text: {
                    var status = "День " + root.world.dayNumber + "  •  " + root.world.formattedTime
                    if (root.world.zombiesCount > 0)
                        status += "  •  Активных зомби: " + root.world.zombiesCount
                    if (root.world.infectedCount > 0)
                        status += "  •  Инфицированных: " + root.world.infectedCount
                    if (root.world.zombiesCount === 0 && root.world.infectedCount === 0)
                        status += "  •  Угроз не обнаружено"
                    return status
                }
                color: root.world.zombiesCount > 0 ? "#fc8181"
                                                   : (root.world.infectedCount > 0 ? "#f6ad55" : "#9ae6b4")
                font.pixelSize: 10
                font.bold: true
            }
        }

        // 2. Центральные карточки статистики
        Repeater {
            model: [
                { title: "ЖИТЕЛИ", val: root.world.citizensCount, clr: "#e2e8f0" },
                { title: "ЗОМБИ", val: root.world.zombiesCount, clr: "#48bb78" },
                { title: "УБИТО", val: root.world.eliminatedCount, clr: "#ecc94b" },
                { title: "ЭВАКУИРОВАНО", val: root.world.evacuatedTotalCount, clr: "#4fd1c5" }
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
                onClicked: root.restartRequested()
            }
        }

        // 4. Кнопка "УЛУЧШЕНИЯ"
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
                    if (!root.world.paused)
                        root.world.togglePause()
                }
            }
        }

        // 5. Бюджет и доход
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
                text: root.world.budget.toLocaleString(Qt.locale("ru_RU"), "f", 0) + " ₽"
                color: "#68d391"
                font.pixelSize: 15
                font.bold: true
                anchors.right: parent.right
            }
            Text {
                text: "+" + root.world.incomePerSec.toFixed(0) + " ₽ / сек"
                color: "#68d391"
                font.pixelSize: 9
                anchors.right: parent.right
            }
        }

        // 6. Управление скоростью времени (Пауза | 1x | 2x | 3x | 4x)
        Row {
            spacing: 2
            Layout.alignment: Qt.AlignVCenter

            Rectangle {
                width: 30; height: 32
                color: root.world.paused ? "#2d3748" : "#141922"
                border.color: root.world.paused ? "#ecc94b" : "#2d3748"
                border.width: 1
                radius: 2
                Text {
                    anchors.centerIn: parent
                    text: "🛑"
                    color: root.world.paused ? "#ecc94b" : "#a0aec0"
                    font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { if (!root.world.paused) root.world.togglePause() }
                }
            }

            Repeater {
                model: [
                    { spd: 1, label: "Ⅰ" },
                    { spd: 2, label: "Ⅱ" },
                    { spd: 3, label: "Ⅲ" },
                    { spd: 4, label: "Ⅳ" }
                ]
                delegate: Rectangle {
                    width: 30; height: 32
                    property bool active: !root.world.paused && root.world.speedMultiplier === modelData.spd
                    color: active ? "#2b3648" : "#141922"
                    border.color: active ? "#63b3ed" : "#2d3748"
                    border.width: 1
                    radius: 2
                    Text {
                        anchors.centerIn: parent
                        text: modelData.label
                        color: active ? "#ffffff" : "#a0aec0"
                        font.pixelSize: 11
                        font.bold: true
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.world.paused) root.world.togglePause()
                            if (modelData.spd === 1) root.world.toggleSpeedTo1()
                            else if (modelData.spd === 2) root.world.toggleSpeedTo2()
                            else if (modelData.spd === 3) root.world.toggleSpeedTo3()
                            else if (modelData.spd === 4) root.world.toggleSpeedTo4()
                        }
                    }
                }
            }
        }
    }
}

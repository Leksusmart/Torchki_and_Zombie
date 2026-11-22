import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

// Меню выбора арсенала: категории и инструменты загружаются из C++ ToolRegistry (world.toolCategories)
Rectangle {
    id: root
    anchors.fill: parent
    color: "#fa0e1218"
    z: 100

    required property var world
    property int maxSlots: 10
    property var selectedIds: [1, 2, 3, 4]

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onWheel: (wheel) => { wheel.accepted = true }
    }

    function toggleTool(toolId, isLocked) {
        if (isLocked) return
        let list = selectedIds.slice()
        let idx = list.indexOf(toolId)
        if (idx !== -1) {
            list.splice(idx, 1)
        } else if (list.length < maxSlots) {
            list.push(toolId)
        }
        selectedIds = list
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 760)
        height: parent.height - 32
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4

            RowLayout {
                spacing: 10
                Text {
                    text: "ВАШ АРСЕНАЛ"
                    color: "#f7fafc"
                    font.pixelSize: 18
                    font.bold: true
                }
                Text { text: "•"; color: "#718096"; font.pixelSize: 18 }
                Text {
                    text: root.selectedIds.length + " / " + root.maxSlots
                    color: "#ecc94b"
                    font.pixelSize: 18
                    font.bold: true
                }
            }

            Text {
                text: "Нажмите на доступный инструмент, чтобы взять его в слот или убрать."
                color: "#a0aec0"
                font.pixelSize: 12
                Layout.fillWidth: true
            }
        }

        Flickable {
            id: flickArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentCol.implicitHeight + 16
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 6
            }

            ColumnLayout {
                id: contentCol
                width: flickArea.width - 12
                spacing: 16

                Repeater {
                    model: root.world.toolCategories
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: modelData.name
                            color: "#fc8181"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 10
                            rowSpacing: 10

                            Repeater {
                                model: modelData.items
                                delegate: Rectangle {
                                    id: card
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 100
                                    Layout.preferredHeight: 62

                                    property bool isLocked: modelData.locked === true
                                    property bool isSelected: !isLocked && root.selectedIds.indexOf(modelData.id) !== -1

                                    color: isLocked ? "#141821" : (isSelected ? "#1e293b" : "#181e29")
                                    border.color: isLocked ? "#262d3d" : (isSelected ? modelData.accent : "#4a5568")
                                    border.width: isSelected ? 2 : 1
                                    radius: 3

                                    Rectangle {
                                        visible: !card.isLocked
                                        width: 4
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        color: modelData.accent
                                        opacity: card.isSelected ? 1.0 : 0.4
                                    }

                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 12
                                        anchors.rightMargin: 10
                                        anchors.topMargin: 8
                                        anchors.bottomMargin: 8
                                        spacing: 4

                                        Text {
                                            text: modelData.title
                                            color: card.isLocked ? "#64748b" : "#f7fafc"
                                            font.pixelSize: 12
                                            font.bold: true
                                            elide: Text.ElideRight
                                            Layout.fillWidth: true
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            Text {
                                                text: card.isLocked ? modelData.lockText : modelData.cost
                                                color: card.isLocked ? "#9b2c2c" : "#68d391"
                                                font.pixelSize: 11
                                                font.bold: true
                                            }
                                            Text {
                                                visible: !card.isLocked && card.isSelected
                                                text: "• ВЗЯТО"
                                                color: modelData.accent
                                                font.pixelSize: 10
                                                font.bold: true
                                            }
                                            Item { Layout.fillWidth: true }
                                            Rectangle {
                                                visible: modelData.slotsTag !== undefined
                                                color: card.isLocked ? "#744210" : "#ecc94b"
                                                radius: 2
                                                Layout.preferredWidth: tagTxt.implicitWidth + 8
                                                Layout.preferredHeight: 16
                                                Text {
                                                    id: tagTxt
                                                    anchors.centerIn: parent
                                                    text: modelData.slotsTag || ""
                                                    color: card.isLocked ? "#cbd5e0" : "#1a202c"
                                                    font.pixelSize: 9
                                                    font.bold: true
                                                }
                                            }
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: card.isLocked ? Qt.ForbiddenCursor : Qt.PointingHandCursor
                                        onClicked: root.toggleTool(modelData.id, card.isLocked)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: applyBtn
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: applyArea.containsMouse ? "#38a169" : "#2f855a"
            border.color: "#68d391"
            border.width: 1
            radius: 4
            Text {
                anchors.centerIn: parent
                text: "ПРИМЕНИТЬ (" + root.selectedIds.length + " / " + root.maxSlots + ")"
                color: "#ffffff"
                font.pixelSize: 14
                font.bold: true
            }
            MouseArea {
                id: applyArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (root.world) {
                        root.world.equippedTools = root.selectedIds
                        if (root.world.paused)
                            root.world.togglePause()
                    }
                    root.visible = false
                }
            }
        }
    }
}

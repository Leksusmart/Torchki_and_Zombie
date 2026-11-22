import QtQuick 2.15

// Нижняя панель выбора активных способностей, динамически читающая ToolRegistry
Item {
    id: root
    required property var world

    readonly property var currentToolMeta: {
        var list = root.world.availableTools || []
        for (var i = 0; i < list.length; ++i) {
            if (list[i].id === root.world.selectedTool)
                return list[i]
        }
        return null
    }

    width: toolsRow.implicitWidth
    height: 120

    // Подсказка режима (показывается автоматически, если у выбранного инструмента есть hintText)
    Rectangle {
        visible: root.currentToolMeta !== null && (root.currentToolMeta.hintText || "") !== ""
        anchors.bottom: toolsRow.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 8
        width: hintRow.implicitWidth + 24
        height: 24
        color: "#ee0d1117"
        border.color: "#2d3748"
        border.width: 1
        radius: 2

        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: 16
            Text {
                text: root.currentToolMeta ? root.currentToolMeta.title : ""
                color: root.currentToolMeta ? root.currentToolMeta.accent : "#fc8181"
                font.pixelSize: 10
                font.bold: true
            }
            Text {
                text: root.currentToolMeta ? root.currentToolMeta.hintText : ""
                color: "#cbd5e0"
                font.pixelSize: 10
            }
            Text { text: "ПКМ — отмена выбора"; color: "#8b9bb4"; font.pixelSize: 10 }
        }
    }

    Row {
        id: toolsRow
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 12

        Repeater {
            model: root.world.availableTools
            delegate: Rectangle {
                visible: root.world.equippedTools.indexOf(modelData.id) !== -1
                width: 190
                height: 82
                color: root.world.selectedTool === modelData.id ? "#2b3648" : "#dd18202c"
                border.color: root.world.selectedTool === modelData.id ? modelData.accent : "#4a5568"
                border.width: root.world.selectedTool === modelData.id ? 2 : 1
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
                        root.world.selectedTool = (root.world.selectedTool === modelData.id) ? 0 : modelData.id
                    }
                }
            }
        }
    }
}

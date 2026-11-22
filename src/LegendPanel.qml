import QtQuick 2.15

// Сворачиваемая левая панель легенды цветов юнитов
Rectangle {
    id: root
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
                text: root.expanded ? "−" : "+"
                color: "#90cdf4"
                font.pixelSize: 13
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.PointingHandCursor
                onClicked: root.expanded = !root.expanded
            }
        }

        Rectangle {
            visible: root.expanded
            width: parent.width
            height: 1
            color: "#262f3f"
        }

        Repeater {
            model: root.expanded ? [
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

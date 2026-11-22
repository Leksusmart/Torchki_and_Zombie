import QtQuick 2.15

// Сворачиваемая правая панель оперативных уведомлений о вспышках и смене суток
Rectangle {
    id: root
    required property var world
    property bool expanded: true
    property int unreadCount: 0
    property bool lastIsNight: world.isNight

    function resetAlerts() {
        alertsModel.clear()
        unreadCount = 0
        lastIsNight = world.isNight
    }

    Connections {
        target: root.world
        function onOutbreakOccurred(x, y, index) {
            alertsModel.insert(0, {
                text: "ВСПЫШКА ЗАРАЖЕНИЯ #" + index + ": зафиксирован нулевой пациент в секторе ("
                      + Math.round(x) + ", " + Math.round(y) + ").",
                accent: "#fc8181"
            })
            if (alertsModel.count > 6) alertsModel.remove(6)
            if (!root.expanded) root.unreadCount++
        }
        function onIsNightChanged() {
            if (root.lastIsNight === root.world.isNight) return
            root.lastIsNight = root.world.isNight

            var msg = root.world.isNight
                    ? "Наступила ночь. Видимость снижена, следите за очагами пожаров."
                    : "Наступил день " + root.world.dayNumber + ". Жители направляются на работу."
            var clr = root.world.isNight ? "#63b3ed" : "#ecc94b"

            alertsModel.insert(0, { text: msg, accent: clr })
            if (alertsModel.count > 6) alertsModel.remove(6)
            if (!root.expanded) root.unreadCount++
        }
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
                    visible: !root.expanded && root.unreadCount > 0
                    width: 16; height: 14
                    radius: 2
                    color: "#e53e3e"
                    Text {
                        anchors.centerIn: parent
                        text: root.unreadCount
                        color: "#ffffff"
                        font.pixelSize: 9
                        font.bold: true
                    }
                }
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
                onClicked: {
                    root.expanded = !root.expanded
                    if (root.expanded)
                        root.unreadCount = 0
                }
            }
        }

        Rectangle {
            visible: root.expanded
            width: parent.width
            height: 1
            color: "#262f3f"
        }

        Repeater {
            model: root.expanded ? alertsModel : null
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

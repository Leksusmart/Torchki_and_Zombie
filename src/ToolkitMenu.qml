import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    anchors.fill: parent
    color: "#fa0e1218" // Плотный тёмный фон без просвечивания карты
    z: 100

    property var world: null
    property int maxSlots: 10
    property var selectedIds: [1, 2, 3]

    // Блокируем клики и скролл карты под меню
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

    readonly property var categoriesData: [
        {
            name: "ОБОРОНА",
            items: [
                { id: 101, title: "Карантинная стена", cost: "25 ₽+", accent: "#e53e3e", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 102, title: "Блокпост", cost: "120 ₽+", accent: "#38b2ac", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 103, title: "Укрепление", cost: "250 ₽", accent: "#ecc94b", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 104, title: "Снайперское гнездо", cost: "450 ₽", accent: "#fc8181", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 105, title: "Минное поле", cost: "550 ₽", accent: "#f6ad55", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        },
        {
            name: "ВОЙСКА",
            items: [
                { id: 2, title: "Отряд десанта", cost: "200 ₽", accent: "#4299e1", locked: false, desc: "Высадка 5 солдат на точку" },
                { id: 201, title: "Чинук", cost: "900 ₽", accent: "#63b3ed", locked: true, lockText: "ЗАБЛОКИРОВАНО", slotsTag: "2 СЛОТА" },
                { id: 202, title: "Сигнальная ракета", cost: "350 ₽", accent: "#f56565", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 203, title: "Отряд зачистки", cost: "400 ₽", accent: "#a0aec0", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        },
        {
            name: "АТАКИ",
            items: [
                { id: 301, title: "Ковровая бомбардировка", cost: "300 ₽", accent: "#ed8936", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 302, title: "Авиаудар", cost: "700 ₽", accent: "#f6ad55", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 303, title: "Напалмовый удар", cost: "800 ₽", accent: "#dd6b20", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 304, title: "Боевой вертолёт", cost: "650 ₽", accent: "#cbd5e0", locked: true, lockText: "ЗАБЛОКИРОВАНО", slotsTag: "2 СЛОТА" },
                { id: 305, title: "Апач", cost: "850 ₽", accent: "#ecc94b", locked: true, lockText: "ЗАБЛОКИРОВАНО", slotsTag: "2 СЛОТА" },
                { id: 306, title: "Миномётный обстрел", cost: "900 ₽", accent: "#fc8181", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 307, title: "Звуковая приманка", cost: "300 ₽", accent: "#b794f4", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 308, title: "Дымовая завеса", cost: "450 ₽", accent: "#a0aec0", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        },
        {
            name: "МЕДИЦИНА И РАЗВЕДКА",
            items: [
                { id: 401, title: "Разведдрон", cost: "40 ₽", accent: "#38b2ac", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 402, title: "Вышка разведки", cost: "250 ₽", accent: "#4fd1c5", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 1, title: "Вакцинирующая капсула", cost: "90 ₽", accent: "#4299e1", locked: false, desc: "Лечит и вакцинирует в радиусе 45м" },
                { id: 403, title: "Палатка вакцинации", cost: "600 ₽", accent: "#63b3ed", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        },
        {
            name: "ЭКОНОМИКА",
            items: [
                { id: 501, title: "Склад припасов", cost: "250 ₽", accent: "#ecc94b", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 502, title: "Продовольственный склад", cost: "400 ₽", accent: "#d69e2e", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        },
        {
            name: "ЭВАКУАЦИЯ",
            items: [
                { id: 3, title: "Точка эвакуации", cost: "75 ₽", accent: "#48bb78", locked: false, desc: "Инициация эвакуации на выходе" },
                { id: 601, title: "Эвакуация с крыши", cost: "150 ₽", accent: "#38b2ac", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 602, title: "Полевое убежище", cost: "500 ₽", accent: "#68d391", locked: true, lockText: "ЗАБЛОКИРОВАНО" },
                { id: 603, title: "Зона эвакуации", cost: "ВСЕГДА", accent: "#ecc94b", locked: true, lockText: "ЗАБЛОКИРОВАНО" }
            ]
        }
    ]

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 760)
        height: parent.height - 32
        spacing: 12

        // 1. Шапка меню
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
                Text {
                    text: "•"
                    color: "#718096"
                    font.pixelSize: 18
                }
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

        // 2. Список категорий через Flickable (гарантирует правильную ширину карточек)
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
                    model: root.categoriesData
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

                                    // Левая цветная полоска-акцент
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
        // 3. Нижняя кнопка "Применить"
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
                        if (root.world.paused) {
                            root.world.togglePause()
                        }
                    }
                    root.visible = false
                }
            }
        }
    }
}
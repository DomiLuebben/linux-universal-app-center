import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal appSelected(string appKey)

    function focusSearch() {
        searchField.forceActiveFocus()
        searchField.selectAll()
    }

    function setCategory(cat) {
        selectedCategory = cat
        storeModel.category = (cat === qsTr("Alle") ? "" : cat)
    }

    property string selectedCategory: qsTr("Alle")

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        // Kopfzeile: Titel & Suchfeld
        Row {
            width: parent.width
            spacing: Theme.s3

            TextField {
                id: searchField
                width: parent.width - modeSwitcher.width - parent.spacing
                placeholderText: storeModel.packagesOnly
                                 ? qsTr("Repository-Pakete durchsuchen (z.B. ripgrep, linux-headers) …")
                                 : qsTr("Anwendungen durchsuchen (z.B. GIMP, Firefox, Texteditor) …")
                font.pixelSize: 14
                color: Theme.text
                text: storeModel.searchQuery

                background: Rectangle {
                    radius: Theme.radiusControl
                    color: Theme.surfaceSunken
                    border.color: searchField.activeFocus ? Theme.accent : Theme.separator
                    border.width: searchField.activeFocus ? 2 : 1
                }

                onTextChanged: {
                    storeModel.search(text)
                }

                Keys.onEscapePressed: function(event) {
                    if (searchField.text.length > 0) {
                        searchField.text = ""
                        event.accepted = true
                    }
                }

                // Lupe links
                leftPadding: 36
                Kirigami.Icon {
                    source: "edit-find"
                    width: 18
                    height: 18
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.textMuted
                }

                // Löschen-Knopf rechts
                rightPadding: clearBtn.visible ? 32 : 12
                Kirigami.Icon {
                    id: clearBtn
                    visible: searchField.text.length > 0
                    source: "edit-clear"
                    width: 16
                    height: 16
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchField.text = ""
                            searchField.forceActiveFocus()
                        }
                    }
                }
            }

            // Umschalter: Anwendungen / Alle Pakete
            Rectangle {
                id: modeSwitcher
                width: 230
                height: searchField.height
                radius: Theme.radiusControl
                color: Theme.surfaceSunken
                border.color: Theme.separator
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2

                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: !storeModel.packagesOnly ? Theme.surfaceRaised : "transparent"
                        border.color: !storeModel.packagesOnly ? Theme.separator : "transparent"
                        border.width: !storeModel.packagesOnly ? 1 : 0

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Anwendungen")
                            font.pixelSize: 12
                            font.weight: !storeModel.packagesOnly ? Font.DemiBold : Font.Normal
                            color: !storeModel.packagesOnly ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                storeModel.packagesOnly = false
                            }
                        }
                    }

                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: storeModel.packagesOnly ? Theme.surfaceRaised : "transparent"
                        border.color: storeModel.packagesOnly ? Theme.separator : "transparent"
                        border.width: storeModel.packagesOnly ? 1 : 0

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Alle Pakete")
                            font.pixelSize: 12
                            font.weight: storeModel.packagesOnly ? Font.DemiBold : Font.Normal
                            color: storeModel.packagesOnly ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                storeModel.packagesOnly = true
                            }
                        }
                    }
                }
            }
        }

        // Filterleiste: Kategorien & Nur Installierte
        Row {
            width: parent.width
            spacing: Theme.s4
            visible: !storeModel.packagesOnly

            ScrollView {
                width: parent.width - 160
                height: 36
                contentHeight: 36
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOff

                Row {
                    spacing: Theme.s2

                    readonly property var categoryList: [
                        qsTr("Alle"),
                        qsTr("Internet"),
                        qsTr("Büro"),
                        qsTr("Grafik"),
                        qsTr("Audio & Video"),
                        qsTr("Spiele"),
                        qsTr("Entwicklung"),
                        qsTr("Bildung & Wissenschaft"),
                        qsTr("Werkzeuge")
                    ]

                    Repeater {
                        model: parent.categoryList
                        delegate: Rectangle {
                            implicitWidth: catLabel.implicitWidth + Theme.s4 * 2
                            implicitHeight: 30
                            radius: Theme.radiusChip
                            color: {
                                if (root.selectedCategory === modelData) {
                                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                }
                                return catFilterMouse.containsMouse ? Theme.surfaceRaised : Theme.surfaceSunken
                            }
                            border.color: root.selectedCategory === modelData ? Theme.accent : Theme.separator
                            border.width: 1

                            Text {
                                id: catLabel
                                anchors.centerIn: parent
                                text: modelData
                                font.pixelSize: 12
                                font.weight: root.selectedCategory === modelData ? Font.DemiBold : Font.Normal
                                color: root.selectedCategory === modelData ? Theme.accent : Theme.text
                            }

                            MouseArea {
                                id: catFilterMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.setCategory(modelData)
                            }
                        }
                    }
                }
            }

            // Schalter: Nur installierte
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2

                CheckBox {
                    id: installedCheck
                    anchors.verticalCenter: parent.verticalCenter
                    checked: storeModel.installedOnly
                    onToggled: storeModel.installedOnly = checked
                }

                Text {
                    text: qsTr("Nur installierte")
                    font.pixelSize: 13
                    color: Theme.text
                    anchors.verticalCenter: parent.verticalCenter

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            installedCheck.checked = !installedCheck.checked
                            storeModel.installedOnly = installedCheck.checked
                        }
                    }
                }
            }
        }

        // Trefferzähler
        Text {
            text: {
                if (storeModel.packagesOnly) {
                    return qsTr("%1 Pakete gefunden").arg(storeModel.count)
                }
                return qsTr("%1 Anwendungen gefunden").arg(storeModel.count)
            }
            font.pixelSize: 13
            font.features: ({ "tnum": 1 })
            color: Theme.textMuted
        }

        // Inhaltsbereich: Grid (Anwendungen) oder List (Pakete)
        Item {
            width: parent.width
            height: parent.height - y

            // Anwendungen-Ansicht: GridView
            GridView {
                id: appsGrid
                visible: !storeModel.packagesOnly && storeModel.count > 0
                anchors.fill: parent
                clip: true
                cellWidth: Math.max(280, Math.floor(width / Math.max(1, Math.floor(width / 290))))
                cellHeight: 120
                model: storeModel

                delegate: Item {
                    width: appsGrid.cellWidth
                    height: appsGrid.cellHeight

                    AppCard {
                        anchors.fill: parent
                        anchors.margins: Theme.s2
                        appKey: model.appKey
                        name: model.name
                        summary: model.summary
                        iconSource: model.iconSource
                        actionState: model.actionState
                        isInstalled: model.isInstalled
                        origin: model.origin
                        onClicked: root.appSelected(model.appKey)
                    }
                }
            }

            // Alle Pakete-Ansicht: ListView
            ListView {
                id: packagesList
                visible: storeModel.packagesOnly && storeModel.count > 0
                anchors.fill: parent
                clip: true
                spacing: Theme.s2
                model: storeModel

                delegate: Card {
                    width: packagesList.width
                    implicitHeight: 52

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s4
                        anchors.rightMargin: Theme.s4
                        spacing: Theme.s3

                        Kirigami.Icon {
                            source: "package-x-generic"
                            width: 24
                            height: 24
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Column {
                            width: parent.width - 200
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Text {
                                text: model.name
                                textFormat: Text.PlainText
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.text
                            }

                            Text {
                                text: model.summary
                                textFormat: Text.PlainText
                                font.pixelSize: 12
                                color: Theme.textMuted
                            }
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s2

                            Chip {
                                visible: model.isInstalled
                                text: qsTr("Installiert")
                                variant: "neutral"
                            }

                            PrimaryButton {
                                visible: !model.isInstalled
                                text: qsTr("Installieren")
                                variant: "primary"
                                onClicked: appStore.requestInstall(model.appKey)
                            }
                        }
                    }
                }
            }

            // Leere Zustände
            EmptyState {
                visible: storeModel.count === 0 && (appStore ? appStore.isLoaded : true)
                anchors.centerIn: parent
                title: searchField.text.length > 0
                       ? qsTr("Keine Treffer für „%1“").arg(searchField.text)
                       : qsTr("Keine passenden Anwendungen gefunden")
                message: storeModel.packagesOnly
                          ? qsTr("Überprüfe die Schreibweise des Paketnamens.")
                          : qsTr("Versuche einen anderen Suchbegriff oder wechsle oben zu „Alle Pakete“.")
            }
        }
    }
}

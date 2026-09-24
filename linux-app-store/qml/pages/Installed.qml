import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal appSelected(string appKey)

    property int selectedTab: (typeof initialInstalledTab !== "undefined") ? initialInstalledTab : 0

    // Eingebettet in „Verwalten": Titel und Reiter kommen von dort.
    property bool compact: false

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        // Kopfzeile mit Titel und Segment-Umschalter
        Item {
            width: parent.width
            height: 36
            visible: !root.compact

            Text {
                text: qsTr("Installiert")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Theme.text
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }

            // Segmentierter Umschalter: Anwendungen / Alle Pakete & Pflege
            Rectangle {
                width: 320
                height: 36
                radius: Theme.radiusControl
                color: Theme.surfaceSunken
                border.color: Theme.separator
                border.width: 1
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2

                    // Reiter 1: Anwendungen
                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: root.selectedTab === 0 ? Theme.surface : "transparent"
                        border.color: root.selectedTab === 0 ? Theme.separator : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Anwendungen")
                            font.pixelSize: 13
                            font.weight: root.selectedTab === 0 ? Font.DemiBold : Font.Normal
                            color: root.selectedTab === 0 ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedTab = 0
                        }
                    }

                    // Reiter 2: Alle Pakete & Systempflege
                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: root.selectedTab === 1 ? Theme.surface : "transparent"
                        border.color: root.selectedTab === 1 ? Theme.separator : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Alle Pakete & Pflege")
                            font.pixelSize: 13
                            font.weight: root.selectedTab === 1 ? Font.DemiBold : Font.Normal
                            color: root.selectedTab === 1 ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedTab = 1
                        }
                    }
                }
            }
        }

        // ====================================================================
        // ANSICHT 1: Installierte Anwendungen (Standard)
        // ====================================================================
        Column {
            visible: root.selectedTab === 0
            width: parent.width
            height: parent.height - 56
            spacing: Theme.s4

            // Suchzeile für installierte Anwendungen
            Row {
                width: parent.width
                spacing: Theme.s3

                TextField {
                    id: appSearchField
                    width: parent.width - 180
                    placeholderText: qsTr("Installierte Anwendungen durchsuchen …")
                    font.pixelSize: 14
                    color: Theme.text
                    text: installedStoreModel.searchQuery

                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.surfaceSunken
                        border.color: appSearchField.activeFocus ? Theme.accent : Theme.separator
                        border.width: appSearchField.activeFocus ? 2 : 1
                    }

                    onTextChanged: installedStoreModel.search(text)

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
                    rightPadding: appClearBtn.visible ? 32 : 12
                    Kirigami.Icon {
                        id: appClearBtn
                        visible: appSearchField.text.length > 0
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
                                appSearchField.text = ""
                                appSearchField.forceActiveFocus()
                            }
                        }
                    }
                }

                Text {
                    text: qsTr("%1 Anwendungen").arg(installedStoreModel.count)
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Leerer Zustand bei 0 Anwendungen
            Item {
                visible: installedStoreModel.count === 0
                width: parent.width
                height: 250

                EmptyState {
                    title: qsTr("Keine Anwendungen gefunden")
                    message: qsTr("Die Suche lieferte keine passenden installierten Anwendungen.")
                }
            }

            // Grid der installierten Anwendungen
            GridView {
                id: appGrid
                visible: installedStoreModel.count > 0
                width: parent.width
                height: parent.height - 60
                clip: true
                reuseItems: true
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 320)))
                cellHeight: 160
                model: installedStoreModel

                delegate: Item {
                    width: appGrid.cellWidth
                    height: appGrid.cellHeight

                    AppCard {
                        anchors.fill: parent
                        anchors.margins: 4
                        appKey: model.appKey
                        name: model.name
                        summary: model.summary
                        iconSource: model.iconSource
                        actionState: model.actionState
                        isInstalled: true
                        origin: model.origin
                        onClicked: root.appSelected(model.appKey)
                    }
                }
            }
        }

        // ====================================================================
        // ANSICHT 2: Alle Pakete & Systempflege
        // ====================================================================
        Column {
            visible: root.selectedTab === 1
            width: parent.width
            height: parent.height - 56
            spacing: Theme.s4

            // Hygiene- & Aufräum-Karte
            Card {
                width: parent.width
                height: 110

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s5

                    Column {
                        width: parent.width - 340
                        spacing: 4
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            text: qsTr("System aufräumen")
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text {
                            text: qsTr("%1 Waisenpakete · %2 Paketcache freigebbar")
                                  .arg(installedModel.orphanCount)
                                  .arg(installedModel.cleanableCacheFormatted)
                            font.pixelSize: 13
                            font.features: ({ "tnum": 1 })
                            color: Theme.textMuted
                        }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s3

                        PrimaryButton {
                            text: qsTr("Waisen entfernen")
                            variant: "quiet"
                            enabled: installedModel.orphanCount > 0 && !daemonClient.isBusy
                            onClicked: installedModel.cleanOrphans()
                        }

                        PrimaryButton {
                            text: qsTr("Cache leeren")
                            variant: "quiet"
                            enabled: !daemonClient.isBusy
                            onClicked: installedModel.cleanCache()
                        }
                    }
                }
            }

            // Suchfeld für alle Pakete
            Row {
                width: parent.width
                spacing: Theme.s3

                TextField {
                    id: searchBox
                    width: parent.width - 160
                    placeholderText: qsTr("Alle Pakete durchsuchen (z.B. ripgrep, linux-headers) …")
                    font.pixelSize: 14
                    color: Theme.text
                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.surfaceSunken
                        border.color: searchBox.activeFocus ? Theme.accent : Theme.separator
                        border.width: 1
                    }
                    onTextChanged: installedModel.search(text)
                }

                Text {
                    text: qsTr("%1 Pakete").arg(installedModel.totalCount)
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // Leerer Zustand
            Item {
                visible: installedModel.totalCount === 0 && !installedModel.isSearching
                width: parent.width
                height: 250

                EmptyState {
                    title: qsTr("Keine Pakete gefunden")
                    message: qsTr("Die Suche lieferte keine passenden installierten Pakete.")
                }
            }

            // Paketliste
            ListView {
                id: installedList
                visible: installedModel.totalCount > 0
                width: parent.width
                height: parent.height - 195
                clip: true
                reuseItems: true
                spacing: 4
                model: installedModel

                delegate: Rectangle {
                    width: installedList.width
                    height: 56
                    radius: Theme.radiusControl
                    color: Theme.surface

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.s4
                        spacing: Theme.s4

                        Column {
                            width: parent.width - 240
                            spacing: 2
                            anchors.verticalCenter: parent.verticalCenter

                            Row {
                                spacing: Theme.s3
                                Text {
                                    text: model.name
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }
                                Text {
                                    text: model.version
                                    font.pixelSize: 12
                                    font.features: ({ "tnum": 1 })
                                    color: Theme.textMuted
                                }
                                Text {
                                    text: model.arch
                                    font.pixelSize: 11
                                    color: Theme.textMuted
                                }
                            }

                            Text {
                                text: model.description
                                font.pixelSize: 12
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.s2

                            Chip {
                                text: qsTr("Waise")
                                variant: "neutral"
                                visible: model.isOrphan
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Chip {
                                text: qsTr("Alter Kernel")
                                variant: "kernel"
                                visible: model.isOldKernel
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: model.sizeFormatted
                                font.pixelSize: 13
                                font.weight: Font.Medium
                                font.features: ({ "tnum": 1 })
                                color: Theme.text
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                    }
                }
            }
        }
    }
}

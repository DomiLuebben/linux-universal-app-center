import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        Text {
            text: qsTr("Installierte Pakete & Hygiene")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

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
                        enabled: installedModel.orphanCount > 0
                        onClicked: installedModel.cleanOrphans()
                    }

                    PrimaryButton {
                        text: qsTr("Cache leeren")
                        variant: "quiet"
                        onClicked: installedModel.cleanCache()
                    }
                }
            }
        }

        // Suchfeld
        Row {
            width: parent.width
            spacing: Theme.s3

            TextField {
                id: searchBox
                width: parent.width - 160
                placeholderText: qsTr("Installierte Pakete durchsuchen...")
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
        EmptyState {
            visible: installedModel.totalCount === 0 && !installedModel.isSearching
            title: qsTr("Keine Pakete gefunden")
            message: qsTr("Die Suche lieferte keine passenden installierten Pakete.")
        }

        // Paketliste
        ListView {
            id: installedList
            visible: installedModel.totalCount > 0
            width: parent.width
            height: parent.height - 240
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

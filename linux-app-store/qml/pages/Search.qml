import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal appSelected(string appKey)

    function setCategory(cat) {
        storeModel.collection = ""
        storeModel.category = cat
    }

    // Wird von der Sammlungsüberschrift "Alle anzeigen" gesetzt
    property string collectionTitle: ""

    readonly property string pageTitle: {
        if (storeModel.searchQuery.length > 0) return qsTr("Ergebnisse für \u201E%1\u201C").arg(storeModel.searchQuery)
        if (storeModel.collection.length > 0) return root.collectionTitle.length > 0 ? root.collectionTitle : qsTr("Sammlung")
        if (storeModel.category.length > 0) return storeModel.category
        return storeModel.packagesOnly ? qsTr("Alle Pakete") : qsTr("Alle Apps")
    }

    // Spaltenzahl wie auf „Entdecken"
    readonly property int gridColumns: Math.max(2, Math.min(5, Math.floor(width / 280)))

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        // Kopfzeile: Titel; gesucht wird über das Feld oben im Fenster
        RowLayout {
            width: parent.width
            spacing: Theme.s3

            Text {
                Layout.fillWidth: true
                text: root.pageTitle
                textFormat: Text.PlainText
                font.pixelSize: 26
                font.weight: Font.Bold
                color: Theme.text
                elide: Text.ElideRight
            }

            // Umschalter: Anwendungen / Alle Pakete
            Rectangle {
                id: modeSwitcher
                Layout.preferredWidth: 230
                Layout.preferredHeight: 34
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

        // Filterleiste: Quelle & nur installierte
        Row {
            width: parent.width
            spacing: Theme.s4
            visible: !storeModel.packagesOnly

            // Quellenfilter (Abschnitt 8.3)
            ComboBox {
                id: sourceFilterCombo
                visible: typeof flatpakUpdates !== "undefined" && flatpakUpdates && flatpakUpdates.available
                anchors.verticalCenter: parent.verticalCenter
                model: {
                    var items = [
                        { label: qsTr("Alle Quellen"), value: "all" },
                        { label: qsTr("Nativ"), value: "native" }
                    ]
                    if (typeof flatpakUpdates !== "undefined" && flatpakUpdates && flatpakUpdates.available) {
                        items.push({ label: qsTr("Flatpak"), value: "flatpak" })
                    }
                    return items
                }
                textRole: "label"
                currentIndex: 0
                onActivated: function(index) {
                    storeModel.sourceFilter = model[index].value
                }
                delegate: ItemDelegate {
                    width: sourceFilterCombo.width
                    contentItem: Text {
                        text: modelData.label
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    highlighted: sourceFilterCombo.highlightedIndex === index
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
            AppTile {
                id: tileMetrics
                visible: false
                width: 260
                name: "Muster"
                summary: "Muster"
            }

            GridView {
                id: appsGrid
                visible: !storeModel.packagesOnly && storeModel.count > 0
                anchors.fill: parent
                clip: true
                cellWidth: Math.floor(width / root.gridColumns)
                // Höhe von einer echten Kachel übernehmen, damit nichts gequetscht wird
                cellHeight: tileMetrics.implicitHeight
                model: storeModel

                delegate: Item {
                    width: appsGrid.cellWidth
                    height: appsGrid.cellHeight

                    AppTile {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.s3
                        appKey: model.appKey
                        name: model.name
                        summary: model.summary
                        iconSource: model.iconSource
                        actionState: model.actionState
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
                title: storeModel.searchQuery.length > 0
                       ? qsTr("Keine Treffer für „%1“").arg(storeModel.searchQuery)
                       : qsTr("Keine passenden Anwendungen gefunden")
                message: storeModel.packagesOnly
                          ? qsTr("Überprüfe die Schreibweise des Paketnamens.")
                          : qsTr("Versuche einen anderen Suchbegriff oder wechsle oben zu „Alle Pakete“.")
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal appSelected(string appKey)
    signal searchRequested()
    signal categorySelected(string category)
    signal collectionSelected(string collectionId, string title)

    // Spaltenzahl wie im Mac App Store: aus der Breite, Kacheln 250–340 px.
    readonly property int gridColumns: Math.max(2, Math.min(5, Math.floor(contentColumn.width / 270)))

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Column {
            id: contentColumn
            width: Math.min(parent.width - Theme.s6 * 2, 1400)
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: Theme.s6
            bottomPadding: Theme.s6
            spacing: Theme.s5

            // Kopfzeile
            Column {
                width: parent.width
                spacing: 2

                Text {
                    text: qsTr("Entdecken")
                    font.pixelSize: 26
                    font.weight: Font.Bold
                    color: Theme.text
                }
                Text {
                    text: qsTr("Anwendungen aus deinen Paketquellen")
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }

            // Wenn Katalog lädt
            Rectangle {
                visible: appStore ? appStore.isLoading : false
                width: parent.width
                height: 80
                radius: Theme.radiusCard
                color: Theme.surfaceSunken
                border.color: Theme.separator
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.s3

                    Kirigami.Icon {
                        source: "view-refresh"
                        width: 20
                        height: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: qsTr("Anwendungskatalog wird geladen …")
                        font.pixelSize: 14
                        color: Theme.textMuted
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // Wenn Katalogfehler auftrat
            Rectangle {
                visible: Boolean(appStore && appStore.lastError && appStore.lastError.length > 0)
                width: parent.width
                implicitHeight: 48
                radius: Theme.radiusCard
                color: Theme.surfaceSunken
                border.color: Theme.neutral
                border.width: 1

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.s3

                    Kirigami.Icon {
                        source: "dialog-warning"
                        width: 20
                        height: 20
                        color: Theme.neutral
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: qsTr("Metadaten-Hinweis: %1").arg(appStore ? appStore.lastError : "")
                        font.pixelSize: 13
                        color: Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }


            // Redaktionelle Sammlungen als dichtes Raster (Mac App Store)
            Repeater {
                model: { if (!appStore) return []; appStore.revision; return appStore.curatedCollections() }
                delegate: Column {
                    id: collectionSection
                    width: parent.width
                    spacing: Theme.s2
                    topPadding: Theme.s3

                    readonly property var collectionApps: appStore ? appStore.getCuratedCollection(modelData.id) : []
                    visible: collectionApps.length > 0

                    RowLayout {
                        width: parent.width

                        Column {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.title
                                font.pixelSize: 20
                                font.weight: Font.Bold
                                color: Theme.text
                            }
                            Text {
                                text: modelData.description
                                font.pixelSize: 12
                                color: Theme.textMuted
                            }
                        }

                        Button {
                            id: showAll
                            flat: true
                            text: qsTr("Alle anzeigen")
                            onClicked: root.collectionSelected(modelData.id, modelData.title)
                            contentItem: Text {
                                text: showAll.text
                                font.pixelSize: 13
                                color: Theme.accent
                            }
                            background: Item {}
                        }
                    }

                    Grid {
                        width: parent.width
                        columns: root.gridColumns
                        columnSpacing: Theme.s4
                        rowSpacing: 0

                        Repeater {
                            // Wie im Mac App Store höchstens drei Reihen je Sammlung
                            model: collectionSection.collectionApps.slice(0, root.gridColumns * 3)
                            delegate: AppTile {
                                width: (collectionSection.width - Theme.s4 * (root.gridColumns - 1)) / root.gridColumns
                                appKey: modelData.appKey ? modelData.appKey : ""
                                name: modelData.name ? modelData.name : ""
                                summary: modelData.summary ? modelData.summary : ""
                                iconSource: modelData.iconSource ? modelData.iconSource : ""
                                actionState: appStore ? appStore.getActionState(modelData.appKey) : "Available"
                                onClicked: root.appSelected(modelData.appKey)
                            }
                        }
                    }
                }
            }

            // Zustandsanzeige bei leerem Katalog
            Item {
                width: parent.width
                height: 200
                visible: (appStore ? appStore.isLoaded && appStore.totalAppCount === 0 : false)
                EmptyState {
                title: qsTr("Keine Store-Metadaten gefunden")
                message: qsTr("AppStream-Metadaten sind auf diesem System noch nicht synchronisiert. System-Updates und die Paketverwaltung funktionieren weiterhin regulär.")
                }
            }
        }
    }
}

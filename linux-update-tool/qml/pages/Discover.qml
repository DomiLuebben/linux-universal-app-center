import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal appSelected(string appKey)
    signal searchRequested()
    signal categorySelected(string category)

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Column {
            width: Math.min(parent.width - Theme.s6 * 2, 1400)
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: Theme.s6
            bottomPadding: Theme.s6
            spacing: Theme.s5

            // Kopfzeile mit Titel und Schnellsuche-Schaltfläche
            Row {
                width: parent.width
                height: 44

                Column {
                    width: parent.width - 180
                    spacing: 2
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: qsTr("Apps entdecken")
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Text {
                        text: qsTr("Anwendungen aus deinen Paketquellen")
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }

                PrimaryButton {
                    text: qsTr("Suchen …")
                    iconName: "edit-find"
                    variant: "quiet"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: root.searchRequested()
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

            // Ruhiger hervorgehobener Themenbereich (Hero Banner)
            Card {
                width: parent.width
                implicitHeight: 140
                color: Theme.surfaceRaised

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.s5
                    spacing: Theme.s5

                    Column {
                        width: parent.width - 200
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s2

                        Text {
                            text: qsTr("Kreativ arbeiten")
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            color: Theme.text
                        }

                        Text {
                            text: qsTr("Auswahl tatsächlich verfügbarer Anwendungen für Grafik, Medien und Design")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }

                        PrimaryButton {
                            text: qsTr("Auswahl ansehen")
                            variant: "primary"
                            iconName: "go-next"
                            onClicked: root.categorySelected("Grafik")
                        }
                    }

                    Kirigami.Icon {
                        source: "applications-graphics"
                        width: 80
                        height: 80
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: 0.85
                    }
                }
            }

            // Kategorie-Pills
            Column {
                width: parent.width
                spacing: Theme.s2

                Text {
                    text: qsTr("Kategorien")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Flow {
                    width: parent.width
                    spacing: Theme.s2

                    readonly property var categories: [
                        { label: qsTr("Internet"), icon: "applications-internet" },
                        { label: qsTr("Büro"), icon: "applications-office" },
                        { label: qsTr("Grafik"), icon: "applications-graphics" },
                        { label: qsTr("Audio & Video"), icon: "applications-multimedia" },
                        { label: qsTr("Spiele"), icon: "applications-games" },
                        { label: qsTr("Entwicklung"), icon: "applications-development" },
                        { label: qsTr("Bildung & Wissenschaft"), icon: "applications-science" },
                        { label: qsTr("Werkzeuge"), icon: "applications-utilities" }
                    ]

                    Repeater {
                        model: parent.categories
                        delegate: Rectangle {
                            implicitWidth: catRow.implicitWidth + Theme.s4 * 2
                            implicitHeight: 34
                            radius: Theme.radiusChip
                            color: catMouse.containsMouse ? Theme.surfaceRaised : Theme.surfaceSunken
                            border.color: catMouse.containsMouse ? Theme.accent : Theme.separator
                            border.width: 1

                            Row {
                                id: catRow
                                anchors.centerIn: parent
                                spacing: Theme.s2

                                Kirigami.Icon {
                                    source: modelData.icon
                                    width: 16
                                    height: 16
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Text {
                                    text: modelData.label
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                    color: Theme.text
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: catMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.categorySelected(modelData.label)
                            }
                        }
                    }
                }
            }

            // Redaktionelle Sammlungen
            Repeater {
                model: appStore ? appStore.curatedCollections() : []
                delegate: Column {
                    width: parent.width
                    spacing: Theme.s3

                    Column {
                        width: parent.width
                        spacing: 2

                        Text {
                            text: modelData.title
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        Text {
                            text: modelData.description
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.s3

                        readonly property var collectionApps: appStore ? appStore.getCuratedCollection(modelData.id) : []

                        Repeater {
                            model: parent.collectionApps
                            delegate: AppCard {
                                width: Math.max(260, Math.min(320, (parent.width - Theme.s3 * 2) / 3))
                                appKey: modelData.appKey ? modelData.appKey : ""
                                name: modelData.name ? modelData.name : ""
                                summary: modelData.summary ? modelData.summary : ""
                                iconSource: modelData.iconSource ? modelData.iconSource : ""
                                actionState: appStore ? appStore.getActionState(modelData.appKey) : "Available"
                                isInstalled: appStore ? appStore.getInstalledState(modelData.appKey).isFullyInstalled : false
                                origin: modelData.origin ? modelData.origin : ""
                                onClicked: root.appSelected(modelData.appKey)
                            }
                        }
                    }
                }
            }

            // Zustandsanzeige bei leerem Katalog
            EmptyState {
                visible: (appStore ? appStore.isLoaded && appStore.totalAppCount === 0 : false)
                title: qsTr("Keine Store-Metadaten gefunden")
                message: qsTr("AppStream-Metadaten sind auf diesem System noch nicht synchronisiert. System-Updates und die Paketverwaltung funktionieren weiterhin regulär.")
            }
        }
    }
}

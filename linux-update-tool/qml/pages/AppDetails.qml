import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    property string appKey: ""

    signal backRequested()
    signal updateRequested()

    readonly property var appRecord: (appStore && root.appKey.length > 0) ? appStore.getApp(root.appKey) : ({})
    readonly property var candidateOffer: (appStore && root.appKey.length > 0) ? appStore.getCandidateOffer(root.appKey) : ({})
    readonly property var installedState: (appStore && root.appKey.length > 0) ? appStore.getInstalledState(root.appKey) : ({})
    readonly property string actionState: (appStore && root.appKey.length > 0) ? appStore.getActionState(root.appKey) : "Unavailable"

    readonly property var candidatePkg: (candidateOffer && candidateOffer.packages && candidateOffer.packages.length > 0) ? candidateOffer.packages[0] : null
    readonly property var installedPkg: (installedState && installedState.installedPackages && installedState.installedPackages.length > 0) ? installedState.installedPackages[0] : null
    property string launchErrorMessage: ""

    Connections {
        target: appStore
        function onAppLaunchFailed(failedKey, msg) {
            if (failedKey === root.appKey) {
                root.launchErrorMessage = msg
            }
        }
        function onAppLaunched(launchedKey) {
            if (launchedKey === root.appKey) {
                root.launchErrorMessage = ""
            }
        }
    }

    function formatBytes(bytes) {
        if (!bytes || bytes <= 0) return qsTr("Wird im Paketplan ermittelt")
        if (bytes < 1024) return bytes + " B"
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB"
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + " MB"
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB"
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        Column {
            width: Math.min(parent.width - Theme.s6 * 2, 1000)
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: Theme.s5
            bottomPadding: Theme.s6
            spacing: Theme.s5

            // 1. Zurück-Knopf
            PrimaryButton {
                text: qsTr("Zurück")
                iconName: "go-previous"
                variant: "quiet"
                onClicked: root.backRequested()
            }

            // 2. Kopfbereich: Icon, Name, Zusammenfassung, Entwickler & Aktionsleiste
            Card {
                width: parent.width
                implicitHeight: headerContent.implicitHeight + Theme.s5 * 2

                Column {
                    id: headerContent
                    anchors.fill: parent
                    anchors.margins: Theme.s5
                    spacing: Theme.s4

                    Row {
                        width: parent.width
                        spacing: Theme.s4

                        Kirigami.Icon {
                            id: appIcon
                            width: 64
                            height: 64
                            anchors.verticalCenter: parent.verticalCenter
                            source: (root.appRecord && root.appRecord.iconSource) ? root.appRecord.iconSource : "application-x-executable"
                        }

                        Column {
                            width: parent.width - appIcon.width - parent.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3

                            Text {
                                text: (root.appRecord && root.appRecord.name) ? root.appRecord.name : qsTr("Unbekannte Anwendung")
                                textFormat: Text.PlainText
                                font.pixelSize: 22
                                font.weight: Font.Bold
                                color: Theme.text
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            Text {
                                text: (root.appRecord && root.appRecord.summary) ? root.appRecord.summary : ""
                                textFormat: Text.PlainText
                                font.pixelSize: 14
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                maximumLineCount: 2
                                wrapMode: Text.WordWrap
                                width: parent.width
                            }

                            Text {
                                visible: Boolean(root.appRecord && root.appRecord.developer && root.appRecord.developer.length > 0)
                                text: qsTr("Entwickler: %1").arg(root.appRecord ? root.appRecord.developer : "")
                                textFormat: Text.PlainText
                                font.pixelSize: 12
                                color: Theme.textMuted
                            }
                        }
                    }

                    // Trenner
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.separator
                    }

                    // 3. Aktionsleiste (Zustandsmatrix aus Abschnitt 7)
                    Row {
                        width: parent.width
                        spacing: Theme.s3

                        // Hauptaktion
                        PrimaryButton {
                            id: mainActionBtn
                            visible: root.actionState !== "InstalledNoLaunch" && root.actionState !== "Unavailable"
                            text: {
                                switch (root.actionState) {
                                case "Available": return qsTr("Installieren")
                                case "Installed": return qsTr("Öffnen")
                                case "UpdateAvailable": return qsTr("Öffnen")
                                case "PartiallyInstalled": return qsTr("Installation vervollständigen")
                                case "PreparingPlan": return qsTr("Wird vorbereitet …")
                                case "Progressing": return qsTr("Wird verarbeitet …")
                                default: return qsTr("Installieren")
                                }
                            }
                            iconName: {
                                if (root.actionState === "Installed" || root.actionState === "UpdateAvailable") return "system-run"
                                if (root.actionState === "Available" || root.actionState === "PartiallyInstalled") return "list-add"
                                return ""
                            }
                            variant: "primary"
                            enabled: (root.actionState === "Installed" || root.actionState === "UpdateAvailable")
                                     ? true
                                     : (!daemonClient.isBusy && (root.actionState === "Available" || root.actionState === "PartiallyInstalled"))
                            onClicked: {
                                if (root.actionState === "Installed" || root.actionState === "UpdateAvailable") {
                                    appStore.launchApp(root.appKey)
                                } else if (root.actionState === "Available" || root.actionState === "PartiallyInstalled") {
                                    appStore.requestInstall(root.appKey)
                                }
                            }
                        }

                        // Status für nicht startbare installierte Apps (Abschnitt 7)
                        PrimaryButton {
                            visible: root.actionState === "InstalledNoLaunch"
                            text: qsTr("Installiert")
                            variant: "quiet"
                            enabled: false
                        }

                        // Status für nicht verfügbare Apps (Abschnitt 7)
                        PrimaryButton {
                            visible: root.actionState === "Unavailable"
                            text: qsTr("Nicht verfügbar")
                            variant: "quiet"
                            enabled: false
                        }

                        // Sekundäraktion: Entfernen (sofern installiert)
                        PrimaryButton {
                            visible: root.actionState === "Installed" || root.actionState === "InstalledNoLaunch"
                            text: qsTr("Entfernen")
                            iconName: "edit-delete"
                            variant: "destructive"
                            enabled: !daemonClient.isBusy
                            onClicked: appStore.requestRemove(root.appKey)
                        }

                        // Sekundäraktion: System aktualisieren (bei UpdateAvailable)
                        PrimaryButton {
                            visible: root.actionState === "UpdateAvailable"
                            text: qsTr("System aktualisieren")
                            iconName: "system-software-update"
                            variant: "primary"
                            enabled: !daemonClient.isBusy
                            onClicked: root.updateRequested()
                        }

                        // Quell- und Versionshinweis
                        Text {
                            visible: root.candidatePkg !== null
                            text: {
                                if (!root.candidatePkg) return ""
                                return qsTr("Quelle: %1 · Version: %2").arg(root.candidatePkg.repoId).arg(root.candidatePkg.version)
                            }
                            textFormat: Text.PlainText
                            font.pixelSize: 12
                            color: Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Fehlermeldung bei fehlgeschlagenem Start
                    Card {
                        visible: root.launchErrorMessage.length > 0
                        width: parent.width
                        height: 48

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.s3
                            spacing: Theme.s3

                            Kirigami.Icon {
                                source: "dialog-warning"
                                width: 20
                                height: 20
                                color: Theme.negative
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: root.launchErrorMessage
                                font.pixelSize: 13
                                color: Theme.negative
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 60
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }

            // 4. Screenshot-Galerie
            Column {
                width: parent.width
                spacing: Theme.s2
                visible: Boolean(root.appRecord && root.appRecord.screenshots && root.appRecord.screenshots.length > 0)

                Text {
                    text: qsTr("Vorschau")
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                ScrollView {
                    width: parent.width
                    height: 240
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded

                    Row {
                        spacing: Theme.s3

                        Repeater {
                            model: (root.appRecord && root.appRecord.screenshots) ? root.appRecord.screenshots : []
                            delegate: Card {
                                width: 360
                                height: 220
                                clip: true

                                Image {
                                    id: screenshotImg
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    source: modelData
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: screenshotImg.status === Image.Error
                                    color: Theme.surfaceSunken
                                    Column {
                                        anchors.centerIn: parent
                                        spacing: Theme.s2
                                        Kirigami.Icon {
                                            source: "image-missing"
                                            width: 32
                                            height: 32
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                        Text {
                                            text: qsTr("Vorschau nicht verfügbar")
                                            font.pixelSize: 12
                                            color: Theme.textMuted
                                            anchors.horizontalCenter: parent.horizontalCenter
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: screenshotImg.status === Image.Loading
                                    color: Theme.surfaceSunken
                                    opacity: 0.5
                                }
                            }
                        }
                    }
                }
            }

            // Keine Screenshots vorhanden
            Text {
                visible: !Boolean(root.appRecord && root.appRecord.screenshots && root.appRecord.screenshots.length > 0)
                text: qsTr("Keine Screenshots verfügbar")
                font.pixelSize: 13
                color: Theme.textMuted
            }

            // 5. Ausführliche Beschreibung
            Card {
                width: parent.width
                implicitHeight: descCol.implicitHeight + Theme.s5 * 2
                visible: Boolean(root.appRecord && root.appRecord.description && root.appRecord.description.length > 0)

                Column {
                    id: descCol
                    anchors.fill: parent
                    anchors.margins: Theme.s5
                    spacing: Theme.s3

                    Text {
                        text: qsTr("Beschreibung")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Text {
                        text: (root.appRecord && root.appRecord.description) ? root.appRecord.description : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        lineHeight: 1.3
                        color: Theme.text
                        width: parent.width
                    }
                }
            }

            // 6. Technische Details Card
            Card {
                width: parent.width
                implicitHeight: detailsCol.implicitHeight + Theme.s5 * 2

                Column {
                    id: detailsCol
                    anchors.fill: parent
                    anchors.margins: Theme.s5
                    spacing: Theme.s3

                    Text {
                        text: qsTr("Technische Details")
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Grid {
                        width: parent.width
                        columns: 2
                        columnSpacing: Theme.s5
                        rowSpacing: Theme.s2

                        // Paketname
                        Text {
                            text: qsTr("Paketname:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            text: (root.appRecord && root.appRecord.defaultPackageName) ? root.appRecord.defaultPackageName : qsTr("Unbekannt")
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            color: Theme.text
                        }

                        // Version
                        Text {
                            text: qsTr("Version:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            text: root.candidatePkg ? root.candidatePkg.version : (root.installedPkg ? root.installedPkg.version : qsTr("Unbekannt"))
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            color: Theme.text
                        }

                        // Repository
                        Text {
                            text: qsTr("Repository:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            text: root.candidatePkg ? root.candidatePkg.repoId : qsTr("Lokal / Unbekannt")
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.text
                        }

                        // Lizenz
                        Text {
                            text: qsTr("Lizenz:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            text: (root.appRecord && root.appRecord.license) ? root.appRecord.license : qsTr("Unbekannt")
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.text
                        }

                        // Downloadgröße
                        Text {
                            text: qsTr("Downloadgröße:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            text: root.formatBytes(root.candidateOffer ? root.candidateOffer.downloadSize : 0)
                            font.pixelSize: 13
                            color: Theme.text
                        }

                        // Website
                        Text {
                            visible: Boolean(root.appRecord && root.appRecord.urlHomepage && root.appRecord.urlHomepage.length > 0)
                            text: qsTr("Website:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            visible: Boolean(root.appRecord && root.appRecord.urlHomepage && root.appRecord.urlHomepage.length > 0)
                            text: (root.appRecord && root.appRecord.urlHomepage) ? root.appRecord.urlHomepage : ""
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.accent

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally(parent.text)
                            }
                        }

                        // Fehler melden
                        Text {
                            visible: Boolean(root.appRecord && root.appRecord.urlBugtracker && root.appRecord.urlBugtracker.length > 0)
                            text: qsTr("Fehler melden:")
                            font.pixelSize: 13
                            color: Theme.textMuted
                            width: 160
                        }
                        Text {
                            visible: Boolean(root.appRecord && root.appRecord.urlBugtracker && root.appRecord.urlBugtracker.length > 0)
                            text: (root.appRecord && root.appRecord.urlBugtracker) ? root.appRecord.urlBugtracker : ""
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.accent

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: Qt.openUrlExternally(parent.text)
                            }
                        }
                    }
                }
            }
        }
    }
}

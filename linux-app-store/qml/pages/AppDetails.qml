import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    property string appKey: ""

    signal backRequested()
    signal updateRequested()

    readonly property double storeRevision: appStore ? appStore.revision : 0
    readonly property var appRecord: (root.storeRevision >= 0 && appStore && root.appKey.length > 0) ? appStore.getApp(root.appKey) : ({})
    readonly property var candidateOffer: (root.storeRevision >= 0 && appStore && root.appKey.length > 0) ? appStore.getCandidateOffer(root.appKey) : ({})
    readonly property var allOffers: (root.storeRevision >= 0 && appStore && root.appKey.length > 0) ? appStore.getAllOffers(root.appKey) : []
    readonly property var installedState: (root.storeRevision >= 0 && appStore && root.appKey.length > 0) ? appStore.getInstalledState(root.appKey) : ({})

    property int selectedOfferIndex: 0
    property bool lightboxOpen: false
    property int lightboxIndex: 0
    readonly property var screenshotList: (root.appRecord && root.appRecord.screenshots) ? root.appRecord.screenshots : []

    function openLightbox(index) {
        if (screenshotList && screenshotList.length > 0) {
            lightboxIndex = Math.max(0, Math.min(index, screenshotList.length - 1))
            lightboxOpen = true
        }
    }

    function closeLightbox() {
        lightboxOpen = false
    }

    function prevScreenshot() {
        if (screenshotList && screenshotList.length > 0) {
            lightboxIndex = (lightboxIndex - 1 + screenshotList.length) % screenshotList.length
        }
    }

    function nextScreenshot() {
        if (screenshotList && screenshotList.length > 0) {
            lightboxIndex = (lightboxIndex + 1) % screenshotList.length
        }
    }

    onAllOffersChanged: {
        root.selectedOfferIndex = defaultOfferIndex()
    }
    onAppKeyChanged: {
        root.selectedOfferIndex = defaultOfferIndex()
    }

    function defaultOfferIndex() {
        if (!allOffers || allOffers.length === 0) return 0
        if (!candidateOffer || !candidateOffer.packages || candidateOffer.packages.length === 0) return 0
        var candPkg = candidateOffer.packages[0]
        for (var i = 0; i < allOffers.length; ++i) {
            var o = allOffers[i]
            if (o.backend === candPkg.backend && o.name === candPkg.name && (o.repoId === candPkg.repoId || !candPkg.repoId)) {
                return i
            }
        }
        return 0
    }

    readonly property var currentOffer: (allOffers && selectedOfferIndex >= 0 && selectedOfferIndex < allOffers.length) ? allOffers[selectedOfferIndex] : (candidateOffer || {})
    readonly property var currentPkg: (currentOffer && currentOffer.packages && currentOffer.packages.length > 0) ? currentOffer.packages[0] : null
    readonly property string currentSource: currentPkg ? (currentPkg.backend || "native") : ""

    readonly property string actionState: (root.storeRevision >= 0 && appStore && root.appKey.length > 0) ? appStore.getActionState(root.appKey, root.currentSource) : "Unavailable"

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

            // Zurück führt der Pfeil oben links im Fenster.

            // 2. Kopfbereich: Icon, Name, Zusammenfassung, Entwickler & Aktionsleiste
            Card {
                width: parent.width
                implicitHeight: headerContent.implicitHeight
                // Frei stehend wie im App Center, ohne Kartenrahmen
                color: "transparent"
                border.width: 0

                Column {
                    id: headerContent
                    anchors.fill: parent
                    anchors.margins: 0
                    spacing: Theme.s4

                    Row {
                        width: parent.width
                        spacing: Theme.s4

                        Kirigami.Icon {
                            id: appIcon
                            width: 64
                            height: 64
                            anchors.verticalCenter: parent.verticalCenter
                            source: { mediaCache.revision; return mediaCache.source(root.appRecord.iconSource || "application-x-executable") || "application-x-executable" }
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
                            objectName: "storeInstallButton"
                            readonly property bool launches: root.actionState === "Installed"
                                                            || root.actionState === "InstalledOtherSource"
                                                            || root.actionState === "UpdateAvailable"
                                                            || root.actionState === "MissingSource"
                            readonly property bool installs: root.actionState === "Available"
                                                             || root.actionState === "PartiallyInstalled"
                                                             || (root.actionState === "ErrorOrCancelled" && !root.installedState.isFullyInstalled)
                            visible: root.actionState !== "InstalledNoLaunch" && root.actionState !== "Unavailable"
                            text: {
                                switch (root.actionState) {
                                case "Available": return qsTr("Installieren")
                                case "Installed": return qsTr("Öffnen")
                                case "InstalledOtherSource": return qsTr("Öffnen")
                                case "UpdateAvailable": return qsTr("Öffnen")
                                case "MissingSource": return qsTr("Öffnen")
                                case "PartiallyInstalled": return qsTr("Installation vervollständigen")
                                case "PreparingPlan": return qsTr("Wird vorbereitet …")
                                case "Progressing": return qsTr("Wird verarbeitet …")
                                case "AwaitingConfirmation": return qsTr("Bestätigung ausstehend")
                                case "OtherTransactionRunning": return qsTr("Andere Paketaktion läuft")
                                case "Reconciling": return qsTr("Installationsstatus wird geprüft …")
                                case "ActionUnsupported": return qsTr("Aktion nicht unterstützt")
                                case "ErrorOrCancelled": return qsTr("Erneut versuchen")
                                default: return qsTr("Installieren")
                                }
                            }
                            iconName: {
                                if (launches) return "system-run"
                                if (installs) return "list-add"
                                return ""
                            }
                            variant: "primary"
                            enabled: launches ? true : (!appStore.isLoading && !daemonClient.isBusy && !daemonClient.hasPlan && daemonClient.installSupported && installs)
                            onClicked: {
                                if (launches) {
                                    appStore.launchApp(root.appKey, root.currentSource)
                                } else if (installs) {
                                    appStore.requestInstall(root.appKey, root.selectedOfferIndex)
                                }
                            }
                        }

                        // Parallele Installation aus anderer Quelle (Abschnitt 8.1)
                        PrimaryButton {
                            id: parallelInstallBtn
                            objectName: "storeParallelInstallButton"
                            visible: root.actionState === "InstalledOtherSource"
                            text: qsTr("Parallel installieren")
                            iconName: "list-add"
                            variant: "secondary"
                            enabled: !appStore.isLoading && !daemonClient.isBusy && !daemonClient.hasPlan && daemonClient.installSupported
                            onClicked: appStore.requestInstall(root.appKey, root.selectedOfferIndex)
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

                        // Sekundäraktion: Entfernen (sofern die ausgewählte Quelle installiert ist)
                        PrimaryButton {
                            visible: (root.actionState === "Installed" || root.actionState === "InstalledNoLaunch")
                                     || Boolean(root.currentOffer && root.currentOffer.isInstalled)
                            objectName: "storeRemoveButton"
                            text: qsTr("Entfernen")
                            iconName: "edit-delete"
                            variant: "destructive"
                            enabled: !appStore.isLoading && !daemonClient.isBusy && !daemonClient.hasPlan && daemonClient.removeSupported
                            onClicked: appStore.requestRemove(root.appKey, root.currentSource)
                        }

                        // Sekundäraktion: System aktualisieren (bei UpdateAvailable)
                        PrimaryButton {
                            visible: root.actionState === "UpdateAvailable"
                            text: qsTr("System aktualisieren")
                            iconName: "system-software-update"
                            variant: "primary"
                            enabled: !appStore.isLoading && !daemonClient.isBusy && !daemonClient.hasPlan
                            onClicked: root.updateRequested()
                        }

                        // Hinweis, wenn die App installiert ist, aber keine Quelle mehr anbietet
                        Text {
                            visible: root.actionState === "MissingSource"
                            text: qsTr("Installiert, in den aktuellen Paketquellen nicht verfügbar")
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.textMuted
                            wrapMode: Text.WordWrap
                        }

                        // Quellen-Auswahlfeld wenn >= 2 Angebote vorhanden (Abschnitt 3.2)
                        ComboBox {
                            id: sourceCombo
                            objectName: "sourceComboBox"
                            visible: root.allOffers.length >= 2
                            model: root.allOffers
                            textRole: "displayText"
                            currentIndex: root.selectedOfferIndex
                            anchors.verticalCenter: parent.verticalCenter
                            onActivated: function(index) {
                                root.selectedOfferIndex = index
                            }
                            delegate: ItemDelegate {
                                width: sourceCombo.width
                                contentItem: Text {
                                    text: modelData.displayText
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                highlighted: sourceCombo.highlightedIndex === index
                            }
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

            // 3. Kennzahlen direkt unter den Knöpfen, wie im Ubuntu App Center
            Column {
                width: parent.width
                spacing: Theme.s3

                Rectangle { width: parent.width; height: 1; color: Theme.separator }

                Flow {
                    width: parent.width
                    spacing: Theme.s6

                    Repeater {
                        model: [
                            { label: qsTr("Download"), value: (root.currentOffer && root.currentOffer.downloadSize > 0) ? root.formatBytes(root.currentOffer.downloadSize) : "\u2014" },
                            { label: qsTr("Lizenz"), value: (root.appRecord && root.appRecord.license) ? root.appRecord.license : qsTr("unbekannt") },
                            { label: qsTr("Quelle"), value: root.currentPkg ? (root.currentPkg.repoId || root.currentPkg.backend) : qsTr("lokal") },
                            { label: qsTr("Version"), value: root.currentPkg ? root.currentPkg.version : (root.installedPkg ? root.installedPkg.version : "\u2014") }
                        ]
                        delegate: Column {
                            spacing: 2
                            Text {
                                text: modelData.label
                                font.pixelSize: 11
                                color: Theme.textMuted
                            }
                            Text {
                                text: modelData.value
                                textFormat: Text.PlainText
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.text
                                elide: Text.ElideRight
                                width: Math.min(implicitWidth, 260)
                            }
                        }
                    }

                    Column {
                        spacing: 2
                        visible: Boolean(root.appRecord && ((root.appRecord.urlHomepage && root.appRecord.urlHomepage.length > 0)
                                                            || (root.appRecord.urlBugtracker && root.appRecord.urlBugtracker.length > 0)))
                        Text {
                            text: qsTr("Links")
                            font.pixelSize: 11
                            color: Theme.textMuted
                        }
                        Row {
                            spacing: Theme.s3
                            Text {
                                visible: Boolean(root.appRecord && root.appRecord.urlHomepage && root.appRecord.urlHomepage.length > 0)
                                text: qsTr("Website")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.accent
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(root.appRecord.urlHomepage)
                                }
                            }
                            Text {
                                visible: Boolean(root.appRecord && root.appRecord.urlBugtracker && root.appRecord.urlBugtracker.length > 0)
                                text: qsTr("Fehler melden")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: Theme.accent
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(root.appRecord.urlBugtracker)
                                }
                            }
                        }
                    }
                }

                Rectangle { width: parent.width; height: 1; color: Theme.separator }
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
                                id: thumbCard
                                width: 360
                                height: 220
                                clip: true
                                border.color: thumbMouseArea.containsMouse ? Theme.accent : Theme.separator
                                border.width: thumbMouseArea.containsMouse ? 2 : 1

                                Image {
                                    id: screenshotImg
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    source: { mediaCache.revision; return mediaCache.source(modelData) }
                                    fillMode: Image.PreserveAspectFit
                                    asynchronous: true
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: screenshotImg.status === Image.Error || screenshotImg.source.toString().length === 0
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

                                // Hover zoom icon indicator
                                Rectangle {
                                    width: 32
                                    height: 32
                                    radius: 16
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.s2
                                    color: Qt.rgba(Theme.surfaceRaised.r, Theme.surfaceRaised.g, Theme.surfaceRaised.b, 0.85)
                                    visible: thumbMouseArea.containsMouse && screenshotImg.status === Image.Ready
                                    Kirigami.Icon {
                                        anchors.centerIn: parent
                                        source: "document-preview"
                                        width: 18
                                        height: 18
                                    }
                                }

                                MouseArea {
                                    id: thumbMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: (screenshotImg.status === Image.Ready) ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onClicked: {
                                        if (screenshotImg.status === Image.Ready) {
                                            root.openLightbox(index)
                                        }
                                    }
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
                            text: root.currentPkg ? root.currentPkg.version : (root.installedPkg ? root.installedPkg.version : qsTr("Unbekannt"))
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
                            text: root.currentPkg ? (root.currentPkg.repoId || root.currentPkg.backend) : qsTr("Lokal / Unbekannt")
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
                            text: root.formatBytes(root.currentOffer ? root.currentOffer.downloadSize : 0)
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

    // 6. Screenshot Lightbox Modal Overlay
    Item {
        id: lightboxModal
        anchors.fill: parent
        z: 500
        visible: root.lightboxOpen

        // Backdrop: semi-transparent darkened background
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.90)

            MouseArea {
                anchors.fill: parent
                onClicked: root.closeLightbox()
            }
        }

        // Keyboard shortcuts for Lightbox navigation
        Shortcut {
            enabled: root.lightboxOpen
            sequence: "Left"
            onActivated: root.prevScreenshot()
        }
        Shortcut {
            enabled: root.lightboxOpen
            sequence: "Right"
            onActivated: root.nextScreenshot()
        }

        // Main Image Area
        Item {
            anchors.fill: parent
            anchors.margins: Theme.s6

            Image {
                id: lightboxImage
                anchors.fill: parent
                anchors.margins: 48
                source: {
                    mediaCache.revision
                    if (root.screenshotList && root.screenshotList.length > root.lightboxIndex && root.lightboxIndex >= 0) {
                        return mediaCache.source(root.screenshotList[root.lightboxIndex])
                    }
                    return ""
                }
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true

                BusyIndicator {
                    anchors.centerIn: parent
                    running: lightboxImage.status === Image.Loading
                    visible: running
                    implicitWidth: 48
                    implicitHeight: 48
                }
            }

            // Left Navigation Button (<)
            RoundButton {
                id: prevButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                visible: root.screenshotList.length > 1
                icon.name: "go-previous"
                Accessible.name: qsTr("Vorheriges Bild")
                palette.button: Theme.surfaceRaised
                palette.buttonText: Theme.text
                onClicked: root.prevScreenshot()
            }

            // Right Navigation Button (>)
            RoundButton {
                id: nextButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 48
                height: 48
                visible: root.screenshotList.length > 1
                icon.name: "go-next"
                Accessible.name: qsTr("Nächstes Bild")
                palette.button: Theme.surfaceRaised
                palette.buttonText: Theme.text
                onClicked: root.nextScreenshot()
            }

            // Top Bar: Counter & Close button
            Row {
                anchors.top: parent.top
                anchors.right: parent.right
                spacing: Theme.s3

                // Counter chip
                Rectangle {
                    width: counterText.implicitWidth + Theme.s4 * 2
                    height: 36
                    radius: 18
                    color: Theme.surfaceRaised
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.screenshotList.length > 1

                    Text {
                        id: counterText
                        anchors.centerIn: parent
                        text: qsTr("%1 / %2").arg(root.lightboxIndex + 1).arg(root.screenshotList.length)
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                }

                // Close Button (X)
                RoundButton {
                    width: 36
                    height: 36
                    icon.name: "dialog-close"
                    Accessible.name: qsTr("Schließen")
                    palette.button: Theme.surfaceRaised
                    palette.buttonText: Theme.text
                    onClicked: root.closeLightbox()
                }
            }
        }
    }
}

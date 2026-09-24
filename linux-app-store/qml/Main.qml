import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "components"
import "pages"

ApplicationWindow {
    id: root
    visible: !(typeof startInTray !== "undefined" && startInTray && typeof trayManager !== "undefined" && trayManager && trayManager.isTrayAvailable)
    width: 1180
    height: 800
    minimumWidth: 900
    minimumHeight: 640
    title: qsTr("Linux Universal App Center")
    color: Theme.bg

    Behavior on color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: 200 * Theme.motionScale }
    }

    // Seitenindizes: 0 Entdecken, 1 Ergebnisse/Kategorie, 2 Verwalten,
    // 3 Verlauf, 4 Protokoll, 5 Einstellungen
    property int currentNavIndex: (typeof initialNavIndex !== "undefined") ? initialNavIndex : 0
    // 0 Aktualisierungen, 1 Apps, 2 Alle Pakete & Pflege
    property int manageTab: (typeof initialManageTab !== "undefined") ? initialManageTab : 0
    property int settingsTab: (typeof initialSettingsTab !== "undefined") ? initialSettingsTab : 0
    property string browseCollectionTitle: ""
    property var planPreviewItem: null

    onClosing: function(close) {
        if (typeof trayManager !== "undefined" && trayManager && trayManager.isTrayAvailable) {
            close.accepted = false
            root.hide()
        }
    }

    function componentFor(index) {
        switch (index) {
        case 1: return searchPage
        case 2: return managePage
        case 3: return historyPage
        case 4: return logsPage
        case 5: return settingsPage
        default: return discoverPage
        }
    }

    // Erst zurück zur Wurzel, dann die Seite wechseln. Früher wurde der Index
    // gesetzt, solange noch eine Detailseite offen war – der Wechsel griff dann
    // nicht, und man landete wieder auf der alten Seite.
    function navigate(index) {
        if (pageStack.depth > 1) pageStack.pop(null)
        if (index === currentNavIndex) return
        currentNavIndex = index
        pageStack.replace(componentFor(index))
    }

    function openCategory(category) {
        searchField.text = ""
        storeModel.search("")
        storeModel.collection = ""
        storeModel.category = category
        navigate(1)
    }

    function openCollection(collectionId, title) {
        searchField.text = ""
        storeModel.search("")
        storeModel.category = ""
        browseCollectionTitle = title
        storeModel.collection = collectionId
        navigate(1)
    }

    function openManage(tab) {
        manageTab = tab
        navigate(2)
    }

    function openSettings(tab) {
        settingsTab = tab
        navigate(5)
    }

    function goBack() {
        if (pageStack.depth <= 1) return
        // Die Planvorschau verwirft beim Verlassen ihren Plan, sonst bliebe er im Daemon stehen.
        if (planPreviewItem && pageStack.currentItem === planPreviewItem) {
            daemonClient.discardStorePlan()
            planPreviewItem = null
        }
        pageStack.pop()
    }

    Connections {
        target: typeof trayManager !== "undefined" ? trayManager : null
        function onOpenUpdatesRequested() {
            root.openManage(0)
            root.show()
            root.raise()
            root.requestActivate()
        }
    }

    Shortcut {
        sequences: [ StandardKey.Quit ]
        onActivated: {
            if (typeof trayManager !== "undefined" && trayManager) {
                trayManager.quitApplication()
            } else {
                Qt.quit()
            }
        }
    }

    Shortcut {
        sequences: [ StandardKey.Find ]
        onActivated: {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            // Das Fortschrittsfenster schließt Escape nur: ein laufender Vorgang geht weiter.
            if (progressPopup.opened) {
                if (typeof operationMonitor !== "undefined" && operationMonitor.running) progressPopup.close()
                else root.finishOperation()
                return
            }
            if (pageStack.currentItem && pageStack.currentItem.lightboxOpen) {
                pageStack.currentItem.closeLightbox()
                return
            }
            root.goBack()
        }
    }

    Shortcut {
        sequences: [ StandardKey.Back, "Alt+Left" ]
        onActivated: root.goBack()
    }

    // Ein aufgelöster Store-Plan wird zur Bestätigung vorgelegt und nicht
    // im Hintergrund ausgeführt (Zustandsmatrix Abschnitt 7).
    Connections {
        target: daemonClient
        function onPlanReady() {
            if (daemonClient.isStorePlan && pageStack.currentItem !== planPreviewItem) {
                planPreviewItem = pageStack.push(planPreviewPage)
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Seitenleiste wie im Ubuntu App Center
        NavRail {
            id: rail
            Layout.fillHeight: true
            currentIndex: pageStack.depth > 1 ? -1 : root.currentNavIndex
            // Kategorie nur hervorheben, wenn wirklich die Kategorie gezeigt wird
            currentCategory: (storeModel.searchQuery.length === 0 && storeModel.collection.length === 0)
                             ? storeModel.category : ""
            onPageSelected: function(index) {
                if (index === 2) root.openManage(root.manageTab)
                else root.navigate(index)
            }
            onCategorySelected: function(category) { root.openCategory(category) }
        }

        // Haupt-Inhaltsbereich
        ColumnLayout {
            id: contentArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Kopfzeile: Zurück-Pfeil links, Suchfeld mittig wie im App Center
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 56

                ToolButton {
                    id: backButton
                    objectName: "headerBackButton"
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.s4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: pageStack.depth > 1
                    icon.name: "go-previous"
                    Accessible.name: qsTr("Zurück")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Zurück")
                    onClicked: root.goBack()
                }

                TextField {
                    id: searchField
                    objectName: "globalSearchField"
                    anchors.centerIn: parent
                    width: Math.min(460, parent.width - 200)
                    placeholderText: qsTr("Apps suchen")
                    font.pixelSize: 13
                    color: Theme.text
                    leftPadding: 34
                    rightPadding: clearIcon.visible ? 30 : 12
                    Accessible.name: qsTr("Apps suchen")

                    background: Rectangle {
                        radius: Theme.radiusControl
                        color: Theme.surfaceSunken
                        border.color: searchField.activeFocus ? Theme.accent : Theme.separator
                        border.width: searchField.activeFocus ? 2 : 1
                    }

                    Kirigami.Icon {
                        source: "edit-find"
                        width: 16
                        height: 16
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        color: Theme.textMuted
                    }

                    Kirigami.Icon {
                        id: clearIcon
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

                    // Die Suche gilt immer dem ganzen Katalog, nicht der gerade
                    // gewählten Kategorie – wie im App Center.
                    onTextChanged: {
                        if (text.length > 0) {
                            storeModel.category = ""
                            storeModel.collection = ""
                            if (root.currentNavIndex !== 1 || pageStack.depth > 1) root.navigate(1)
                        }
                        storeModel.search(text)
                    }

                    Keys.onEscapePressed: function(event) {
                        if (text.length > 0) {
                            text = ""
                            event.accepted = true
                        } else {
                            event.accepted = false
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                StackView {
                    id: pageStack
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: statusBar.visible ? statusBar.top : parent.bottom
                    initialItem: discoverPage

                    pushEnter: Transition {
                        PropertyAnimation {
                            property: "opacity"
                            from: 0
                            to: 1
                            duration: 250 * Theme.motionScale
                            easing.type: Easing.OutQuint
                        }
                        PropertyAnimation {
                            property: "x"
                            from: 24
                            to: 0
                            duration: 250 * Theme.motionScale
                            easing.type: Easing.OutQuint
                        }
                    }
                    pushExit: Transition {
                        PropertyAnimation {
                            property: "opacity"
                            from: 1
                            to: 0
                            duration: 200 * Theme.motionScale
                        }
                    }
                }

                // Dauerhafte kompakte Statusleiste für aktive Transaktionen (Abschnitt 3.2 & 3.3)
                Rectangle {
                    id: statusBar
                    visible: daemonClient.isBusy || daemonClient.hasPlan
                             || (typeof operationMonitor !== "undefined" && operationMonitor.active)
                    // Die automatische Prüfung hinterlässt im Daemon einen fertigen,
                    // aber unbestätigten Systemplan. Das ist KEINE laufende Transaktion:
                    // früher stand hier „Transaktion aktiv …", und „Anzeigen" öffnete ein
                    // leeres Fortschrittsfenster, das bei „Aktualisierungsplan bereit"
                    // scheinbar hing.
                    readonly property bool monitorActive: typeof operationMonitor !== "undefined" && operationMonitor.active
                    readonly property bool systemPlanWaiting: !monitorActive && !daemonClient.isBusy
                                                              && daemonClient.hasPlan && !daemonClient.isStorePlan
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 48
                    color: Theme.surfaceRaised
                    border.color: Theme.separator
                    border.width: 1

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.s5
                        anchors.rightMargin: Theme.s5
                        spacing: Theme.s4

                        Kirigami.Icon {
                            source: daemonClient.isBusy || statusBar.systemPlanWaiting ? "system-software-update" : "dialog-information"
                            width: 22
                            height: 22
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: {
                                if (typeof operationMonitor !== "undefined" && operationMonitor.active) {
                                    if (!operationMonitor.running) return operationMonitor.resultMessage.length > 0 ? operationMonitor.resultMessage : operationMonitor.title
                                    if (operationMonitor.awaitingReview) return qsTr("%1 · Änderungen prüfen").arg(operationMonitor.title)
                                    if (operationMonitor.indeterminate) return operationMonitor.title
                                    return qsTr("%1 · %2 %").arg(operationMonitor.title).arg(Math.round(operationMonitor.progress * 100))
                                }
                                if (statusBar.systemPlanWaiting) {
                                    var n = updatesModel.totalCount
                                    return n === 1 ? qsTr("1 Systemaktualisierung bereit – noch nicht gestartet")
                                                   : qsTr("%1 Systemaktualisierungen bereit – noch nicht gestartet").arg(n)
                                }
                                return daemonClient.statusMessage.length > 0 ? daemonClient.statusMessage : qsTr("Transaktion aktiv …")
                            }
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            color: Theme.text
                            elide: Text.ElideRight
                            width: parent.width - 22 - statusBarAction.width - parent.spacing * 2
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        PrimaryButton {
                            id: statusBarAction
                            objectName: "statusBarActionButton"
                            text: statusBar.systemPlanWaiting ? qsTr("Jetzt aktualisieren") : qsTr("Anzeigen")
                            variant: "primary"
                            anchors.verticalCenter: parent.verticalCenter
                            onClicked: {
                                // Wartender Systemplan: derselbe Start wie „System aktualisieren"
                                // in Verwalten. Store-Plan: Vorschau. Laufender Vorgang:
                                // Fortschritt. Sonst (Daemon plant noch): die Liste in Verwalten
                                // statt eines leeren Fortschrittsfensters.
                                if (statusBar.systemPlanWaiting) daemonClient.startUpgrade()
                                else if (daemonClient.isStorePlan) planPreviewItem = pageStack.push(planPreviewPage)
                                else if (statusBar.monitorActive) progressPopup.open()
                                else root.openManage(0)
                            }
                        }
                    }
                }
            }
        }
    }

    // Seiten-Komponenten
    Component {
        id: discoverPage
        Discover {
            onAppSelected: function(appKey) {
                pageStack.push(appDetailsPage, { appKey: appKey })
            }
            onSearchRequested: {
                searchField.forceActiveFocus()
            }
            onCategorySelected: function(category) {
                root.openCategory(category)
            }
            onCollectionSelected: function(collectionId, title) {
                root.openCollection(collectionId, title)
            }
        }
    }

    Component {
        id: searchPage
        Search {
            collectionTitle: root.browseCollectionTitle
            onAppSelected: function(appKey) {
                pageStack.push(appDetailsPage, { appKey: appKey })
            }
        }
    }

    Component {
        id: appDetailsPage
        AppDetails {
            onBackRequested: root.goBack()
            onUpdateRequested: root.openManage(0)
        }
    }

    Component {
        id: managePage
        Manage {
            tab: root.manageTab
            onTabSelected: function(tab) { root.manageTab = tab }
            onAppSelected: function(appKey) {
                pageStack.push(appDetailsPage, { appKey: appKey })
            }
            onOpenSettingsTabRequested: function(t) { root.openSettings(t) }
            onStartUpgradeRequested: daemonClient.startUpgrade()
            onRefreshRequested: daemonClient.refreshUpdates()
        }
    }

    Component {
        id: historyPage
        History {}
    }

    Component {
        id: logsPage
        Logs {}
    }

    Component {
        id: settingsPage
        Settings {
            selectedTab: root.settingsTab
        }
    }

    Component {
        id: planPreviewPage
        PlanPreview {
            onConfirmed: {
                // Bestätigt wird genau die angezeigte Planrevision.
                // Das Fortschrittsfenster öffnet sich, sobald der Daemon den
                // Commit angenommen hat; bei Ablehnung bleibt die Vorschau.
                daemonClient.commitStorePlan(daemonClient.planModel.planRevision)
            }
            onDiscarded: {
                daemonClient.discardStorePlan()
                planPreviewItem = null
                pageStack.pop()
            }
        }
    }

    Component {
        id: reportPage
        Report {
            rebootRequired: progressModel.hasKernelUpdate
            onCloseReportRequested: {
                pageStack.pop(null) // Bis zum Root zurück
            }
        }
    }

    Component.onCompleted: {
        if (currentNavIndex !== 0) pageStack.replace(componentFor(currentNavIndex))
        if (typeof initialAppKey !== "undefined" && initialAppKey.length > 0) {
            pageStack.push(appDetailsPage, { appKey: initialAppKey })
        }
    }

    // Gemeinsames Fortschrittsfenster für System, Store, Flatpak, Snap und AUR.
    // Früher belegte eine Transaktion das ganze Hauptfenster; jetzt bleibt das
    // Fenster bedienbar und der Vorgang kann im Hintergrund weiterlaufen.
    Popup {
        id: progressPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 780)
        // Prüfansicht braucht Platz, sonst so hoch wie der Inhalt.
        height: Math.min(root.height - 80, operationView.reviewing ? 660 : operationView.implicitHeight)
        modal: true
        dim: true
        closePolicy: Popup.NoAutoClose
        padding: 0

        background: Rectangle {
            color: Theme.surfaceRaised
            radius: Theme.radiusCard
            border.color: Theme.separator
            border.width: 1
        }

        contentItem: OperationProgressView {
            id: operationView
            onBackgroundRequested: progressPopup.close()
            onDoneRequested: root.finishOperation()
        }
    }

    function finishOperation() {
        var source = (typeof operationMonitor !== "undefined") ? operationMonitor.source : ""
        var succeeded = (typeof operationMonitor !== "undefined") && operationMonitor.result === 1
        if (typeof operationMonitor !== "undefined") operationMonitor.acknowledge()
        progressPopup.close()
        // Nach einem Kernel-Update weiterhin der Hinweis auf den Neustart.
        if (succeeded && (source === "system" || source === "store") && progressModel.hasKernelUpdate) {
            pageStack.push(reportPage)
        }
    }

    Connections {
        target: typeof operationMonitor !== "undefined" ? operationMonitor : null
        function onStarted() {
            progressPopup.open()
        }
    }

    Connections {
        target: daemonClient
        function onTransactionStarted() {
            // Der Daemon hat den Commit angenommen: die Planvorschau ist erledigt.
            if (planPreviewItem && pageStack.currentItem === planPreviewItem) {
                planPreviewItem = null
                pageStack.pop()
            }
        }
    }
}

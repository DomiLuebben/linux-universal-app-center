import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "components"
import "pages"

ApplicationWindow {
    id: root
    visible: true
    width: 1180
    height: 800
    minimumWidth: 900
    minimumHeight: 640
    title: qsTr("Linux Update Tool")
    color: Theme.bg

    Behavior on color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: 200 * Theme.motionScale }
    }

    // Tastenkürzel laut Spezifikation 3.5
    Shortcut {
        sequences: [ StandardKey.Find ]
        onActivated: {
            currentNavIndex = 1
            if (pageStack.depth > 1) {
                pageStack.pop(null)
            }
            if (searchPageComponent.status === Component.Ready && pageStack.currentItem && pageStack.currentItem.focusSearch) {
                pageStack.currentItem.focusSearch()
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: {
            if (pageStack.depth > 1) {
                pageStack.pop()
            }
        }
    }

    Shortcut {
        sequences: [ StandardKey.Back, "Alt+Left" ]
        onActivated: {
            if (pageStack.depth > 1) {
                pageStack.pop()
            }
        }
    }

    Row {
        anchors.fill: parent

        // NavRail links
        NavRail {
            id: rail
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            currentIndex: pageStack.depth > 1 ? -1 : currentNavIndex
            onPageSelected: function(index) {
                currentNavIndex = index
                if (pageStack.depth > 1) {
                    pageStack.pop(null)
                }
            }
        }

        // Haupt-Inhaltsbereich
        Item {
            id: contentArea
            width: root.width - rail.width
            height: root.height

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
                        source: daemonClient.isBusy ? "system-software-update" : "dialog-information"
                        width: 22
                        height: 22
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: daemonClient.statusMessage.length > 0 ? daemonClient.statusMessage : qsTr("Transaktion aktiv …")
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: Theme.text
                        elide: Text.ElideRight
                        width: parent.width - 160
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    PrimaryButton {
                        text: qsTr("Anzeigen")
                        variant: "primary"
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: {
                            if (pageStack.currentItem !== transactionPage) {
                                pageStack.push(transactionPage)
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
                        currentNavIndex = 1
                    }
                    onCategorySelected: function(category) {
                        currentNavIndex = 1
                        storeModel.category = category
                    }
                }
            }

            Component {
                id: searchPage
                Search {
                    id: searchPageComponent
                    onAppSelected: function(appKey) {
                        pageStack.push(appDetailsPage, { appKey: appKey })
                    }
                }
            }

            Component {
                id: appDetailsPage
                AppDetails {
                    onBackRequested: {
                        pageStack.pop()
                    }
                    onUpdateRequested: {
                        currentNavIndex = 3
                        pageStack.pop(null)
                    }
                }
            }

            Component {
                id: updatesPage
                Updates {
                    onStartUpgradeRequested: {
                        daemonClient.startUpgrade()
                    }
                    onRefreshRequested: {
                        daemonClient.refreshUpdates()
                    }
                }
            }

            Component {
                id: installedPage
                Installed {
                    onAppSelected: function(appKey) {
                        pageStack.push(appDetailsPage, { appKey: appKey })
                    }
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
                Settings {}
            }

            Component {
                id: transactionPage
                Transaction {
                    onCancelRequested: {
                        daemonClient.cancelTransaction()
                    }
                    onFinishAcknowledged: {
                        if (progressModel.hasKernelUpdate) {
                            pageStack.push(reportPage)
                        } else {
                            pageStack.pop()
                        }
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
        }
    }

    property int currentNavIndex: (typeof initialNavIndex !== "undefined") ? initialNavIndex : 0

    Component.onCompleted: {
        if (currentNavIndex !== 0) {
            switch (currentNavIndex) {
                case 1: pageStack.replace(searchPage); break;
                case 2: pageStack.replace(installedPage); break;
                case 3: pageStack.replace(updatesPage); break;
                case 4: pageStack.replace(historyPage); break;
                case 5: pageStack.replace(logsPage); break;
                case 6: pageStack.replace(settingsPage); break;
            }
        }
    }

    onCurrentNavIndexChanged: {
        if (pageStack.depth === 1) {
            switch (currentNavIndex) {
                case 0: pageStack.replace(discoverPage); break;
                case 1: pageStack.replace(searchPage); break;
                case 2: pageStack.replace(installedPage); break;
                case 3: pageStack.replace(updatesPage); break;
                case 4: pageStack.replace(historyPage); break;
                case 5: pageStack.replace(logsPage); break;
                case 6: pageStack.replace(settingsPage); break;
            }
        }
    }

    Connections {
        target: daemonClient
        function onTransactionStarted() {
            if (pageStack.currentItem !== transactionPage) {
                pageStack.push(transactionPage)
            }
        }
    }
}

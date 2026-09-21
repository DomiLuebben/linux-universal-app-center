import QtQuick
import QtQuick.Controls
import "components"
import "pages"

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 760
    minimumWidth: 900
    minimumHeight: 640
    title: qsTr("Linux Update Tool")
    color: Theme.bg

    Behavior on color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: 200 * Theme.motionScale }
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
                currentNavIndex = index;
                if (pageStack.depth > 1) {
                    pageStack.pop();
                }
            }
        }

        // Haupt-Inhaltsbereich
        Item {
            width: root.width - rail.width
            height: root.height

            StackView {
                id: pageStack
                anchors.fill: parent
                initialItem: updatesPage

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

            // Seiten-Komponenten
            Component {
                id: updatesPage
                Updates {
                    onStartUpgradeRequested: {
                        daemonClient.startUpgrade();
                        pageStack.push(transactionPage);
                    }
                    onRefreshRequested: {
                        daemonClient.refreshUpdates();
                    }
                }
            }

            Component {
                id: installedPage
                Installed {}
            }

            Component {
                id: historyPage
                History {}
            }

            Component {
                id: settingsPage
                Settings {}
            }

            Component {
                id: transactionPage
                Transaction {
                    onCancelRequested: {
                        daemonClient.cancelTransaction();
                    }
                    onFinishAcknowledged: {
                        if (progressModel.hasKernelUpdate) {
                            pageStack.push(reportPage);
                        } else {
                            pageStack.pop();
                        }
                    }
                }
            }

            Component {
                id: reportPage
                Report {
                    rebootRequired: progressModel.hasKernelUpdate
                    onCloseReportRequested: {
                        pageStack.pop(null); // Bis zum Root zurück
                    }
                }
            }
        }
    }

    property int currentNavIndex: 0
    onCurrentNavIndexChanged: {
        if (pageStack.depth === 1) {
            switch (currentNavIndex) {
                case 0: pageStack.replace(updatesPage); break;
                case 1: pageStack.replace(installedPage); break;
                case 2: pageStack.replace(historyPage); break;
                case 3: pageStack.replace(installedPage); break;
                case 4: pageStack.replace(settingsPage); break;
            }
        }
    }

    Connections {
        target: daemonClient
        function onTransactionStarted() {
            if (pageStack.currentItem !== transactionPage) {
                pageStack.push(transactionPage);
            }
        }
    }
}

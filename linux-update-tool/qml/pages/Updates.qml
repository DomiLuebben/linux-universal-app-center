import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root
    signal startUpgradeRequested()
    signal refreshRequested()

    // Hero-Karte oben
    Card {
        id: heroCard
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s6
        height: 140

        Row {
            anchors.fill: parent
            anchors.margins: Theme.s5
            spacing: Theme.s5

            Text {
                text: updatesModel.totalCount
                font.pixelSize: 42
                font.weight: Font.Bold
                font.features: ({ "tnum": 1 })
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2
                width: parent.width - 320

                Text {
                    text: updatesModel.totalCount === 1 ? qsTr("Aktualisierung verfügbar") : qsTr("Aktualisierungen verfügbar")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    text: qsTr("%1 laden · %2 mehr belegt · zuletzt geprüft um %3 Uhr")
                          .arg(updatesModel.totalDownloadFormatted)
                          .arg(updatesModel.totalInstalledDeltaFormatted)
                          .arg(daemonClient.lastCheckedString)
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                }
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s3

                PrimaryButton {
                    text: qsTr("Aktualisieren")
                    variant: "primary"
                    enabled: updatesModel.selectedCount > 0
                    onClicked: root.startUpgradeRequested()
                }

                PrimaryButton {
                    text: qsTr("Prüfen")
                    variant: "quiet"
                    onClicked: root.refreshRequested()
                }
            }
        }
    }

    // Leerer Zustand
    EmptyState {
        visible: updatesModel.totalCount === 0
        anchors.top: heroCard.bottom
        anchors.topMargin: Theme.s7
        title: qsTr("System ist aktuell")
        message: qsTr("Alle Pakete entsprechen dem neuesten Stand der Paketquellen.")
    }

    // Rolling Release Hinweis (Arch / CachyOS)
    Card {
        id: rollingNoticeCard
        visible: !daemonClient.partialUpgradeSupported && updatesModel.totalCount > 0
        anchors.top: heroCard.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s6
        anchors.topMargin: Theme.s2
        height: 52

        Row {
            anchors.fill: parent
            anchors.margins: Theme.s3
            spacing: Theme.s4

            Text {
                text: "ℹ"
                font.pixelSize: 18
                font.weight: Font.Bold
                color: Theme.accent
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("Arch Linux / CachyOS (Rolling Release): Alle Pakete werden zusammen aktualisiert, um Systemkonsistenz zu gewährleisten.")
                font.pixelSize: 13
                color: Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // Paketliste
    ListView {
        id: pkgList
        visible: updatesModel.totalCount > 0
        anchors.top: rollingNoticeCard.visible ? rollingNoticeCard.bottom : heroCard.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.s6
        anchors.topMargin: Theme.s2

        model: updatesModel
        clip: true
        reuseItems: true
        spacing: 2

        delegate: PackageRow {
            width: pkgList.width
            pkgName: model.name
            versionTransition: model.versionTransition
            downloadSizeFormatted: model.downloadSizeFormatted
            repo: model.repo
            isSecurity: model.isSecurity
            isKernel: model.isKernel
            selected: model.selected
            showCheckbox: daemonClient.partialUpgradeSupported
            onToggled: updatesModel.toggleSelection(index)
        }
    }
}

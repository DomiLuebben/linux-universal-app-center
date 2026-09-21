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
        height: 210

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
                width: parent.width - 390

                Text {
                    text: qsTr("Geplante Paketänderungen")
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    text: qsTr("%1 noch laden · Speicheränderung %2 · geprüft um %3 Uhr")
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
                    text: qsTr("Plan ausführen")
                    variant: "primary"
                    enabled: daemonClient.hasPlan && !daemonClient.isBusy
                    onClicked: root.startUpgradeRequested()
                }

                PrimaryButton {
                    text: qsTr("Prüfen")
                    variant: "quiet"
                    enabled: !daemonClient.isBusy
                    onClicked: root.refreshRequested()
                }
            }
        }
    }

    Text {
        anchors.left: heroCard.left
        anchors.right: heroCard.right
        anchors.bottom: heroCard.bottom
        anchors.margins: Theme.s4
        text: daemonClient.statusMessage
        color: Theme.textMuted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Button {
        visible: daemonClient.dnf5Commands.length > 0
        enabled: !daemonClient.isBusy
        anchors.top: heroCard.top
        anchors.right: heroCard.right
        anchors.margins: Theme.s3
        text: qsTr("Paketaktion …")
        onClicked: actionDialog.open()
    }

    Dialog {
        id: actionDialog
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 520)
        title: qsTr("DNF5-Paketaktion vorbereiten")
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: daemonClient.planDnf5(commandBox.currentText, packageInput.text,
                                         commandBox.currentText === "upgrade" && securityOnly.checked,
                                         commandBox.currentText === "upgrade" && excludeKernel.checked)
        Column {
            width: parent.width
            spacing: 12
            ComboBox { id: commandBox; width: parent.width; model: daemonClient.dnf5Commands }
            TextField { id: packageInput; width: parent.width; placeholderText: qsTr("Paketnamen, Gruppen-ID oder Transaktionsnummer") }
            CheckBox { id: securityOnly; visible: commandBox.currentText === "upgrade"; text: qsTr("Nur Sicherheitsupdates") }
            CheckBox { id: excludeKernel; visible: commandBox.currentText === "upgrade"; text: qsTr("Kernel ausschließen") }
            Text {
                width: parent.width
                text: qsTr("Die Vorbereitung lädt benötigte Pakete herunter. Danach zeigt die Liste alle Änderungen zur Bestätigung.")
                wrapMode: Text.Wrap
                color: Theme.text
            }
        }
    }

    // Leerer Zustand
    EmptyState {
        visible: updatesModel.totalCount === 0 && !daemonClient.isBusy
        anchors.top: heroCard.bottom
        anchors.topMargin: Theme.s7
        title: daemonClient.hasPlan ? qsTr("Keine Paketänderungen") : qsTr("Noch kein bestätigter Plan")
        message: daemonClient.hasPlan ? qsTr("Der letzte Plan enthält keine Paketänderungen.") : qsTr("Mit Prüfen einen aktuellen Plan erstellen.")
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

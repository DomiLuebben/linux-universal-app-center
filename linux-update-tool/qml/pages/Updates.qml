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
                id: countLabel
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
                // Breite aus den tatsächlichen Nachbarn ableiten statt aus einer
                // geratenen Zahl: die vorherigen 390 px passten nicht zur
                // Knopfbreite, weshalb die Zeile darunter verdeckt wurde.
                width: Math.max(0, parent.width - countLabel.width
                                   - actionRow.width - 2 * parent.spacing)

                Text {
                    width: parent.width
                    elide: Text.ElideRight
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
                    width: parent.width
                    elide: Text.ElideRight
                }
            }

            Row {
                id: actionRow
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
                    enabled: !daemonClient.isBusy && !aurUpdates.busy
                    onClicked: {
                        root.refreshRequested();
                        // Ein Knopf für beide Listen auf dieser Seite.
                        if (aurUpdates.available) aurUpdates.check();
                    }
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

    // Gemeinsamer Listenbereich: Systemaktualisierungen, darunter durch einen
    // Trennstrich abgesetzt die AUR-Pakete. Bewusst eine Seite statt zwei.
    ScrollView {
        id: listenBereich
        anchors.top: rollingNoticeCard.visible ? rollingNoticeCard.bottom : heroCard.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.s6
        anchors.topMargin: Theme.s2
        clip: true
        contentWidth: availableWidth

        Column {
            width: listenBereich.availableWidth
            spacing: Theme.s3

            Text {
                text: qsTr("Systemaktualisierungen")
                visible: updatesModel.totalCount > 0
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.textMuted
            }

            ListView {
                id: pkgList
                width: parent.width
                height: contentHeight
                visible: updatesModel.totalCount > 0
                interactive: false          // scrollt über den umgebenden Bereich
                model: updatesModel
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

            // Trennstrich zum AUR-Teil
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: aurUpdates.available && updatesModel.totalCount > 0
            }

            Row {
                width: parent.width
                spacing: Theme.s3
                visible: aurUpdates.available

                Text {
                    text: qsTr("AUR")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    width: parent.width - 120
                    elide: Text.ElideRight
                    text: aurUpdates.statusMessage
                    font.pixelSize: 12
                    color: Theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            ListView {
                id: aurList
                width: parent.width
                height: contentHeight
                visible: aurUpdates.available && aurUpdates.count > 0
                interactive: false
                model: aurUpdates
                spacing: 2

                delegate: Row {
                    width: aurList.width
                    height: 44
                    spacing: Theme.s4

                    Column {
                        width: parent.width - aurKnopf.width - Theme.s4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: model.name
                            font.pixelSize: 14
                            color: Theme.text
                        }

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: model.installedVersion + " \u2192 " + model.availableVersion
                            font.pixelSize: 12
                            font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                            color: Theme.textMuted
                        }
                    }

                    PrimaryButton {
                        id: aurKnopf
                        text: qsTr("Aktualisieren")
                        variant: "quiet"
                        enabled: !aurUpdates.busy
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: aurUpdates.prepare(model.name)
                    }
                }
            }
        }
    }

    // Der PKGBUILD wird vor dem Bauen gezeigt. Ein AUR-Bau fuehrt fremden Code
    // mit den Rechten des Benutzers aus; das gehoert angesehen.
    Dialog {
        id: pkgbuildDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - Theme.s7, 900)
        height: Math.min(root.height - Theme.s7, 620)
        modal: true
        title: qsTr("PKGBUILD pruefen: %1").arg(paketName)

        property string paketName: ""
        property string pkgDir: ""

        footer: DialogButtonBox {
            Button {
                text: qsTr("Bauen und installieren")
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("Abbrechen")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        onAccepted: aurUpdates.build(pkgbuildDialog.pkgDir)

        ScrollView {
            anchors.fill: parent
            clip: true

            TextArea {
                id: pkgbuildAnsicht
                readOnly: true
                wrapMode: TextArea.NoWrap
                font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                font.pixelSize: 12
                color: Theme.textOnSunken
            }
        }
    }

    Connections {
        target: aurUpdates
        function onPrepared(name, pkgDir, pkgbuild, missingRepoDeps) {
            pkgbuildDialog.paketName = name;
            pkgbuildDialog.pkgDir = pkgDir;
            pkgbuildAnsicht.text = pkgbuild;
            pkgbuildDialog.open();
        }
    }
}

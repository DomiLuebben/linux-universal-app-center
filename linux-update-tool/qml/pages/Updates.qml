import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    id: root
    signal startUpgradeRequested()
    signal refreshRequested()

    readonly property bool checked: daemonClient.hasPlan
    readonly property bool pending: daemonClient.isBusy

    Column {
        id: heading
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.s6
        spacing: 6
        Text { text: qsTr("Aktualisierungen"); color: Theme.text; font.pixelSize: 27; font.weight: Font.DemiBold }
        Text { text: qsTr("Dein System im Überblick."); color: Theme.textMuted; font.pixelSize: 14 }
    }

    Card {
        id: heroCard
        anchors.top: heading.bottom
        anchors.topMargin: Theme.s5
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.s6
        anchors.rightMargin: Theme.s6
        height: heroContent.implicitHeight + Theme.s5 * 2
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

        ColumnLayout {
            id: heroContent
            anchors.fill: parent
            anchors.margins: Theme.s5
            spacing: Theme.s4
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s4
                Rectangle {
                    width: 56; height: 56; radius: 16
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                    Text {
                        anchors.centerIn: parent
                        text: daemonClient.hasError ? "!" : root.pending ? "…" : root.checked ? (updatesModel.totalCount > 0 ? updatesModel.totalCount : "✓") : "↻"
                        font.pixelSize: 28; font.weight: Font.DemiBold; color: daemonClient.hasError ? Theme.negative : Theme.accent
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        Layout.fillWidth: true
                        text: daemonClient.hasError ? qsTr("Aktion fehlgeschlagen") : root.pending ? qsTr("Paketlisten werden geprüft") : !root.checked ? qsTr("Aktuellen Stand prüfen") : updatesModel.totalCount > 0 ? qsTr("%1 Paketänderungen bereit").arg(updatesModel.totalCount) : qsTr("Dein System ist aktuell")
                        font.pixelSize: 20; font.weight: Font.DemiBold; color: Theme.text; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.pending ? qsTr("Die Prüfung kann einen Moment dauern.") : root.checked ? qsTr("Zuletzt geprüft um %1 Uhr").arg(daemonClient.lastCheckedString) : qsTr("Erst nach erfolgreicher Prüfung ist der Status bestätigt.")
                        font.pixelSize: 12; color: Theme.textMuted; wrapMode: Text.Wrap
                    }
                }
                BusyIndicator { running: root.pending; visible: running; implicitWidth: 30; implicitHeight: 30 }
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.separator }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s5
                ColumnLayout {
                    spacing: 4
                    Text { text: qsTr("DOWNLOAD"); font.pixelSize: 10; font.letterSpacing: 1; color: Theme.textMuted }
                    Text { text: root.checked ? updatesModel.totalDownloadFormatted : "—"; font.pixelSize: 18; color: Theme.text; font.weight: Font.DemiBold }
                }
                ColumnLayout {
                    spacing: 4
                    Text { text: qsTr("SPEICHERÄNDERUNG"); font.pixelSize: 10; font.letterSpacing: 1; color: Theme.textMuted }
                    Text { text: root.checked ? updatesModel.totalInstalledDeltaFormatted : "—"; font.pixelSize: 18; color: Theme.text; font.weight: Font.DemiBold }
                }
                Item { Layout.fillWidth: true }
                PrimaryButton {
                    text: qsTr("Prüfen")
                    variant: "quiet"
                    enabled: !root.pending && !aurUpdates.busy
                    onClicked: {
                        root.refreshRequested();
                        if (aurUpdates.available) aurUpdates.check();
                    }
                }
                PrimaryButton {
                    text: qsTr("Aktualisieren")
                    variant: "primary"
                    enabled: root.checked && !root.pending && updatesModel.totalCount > 0
                    onClicked: root.startUpgradeRequested()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: daemonClient.statusMessage
                color: daemonClient.hasError ? Theme.negative : Theme.textMuted; font.pixelSize: 12; wrapMode: Text.Wrap
            }
        }
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

    // Gemeinsamer Listenbereich: Systemaktualisierungen, darunter durch einen
    // Trennstrich abgesetzt die AUR-Pakete. Bewusst eine Seite statt zwei.
    ScrollView {
        id: listenBereich
        anchors.top: heroCard.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.s6
        anchors.topMargin: Theme.s5
        clip: true
        contentWidth: availableWidth

        Column {
            width: listenBereich.availableWidth
            spacing: Theme.s3

            RowLayout {
                width: parent.width
                Text { text: qsTr("Systempakete"); font.pixelSize: 15; font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
                Text { text: updatesModel.totalCount > 0 ? qsTr("%1 Pakete").arg(updatesModel.totalCount) : ""; font.pixelSize: 12; color: Theme.textMuted }
                PrimaryButton {
                    visible: daemonClient.dnf5Commands.length > 0
                    enabled: !root.pending
                    text: qsTr("Paketaktion …"); variant: "quiet"
                    onClicked: actionDialog.open()
                }
            }
            Text {
                width: parent.width
                visible: !daemonClient.partialUpgradeSupported && updatesModel.totalCount > 0
                text: qsTr("Rolling Release · Alle Systempakete werden gemeinsam aktualisiert.")
                font.pixelSize: 12; color: Theme.textMuted; wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                visible: updatesModel.totalCount === 0
                text: root.pending ? qsTr("Aktuelle Pakete werden ermittelt …") : root.checked ? qsTr("Für die eingerichteten Paketquellen sind keine Änderungen nötig.") : qsTr("Noch kein bestätigtes Prüfergebnis. Starte eine neue Prüfung.")
                color: Theme.textMuted; font.pixelSize: 13; wrapMode: Text.Wrap
                topPadding: Theme.s3; bottomPadding: Theme.s5
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root
    signal startUpgradeRequested()
    signal refreshRequested()
    signal openSettingsTabRequested(int tab)

    // Eingebettet in „Verwalten": die Überschrift kommt von dort.
    property bool compact: false

    readonly property bool checked: daemonClient.hasPlan || (!daemonClient.hasError && daemonClient.lastCheckedString.length > 0)
    readonly property bool pending: daemonClient.isBusy
    readonly property bool isAnyBusy: daemonClient.isBusy ||
        Boolean(typeof aurUpdates !== "undefined" && aurUpdates.busy) ||
        Boolean(typeof pacstallUpdates !== "undefined" && pacstallUpdates.busy) ||
        Boolean(typeof flatpakUpdates !== "undefined" && flatpakUpdates.busy) ||
        Boolean(typeof snapUpdates !== "undefined" && snapUpdates.busy)

    readonly property int totalAllUpdates: {
        var sum = updatesModel.totalCount;
        if (typeof aurUpdates !== "undefined" && aurUpdates.available) sum += aurUpdates.count;
        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available) sum += pacstallUpdates.count;
        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available) sum += flatpakUpdates.count;
        if (typeof snapUpdates !== "undefined" && snapUpdates.available) sum += snapUpdates.count;
        return sum;
    }

    readonly property string breakdownText: {
        var parts = [];
        if (updatesModel.totalCount > 0) parts.push(qsTr("%1 System").arg(updatesModel.totalCount));
        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available && flatpakUpdates.count > 0) {
            parts.push(qsTr("%1 Flatpak").arg(flatpakUpdates.count));
        }
        if (typeof snapUpdates !== "undefined" && snapUpdates.available && snapUpdates.count > 0) {
            parts.push(qsTr("%1 Snap").arg(snapUpdates.count));
        }
        if (typeof aurUpdates !== "undefined" && aurUpdates.available && aurUpdates.count > 0) {
            parts.push(qsTr("%1 AUR").arg(aurUpdates.count));
        }
        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available && pacstallUpdates.count > 0) {
            parts.push(qsTr("%1 Pacstall").arg(pacstallUpdates.count));
        }
        return parts.join(" \u00B7 ");
    }

    Column {
        id: heading
        visible: !root.compact
        height: root.compact ? 0 : implicitHeight
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
        anchors.topMargin: root.compact ? 0 : Theme.s5
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
                        text: daemonClient.hasError ? "!" : (root.pending || root.isAnyBusy) ? "\u2026" : (root.checked || (typeof flatpakUpdates !== "undefined" && flatpakUpdates.hasChecked)) ? (root.totalAllUpdates > 0 ? root.totalAllUpdates : "\u2713") : "\u21bb"
                        font.pixelSize: 28; font.weight: Font.DemiBold; color: daemonClient.hasError ? Theme.negative : Theme.accent
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    Text {
                        Layout.fillWidth: true
                        text: daemonClient.hasError ? qsTr("Aktion fehlgeschlagen") : (root.pending || root.isAnyBusy) ? qsTr("Paketlisten werden geprüft") : !root.checked ? qsTr("Aktuellen Stand prüfen") : root.totalAllUpdates > 0 ? qsTr("%1 Paketänderungen bereit").arg(root.totalAllUpdates) : qsTr("Dein System ist aktuell")
                        font.pixelSize: 20; font.weight: Font.DemiBold; color: Theme.text; wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (root.pending || root.isAnyBusy) ? qsTr("Die Prüfung kann einen Moment dauern.") : root.totalAllUpdates > 0 ? root.breakdownText : root.checked ? qsTr("Zuletzt geprüft um %1 Uhr").arg(daemonClient.lastCheckedString) : qsTr("Erst nach erfolgreicher Prüfung ist der Status bestätigt.")
                        font.pixelSize: 12; color: Theme.textMuted; wrapMode: Text.Wrap
                    }
                }
                BusyIndicator { running: root.pending || root.isAnyBusy; visible: running; implicitWidth: 30; implicitHeight: 30 }
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
                    enabled: !root.isAnyBusy
                    onClicked: {
                        root.refreshRequested();
                        if (typeof aurUpdates !== "undefined" && aurUpdates.available) aurUpdates.check();
                        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available) pacstallUpdates.check();
                        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available) flatpakUpdates.check();
                        if (typeof snapUpdates !== "undefined" && snapUpdates.available) snapUpdates.check();
                    }
                }
                PrimaryButton {
                    text: {
                        if (updatesModel.totalCount > 0) return qsTr("System aktualisieren")
                        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available && flatpakUpdates.count > 0) return qsTr("Flatpaks aktualisieren")
                        if (typeof snapUpdates !== "undefined" && snapUpdates.available && snapUpdates.count > 0) return qsTr("Snaps aktualisieren")
                        if (typeof aurUpdates !== "undefined" && aurUpdates.available && aurUpdates.count > 0) return qsTr("AUR aktualisieren")
                        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available && pacstallUpdates.count > 0) return qsTr("Pacstall aktualisieren")
                        return qsTr("System aktualisieren")
                    }
                    variant: "primary"
                    enabled: !root.isAnyBusy && root.totalAllUpdates > 0
                    onClicked: {
                        if (updatesModel.totalCount > 0) {
                            root.startUpgradeRequested()
                        } else if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available && flatpakUpdates.count > 0) {
                            flatpakUpdates.updateAll()
                        } else if (typeof snapUpdates !== "undefined" && snapUpdates.available && snapUpdates.count > 0) {
                            snapUpdates.updateAll()
                        } else if (typeof aurUpdates !== "undefined" && aurUpdates.available && aurUpdates.count > 0) {
                            aurUpdates.upgradeAll()
                        } else if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available && pacstallUpdates.count > 0) {
                            pacstallUpdates.upgradeAll()
                        }
                    }
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

            // Trennstrich zum Flatpak-Teil
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: Boolean(typeof flatpakUpdates !== "undefined" && flatpakUpdates.available && (updatesModel.totalCount > 0 || (flatpakUpdates.count > 0)))
            }

            RowLayout {
                width: parent.width
                visible: Boolean(typeof flatpakUpdates !== "undefined" && flatpakUpdates.available)

                Text {
                    text: qsTr("Flatpak")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                }

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: (typeof flatpakUpdates !== "undefined" && flatpakUpdates.count > 0) ? qsTr("%1 Aktualisierung(en) verfügbar").arg(flatpakUpdates.count) : qsTr("Alle Flatpaks sind aktuell.")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }

                PrimaryButton {
                    text: qsTr("Alle aktualisieren")
                    variant: "quiet"
                    visible: Boolean(typeof flatpakUpdates !== "undefined" && flatpakUpdates.count > 0)
                    enabled: !root.isAnyBusy
                    onClicked: flatpakUpdates.updateAll()
                }
            }

            ListView {
                id: flatpakList
                width: parent.width
                height: contentHeight
                visible: Boolean(typeof flatpakUpdates !== "undefined" && flatpakUpdates.available && flatpakUpdates.count > 0)
                interactive: false
                model: flatpakUpdates
                spacing: 2

                delegate: Row {
                    width: flatpakList.width
                    height: 44
                    spacing: Theme.s4

                    Column {
                        width: parent.width - flatpakKnopf.width - Theme.s4
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
                            text: model.versionTransition + (model.downloadSizeFormatted.length > 0 ? " \u00B7 " + model.downloadSizeFormatted : "")
                            font.pixelSize: 12
                            font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                            color: Theme.textMuted
                        }
                    }

                    PrimaryButton {
                        id: flatpakKnopf
                        text: qsTr("Aktualisieren")
                        variant: "quiet"
                        enabled: !root.isAnyBusy
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: flatpakUpdates.updateApp(model.ref)
                    }
                }
            }

            // Trennstrich zum Snap-Teil
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: Boolean(typeof snapUpdates !== "undefined" && snapUpdates.available && (updatesModel.totalCount > 0 || (typeof flatpakUpdates !== "undefined" && flatpakUpdates.count > 0) || snapUpdates.count > 0))
            }

            RowLayout {
                width: parent.width
                visible: Boolean(typeof snapUpdates !== "undefined" && snapUpdates.available)

                Text {
                    text: qsTr("Snap")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                }

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: (typeof snapUpdates !== "undefined" && snapUpdates.count > 0) ? qsTr("%1 Aktualisierung(en) verfügbar").arg(snapUpdates.count) : qsTr("Alle Snaps sind aktuell.")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }

                PrimaryButton {
                    text: qsTr("Alle aktualisieren")
                    variant: "quiet"
                    visible: Boolean(typeof snapUpdates !== "undefined" && snapUpdates.count > 0)
                    enabled: !root.isAnyBusy
                    onClicked: snapUpdates.updateAll()
                }
            }

            ListView {
                id: snapList
                width: parent.width
                height: contentHeight
                visible: Boolean(typeof snapUpdates !== "undefined" && snapUpdates.available && snapUpdates.count > 0)
                interactive: false
                model: snapUpdates
                spacing: 2

                delegate: Row {
                    width: snapList.width
                    height: 44
                    spacing: Theme.s4

                    Column {
                        width: parent.width - snapKnopf.width - Theme.s4
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
                            text: model.versionTransition + (model.publisher.length > 0 ? " \u00B7 " + model.publisher : "")
                            font.pixelSize: 12
                            font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                            color: Theme.textMuted
                        }
                    }

                    PrimaryButton {
                        id: snapKnopf
                        text: qsTr("Aktualisieren")
                        variant: "quiet"
                        enabled: !root.isAnyBusy
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: snapUpdates.updateApp(model.name)
                    }
                }
            }

            // Hinweiszeile bei vorhandenen Fremdpaketen und ausgeschaltetem AUR (Abschnitt 3.3)
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: Boolean(typeof aurUpdates !== "undefined" && !aurUpdates.available && aurUpdates.hasForeignPackages)
            }

            RowLayout {
                width: parent.width
                visible: Boolean(typeof aurUpdates !== "undefined" && !aurUpdates.available && aurUpdates.hasForeignPackages)
                spacing: Theme.s2

                Kirigami.Icon {
                    source: "dialog-information"
                    implicitWidth: 16
                    implicitHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("%1 Pakete stammen nicht aus deinen Paketquellen (z. B. aus dem AUR). AUR-Aktualisierungen sind ausgeschaltet.").arg(typeof aurUpdates !== "undefined" ? aurUpdates.foreignPackageCount : 0)
                    font.pixelSize: 12
                    color: Theme.textMuted
                    wrapMode: Text.Wrap
                }

                Text {
                    text: qsTr("Einstellungen")
                    font.pixelSize: 12
                    font.underline: true
                    color: Theme.accent
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openSettingsTabRequested(1)
                    }
                }
            }

            // Trennstrich zum AUR-Teil
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: Boolean(typeof aurUpdates !== "undefined" && aurUpdates.available && (updatesModel.totalCount > 0 || (typeof flatpakUpdates !== "undefined" && flatpakUpdates.count > 0) || (typeof snapUpdates !== "undefined" && snapUpdates.count > 0) || aurUpdates.count > 0))
            }

            RowLayout {
                width: parent.width
                visible: aurUpdates.available

                Text {
                    text: qsTr("AUR")
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    color: Theme.textMuted
                }

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: aurUpdates.statusMessage
                    font.pixelSize: 12
                    color: Theme.textMuted
                }

                // Alle AUR-Pakete in einem Durchgang: eine Prüfansicht, ein Passwort.
                PrimaryButton {
                    text: qsTr("Alle aktualisieren")
                    variant: "quiet"
                    visible: aurUpdates.count > 0
                    enabled: !root.isAnyBusy
                    onClicked: aurUpdates.upgradeAll()
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
                        enabled: !root.isAnyBusy
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: aurUpdates.upgradePackages([model.name])
                    }
                }
            }

            // Pacstall-Aktualisierungen (Paket C)
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.separator
                visible: Boolean(typeof pacstallUpdates !== "undefined" && pacstallUpdates.available && (updatesModel.totalCount > 0 || (typeof flatpakUpdates !== "undefined" && flatpakUpdates.count > 0) || (typeof snapUpdates !== "undefined" && snapUpdates.count > 0) || (typeof aurUpdates !== "undefined" && aurUpdates.count > 0)))
            }

            Row {
                width: parent.width
                height: 36
                spacing: Theme.s3
                visible: Boolean(typeof pacstallUpdates !== "undefined" && pacstallUpdates.available)

                Kirigami.Icon {
                    source: "package-x-generic"
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: qsTr("Pacstall")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: typeof pacstallUpdates !== "undefined" ? pacstallUpdates.statusMessage : ""
                    font.pixelSize: 12
                    color: Theme.textMuted
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    width: parent.width - 240
                }

                Item { width: 1; height: 1 }

                PrimaryButton {
                    text: qsTr("Alle aktualisieren")
                    variant: "primary"
                    visible: typeof pacstallUpdates !== "undefined" && pacstallUpdates.count > 0
                    enabled: !root.isAnyBusy
                    onClicked: pacstallUpdates.upgradeAll()
                }
            }

            ListView {
                id: pacstallList
                width: parent.width
                height: contentHeight
                visible: Boolean(typeof pacstallUpdates !== "undefined" && pacstallUpdates.available && pacstallUpdates.count > 0)
                interactive: false
                model: typeof pacstallUpdates !== "undefined" ? pacstallUpdates : null
                spacing: 2

                delegate: Row {
                    width: pacstallList.width
                    height: 44
                    spacing: Theme.s4

                    Column {
                        width: parent.width - pacstallKnopf.width - Theme.s4
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
                        id: pacstallKnopf
                        text: qsTr("Aktualisieren")
                        variant: "quiet"
                        enabled: !root.isAnyBusy
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: pacstallUpdates.upgradePackages([model.name])
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

// Vorschau einer Store-Transaktion. Zeigt den aufgelösten Paketplan und bindet
// die Bestätigung an genau die Planrevision, die hier angezeigt wird
// (Abschnitt 3.8 und Zustandsmatrix "Vorschau wartet auf Bestätigung").
Item {
    id: root

    signal confirmed()
    signal discarded()

    readonly property var plan: daemonClient.planModel

    ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth

        Column {
            width: scroller.availableWidth - Theme.s6 * 2
            x: Theme.s6
            topPadding: Theme.s6
            bottomPadding: Theme.s6
            spacing: Theme.s5

            Row {
                width: parent.width
                spacing: Theme.s3

                PrimaryButton {
                    iconName: "go-previous"
                    text: qsTr("Zurück")
                    variant: "quiet"
                    onClicked: root.discarded()
                }
            }

            Text {
                text: root.plan.actionTitle.length > 0 ? root.plan.actionTitle : qsTr("Paketoperation")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Theme.text
                textFormat: Text.PlainText
            }

            Text {
                width: parent.width
                text: qsTr("%1 zu installieren · %2 zu aktualisieren · %3 zu entfernen")
                        .arg(root.plan.installCount)
                        .arg(root.plan.upgradeCount)
                        .arg(root.plan.removeCount)
                font.pixelSize: 14
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }

            // Größen erst nach der Auflösung - vorher wird nichts versprochen.
            Row {
                width: parent.width
                spacing: Theme.s6

                Column {
                    spacing: Theme.s1
                    Text { text: qsTr("Herunterladen"); font.pixelSize: 12; color: Theme.textMuted }
                    Text {
                        text: root.plan.totalDownloadFormatted
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                }

                Column {
                    spacing: Theme.s1
                    Text { text: qsTr("Speicherbedarf"); font.pixelSize: 12; color: Theme.textMuted }
                    Text {
                        text: root.plan.totalInstalledDeltaFormatted
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                }
            }

            // Abschnitt 8.7: betroffene Anwendungen ausdrücklich nennen, auch die,
            // die der Benutzer nicht angeklickt hat.
            Card {
                width: parent.width
                height: affectedColumn.implicitHeight + Theme.s4 * 2
                visible: root.plan.affectedApps.length > 0

                Column {
                    id: affectedColumn
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2

                    Row {
                        spacing: Theme.s2
                        Kirigami.Icon { source: "dialog-warning"; width: 18; height: 18 }
                        Text {
                            text: qsTr("Betroffene Anwendungen")
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                    }

                    Text {
                        width: affectedColumn.width
                        text: root.plan.affectedApps.join(", ")
                        textFormat: Text.PlainText
                        font.pixelSize: 13
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // Warnungen des Backends unverändert weiterreichen
            Card {
                width: parent.width
                height: warningColumn.implicitHeight + Theme.s4 * 2
                visible: root.plan.warnings.length > 0

                Column {
                    id: warningColumn
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2

                    Repeater {
                        model: root.plan.warnings
                        delegate: Text {
                            width: warningColumn.width
                            text: modelData
                            textFormat: Text.PlainText
                            font.pixelSize: 13
                            color: Theme.negative
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            // EmptyState zentriert sich selbst und braucht deshalb einen Rahmen.
            Item {
                width: parent.width
                height: 200
                visible: root.plan.isEmpty

                EmptyState {
                    title: qsTr("Keine Änderungen erforderlich")
                    message: qsTr("Der Paketplan enthält keine Operationen.")
                }
            }

            ListView {
                width: parent.width
                height: Math.min(contentHeight, 420)
                visible: !root.plan.isEmpty
                clip: true
                model: root.plan
                interactive: contentHeight > height

                delegate: PackageRow {
                    width: ListView.view.width
                    pkgName: model.name
                    versionTransition: model.version.length > 0 && model.newVersion.length > 0
                                       ? model.version + " → " + model.newVersion
                                       : (model.newVersion.length > 0 ? model.newVersion : model.version)
                    downloadSizeFormatted: model.downloadSizeFormatted
                    repo: model.repo
                    isSecurity: model.isSecurity
                    isKernel: model.isKernel
                    showCheckbox: false
                }
            }

            Row {
                width: parent.width
                spacing: Theme.s3

                PrimaryButton {
                    objectName: "storeConfirmButton"
                    text: root.plan.removeCount > 0 && root.plan.installCount === 0
                          ? qsTr("Entfernen bestätigen")
                          : qsTr("Installation bestätigen")
                    iconName: "dialog-ok"
                    variant: root.plan.removeCount > 0 && root.plan.installCount === 0 ? "destructive" : "primary"
                    enabled: daemonClient.hasPlan && !daemonClient.isBusy && root.plan.planRevision.length > 0 && !root.plan.isEmpty && !root.plan.hasProtectedConflict
                    onClicked: root.confirmed()
                }

                PrimaryButton {
                    text: qsTr("Verwerfen")
                    variant: "quiet"
                    enabled: !daemonClient.isBusy
                    onClicked: root.discarded()
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    // Einmal je Sitzung von selbst prüfen. Danach entscheidet der Benutzer
    // über den Knopf, damit nicht jeder Seitenwechsel ins Netz greift.
    Component.onCompleted: {
        if (aurUpdates.available && !aurUpdates.hasChecked && !aurUpdates.busy) {
            aurUpdates.check();
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        Row {
            width: parent.width
            spacing: Theme.s4

            Column {
                width: parent.width - pruefenKnopf.width - Theme.s4
                spacing: 4

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: qsTr("AUR-Aktualisierungen")
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    text: aurUpdates.statusMessage
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }

            PrimaryButton {
                id: pruefenKnopf
                text: qsTr("Prüfen")
                variant: "primary"
                enabled: aurUpdates.available && !aurUpdates.busy
                anchors.verticalCenter: parent.verticalCenter
                onClicked: aurUpdates.check()
            }
        }

        // Hinweis, wenn der Helfer fehlt: ohne ihn kann die Seite nichts tun.
        Card {
            width: parent.width
            height: 72
            visible: !aurUpdates.available

            Text {
                anchors.fill: parent
                anchors.margins: Theme.s4
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
                color: Theme.textMuted
                text: qsTr("Diese Seite nutzt den AUR-Helfer des Pakets linux-package-installer. "
                         + "Nach dessen Installation steht sie zur Verfügung.")
            }
        }

        Card {
            width: parent.width
            height: parent.height - y
            visible: aurUpdates.available
            sunken: true

            ListView {
                id: liste
                anchors.fill: parent
                anchors.margins: Theme.s3
                clip: true
                spacing: Theme.s2
                model: aurUpdates

                delegate: Row {
                    width: liste.width
                    spacing: Theme.s4

                    Column {
                        width: parent.width - aktualisierenKnopf.width - Theme.s4
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: model.name
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.textOnSunken
                        }

                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: model.installedVersion + " → " + model.availableVersion
                            font.pixelSize: 12
                            font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                            color: Theme.textMuted
                        }
                    }

                    PrimaryButton {
                        id: aktualisierenKnopf
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

    // Der PKGBUILD wird vor dem Bauen gezeigt. Ein AUR-Bau führt fremden Code
    // mit den Rechten des Benutzers aus; das gehört angesehen, nicht bestätigt.
    Dialog {
        id: pkgbuildDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(root.width - Theme.s7, 900)
        height: Math.min(root.height - Theme.s7, 620)
        modal: true
        title: qsTr("PKGBUILD prüfen: %1").arg(paketName)

        property string paketName: ""
        property string pkgDir: ""
        property string fehlendeDeps: ""

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

        Column {
            anchors.fill: parent
            spacing: Theme.s2

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                visible: pkgbuildDialog.fehlendeDeps.length > 0
                color: Theme.neutral
                font.pixelSize: 12
                text: qsTr("Wird zusätzlich aus den offiziellen Quellen installiert: %1")
                      .arg(pkgbuildDialog.fehlendeDeps)
            }

            ScrollView {
                width: parent.width
                height: parent.height - (pkgbuildDialog.fehlendeDeps.length > 0 ? 40 : 0)
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
    }

    Connections {
        target: aurUpdates
        function onPrepared(name, pkgDir, pkgbuild, missingRepoDeps) {
            pkgbuildDialog.paketName = name;
            pkgbuildDialog.pkgDir = pkgDir;
            pkgbuildDialog.fehlendeDeps = missingRepoDeps.join(", ");
            pkgbuildAnsicht.text = pkgbuild;
            pkgbuildDialog.open();
        }
    }
}

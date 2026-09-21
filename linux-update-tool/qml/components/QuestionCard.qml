import QtQuick
import QtQuick.Controls

Card {
    id: root
    raised: true

    property string questionId: ""
    property string questionKind: "" // "GpgKeyImport", "ConffilePrompt", "FileConflict"
    property var payload: ({})

    signal answered(string id, var answer)

    implicitHeight: col.implicitHeight + Theme.s5 * 2
    implicitWidth: 600

    Column {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s4

        Text {
            text: {
                if (root.questionKind === "GpgKeyImport") return qsTr("GPG-Schlüssel importieren?")
                if (root.questionKind === "ConffilePrompt") return qsTr("Konfigurationsdatei geändert")
                return qsTr("Bestätigung erforderlich")
            }
            font.pixelSize: 16
            font.weight: Font.DemiBold
            color: Theme.text
        }

        // GPG-Key Ansicht
        Column {
            visible: root.questionKind === "GpgKeyImport"
            width: parent.width
            spacing: Theme.s2

            Text {
                text: qsTr("Repository: %1").arg(root.payload.repo || "")
                color: Theme.text
                font.pixelSize: 13
            }

            Text {
                text: qsTr("Benutzer: %1").arg(root.payload.userId || "")
                color: Theme.text
                font.pixelSize: 13
            }

            Card {
                sunken: true
                width: parent.width
                height: 48
                Text {
                    anchors.centerIn: parent
                    text: root.payload.fingerprint || ""
                    font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textOnSunken
                }
            }
        }

        // Conffile Diff Ansicht
        Column {
            visible: root.questionKind === "ConffilePrompt"
            width: parent.width
            spacing: Theme.s2

            Text {
                text: root.payload.file || ""
                font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                font.pixelSize: 12
                color: Theme.text
            }

            ScrollView {
                width: parent.width
                height: 140
                clip: true

                TextArea {
                    readOnly: true
                    text: root.payload.diff || ""
                    font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                    font.pixelSize: 12
                    color: Theme.textOnSunken
                    background: Rectangle {
                        color: Theme.surfaceSunken
                        radius: Theme.radiusControl
                    }
                }
            }
        }

        // Knöpfe
        Row {
            anchors.right: parent.right
            spacing: Theme.s3

            PrimaryButton {
                text: root.questionKind === "ConffilePrompt" ? qsTr("Eigene behalten") : qsTr("Ablehnen")
                variant: "quiet"
                onClicked: root.answered(root.questionId, { "accepted": false })
            }

            PrimaryButton {
                text: root.questionKind === "ConffilePrompt" ? qsTr("Neue übernehmen") : qsTr("Importieren")
                variant: "primary"
                onClicked: root.answered(root.questionId, { "accepted": true })
            }
        }
    }
}

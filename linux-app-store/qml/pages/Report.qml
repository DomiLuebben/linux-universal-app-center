import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root
    signal closeReportRequested()

    property bool rebootRequired: false
    property var servicesNeedingRestart: []
    property var pacnewFiles: []
    property string summaryText: qsTr("Alle Pakete wurden erfolgreich aktualisiert.")

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s5

        Text {
            text: qsTr("Abschlussbericht")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

        // Neustart-Warnung falls erforderlich
        Card {
            visible: root.rebootRequired
            width: parent.width
            height: 90
            color: Theme.negative.lighter(1.6)

            Row {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s4

                Text {
                    text: "⚠"
                    font.pixelSize: 32
                    color: Theme.negative
                    anchors.verticalCenter: parent.verticalCenter
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4
                    Text {
                        text: qsTr("System-Neustart erforderlich")
                        font.pixelSize: 15
                        font.weight: Font.Bold
                        color: Theme.negative
                    }
                    Text {
                        text: qsTr("Ein neuer Linux-Kernel oder Systembibliotheken wurden eingespielt.")
                        font.pixelSize: 13
                        color: Theme.text
                    }
                }
            }
        }

        // Konfigurationsänderungen (.pacnew / .pacsave)
        Card {
            visible: root.pacnewFiles && root.pacnewFiles.length > 0
            width: parent.width
            height: Math.min(220, 70 + (root.pacnewFiles ? root.pacnewFiles.length * 26 : 0))

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Row {
                    spacing: Theme.s2
                    Text {
                        text: "📄"
                        font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: qsTr("Konfigurationsdateien (.pacnew erkannt)")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }

                Text {
                    text: qsTr("Folgende Konfigurationsdateien erfordern Prüfung oder Zusammenführung:")
                    font.pixelSize: 12
                    color: Theme.textMuted
                }

                ListView {
                    width: parent.width
                    height: Math.min(120, parent.height - 70)
                    clip: true
                    model: root.pacnewFiles
                    delegate: Text {
                        width: parent ? parent.width : 0
                        text: modelData
                        font.pixelSize: 12
                        font.family: "monospace"
                        color: Theme.text
                        elide: Text.ElideMiddle
                    }
                }
            }
        }

        // Dienste-Neustart empfohlen
        Card {
            visible: root.servicesNeedingRestart && root.servicesNeedingRestart.length > 0
            width: parent.width
            height: 90

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Text {
                    text: qsTr("Dienste-Neustart empfohlen")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Text {
                    text: root.servicesNeedingRestart ? root.servicesNeedingRestart.join(", ") : ""
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }
        }

        // Zusammenfassung
        Card {
            width: parent.width
            height: 100

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Text {
                    text: qsTr("Zusammenfassung")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Text {
                    text: root.summaryText
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }
        }

        Item { height: 20 }

        PrimaryButton {
            text: qsTr("Zurück zur Übersicht")
            variant: "primary"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: root.closeReportRequested()
        }
    }
}

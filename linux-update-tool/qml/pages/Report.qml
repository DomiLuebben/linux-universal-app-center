import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root
    signal closeReportRequested()

    property bool rebootRequired: false
    property var servicesNeedingRestart: []
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

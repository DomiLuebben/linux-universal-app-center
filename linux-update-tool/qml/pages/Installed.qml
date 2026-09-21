import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        Text {
            text: qsTr("Installierte Pakete & Hygiene")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

        // Aufräum-Karte
        Card {
            width: parent.width
            height: 110

            Row {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s5

                Column {
                    width: parent.width - 200
                    spacing: 4
                    Text {
                        text: qsTr("System aufräumen")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }
                    Text {
                        text: qsTr("Verwaiste Abhängigkeiten, alte Kernel und Paketcache bereinigen.")
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }

                PrimaryButton {
                    text: qsTr("Jetzt bereinigen")
                    variant: "quiet"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: {
                        // Aufräumen triggern
                    }
                }
            }
        }

        // Suchfeld
        TextField {
            id: searchBox
            width: parent.width
            placeholderText: qsTr("Installierte Pakete durchsuchen...")
            font.pixelSize: 14
            color: Theme.text
            background: Rectangle {
                radius: Theme.radiusControl
                color: Theme.surfaceSunken
                border.color: searchBox.activeFocus ? Theme.accent : Theme.separator
                border.width: 1
            }
        }

        EmptyState {
            title: qsTr("Paketverwaltung bereit")
            message: qsTr("Nutze die Suche, um installierte Pakete zu finden.")
        }
    }
}

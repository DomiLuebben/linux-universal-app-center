import QtQuick
import "../components"

Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s5

        Text {
            text: qsTr("Einstellungen")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

        Card {
            width: parent.width
            height: 90

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Text {
                    text: qsTr("Erscheinungsbild")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    text: qsTr("Folgt den Systemfarben von KDE Plasma / Breeze.")
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }
        }

        Card {
            width: parent.width
            height: 90

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Text {
                    text: qsTr("Paketmanager-Backend")
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    text: qsTr("Nativer Zugriff ohne PackageKit.")
                    font.pixelSize: 13
                    color: Theme.textMuted
                }
            }
        }
    }
}

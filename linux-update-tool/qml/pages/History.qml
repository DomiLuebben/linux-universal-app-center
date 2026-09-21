import QtQuick
import "../components"

Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        Text {
            text: qsTr("Aktualisierungsverlauf")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

        EmptyState {
            title: qsTr("Keine früheren Einträge")
            message: qsTr("Durchgeführte Transaktionen werden hier chronologisch aufgeführt.")
        }
    }
}

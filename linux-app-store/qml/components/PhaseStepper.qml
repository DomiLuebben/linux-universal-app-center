import QtQuick

Row {
    id: root
    property int currentPhaseIndex: 0 // 0..5

    readonly property var phases: [
        qsTr("Prüfen"),
        qsTr("Laden"),
        qsTr("Installieren"),
        qsTr("Skripte"),
        qsTr("Aufräumen"),
        qsTr("Fertig")
    ]

    spacing: Theme.s4
    anchors.horizontalCenter: parent.horizontalCenter

    Repeater {
        model: root.phases
        delegate: Row {
            spacing: Theme.s2

            Rectangle {
                width: 18
                height: 18
                radius: 9
                anchors.verticalCenter: parent.verticalCenter

                color: {
                    if (index < root.currentPhaseIndex) return Theme.accent
                    if (index === root.currentPhaseIndex) return "transparent"
                    return Theme.surfaceSunken
                }

                border.color: index <= root.currentPhaseIndex ? Theme.accent : Theme.separator
                border.width: index === root.currentPhaseIndex ? 2.5 : 1

                Rectangle {
                    visible: index === root.currentPhaseIndex
                    anchors.centerIn: parent
                    width: 6
                    height: 6
                    radius: 3
                    color: Theme.accent
                }
            }

            Text {
                text: modelData
                font.pixelSize: 13
                font.weight: index === root.currentPhaseIndex ? Font.DemiBold : Font.Normal
                color: index === root.currentPhaseIndex ? Theme.text : (index < root.currentPhaseIndex ? Theme.text : Theme.textMuted)
                anchors.verticalCenter: parent.verticalCenter
            }

            // Verbindungslinie
            Rectangle {
                visible: index < root.phases.length - 1
                width: Theme.s4
                height: 2
                anchors.verticalCenter: parent.verticalCenter
                color: index < root.currentPhaseIndex ? Theme.accent : Theme.separator
            }
        }
    }
}

import QtQuick
import QtQuick.Controls

ListView {
    id: root
    property var tasks: []

    implicitHeight: Math.min(220, count * 44)
    clip: true
    spacing: Theme.s2

    delegate: Rectangle {
        width: root.width
        height: 40
        radius: Theme.radiusControl
        color: modelData.isRunning ? Theme.surfaceAlt : "transparent"

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.s3
            anchors.rightMargin: Theme.s3
            spacing: Theme.s3

            // Statussymbol
            Rectangle {
                width: 16
                height: 16
                radius: 8
                anchors.verticalCenter: parent.verticalCenter

                color: {
                    if (modelData.isFinished) return modelData.exitCode === 0 ? Theme.positive : Theme.negative
                    if (modelData.isRunning) return "transparent"
                    return Theme.surfaceSunken
                }
                border.color: modelData.isRunning ? Theme.accent : "transparent"
                border.width: modelData.isRunning ? 2 : 0

                Text {
                    visible: modelData.isFinished
                    anchors.centerIn: parent
                    text: modelData.exitCode === 0 ? "✓" : "✗"
                    color: Theme.onAccent
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 80

                Text {
                    text: modelData.label
                    font.pixelSize: 13
                    font.weight: modelData.isRunning ? Font.DemiBold : Font.Normal
                    color: modelData.isRunning ? Theme.accent : Theme.text
                }

                Text {
                    text: modelData.name
                    font.pixelSize: 11
                    font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                    color: Theme.textMuted
                }
            }
        }
    }
}

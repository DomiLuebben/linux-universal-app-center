import QtQuick

Card {
    id: root
    property string value: "0"
    property string label: ""
    property string secondary: ""

    implicitHeight: col.implicitHeight + Theme.s5 * 2
    implicitWidth: col.implicitWidth + Theme.s5 * 2

    Column {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s2

        Text {
            text: root.value
            font.pixelSize: 28
            font.weight: Font.DemiBold
            font.features: ({ "tnum": 1 })
            color: Theme.accent
        }

        Text {
            text: root.label
            font.pixelSize: 16
            font.weight: Font.Medium
            color: Theme.text
        }

        Text {
            visible: root.secondary.length > 0
            text: root.secondary
            font.pixelSize: 13
            color: Theme.textMuted
            font.features: ({ "tnum": 1 })
        }
    }
}

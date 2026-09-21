import QtQuick

Column {
    id: root
    property string title: ""
    property string message: ""

    spacing: Theme.s3
    anchors.centerIn: parent

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: "✓"
        font.pixelSize: 48
        font.weight: Font.Bold
        color: Theme.positive
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.title
        font.pixelSize: 20
        font.weight: Font.DemiBold
        color: Theme.text
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.message
        font.pixelSize: 14
        color: Theme.textMuted
    }
}

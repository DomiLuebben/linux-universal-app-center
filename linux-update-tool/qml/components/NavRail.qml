import QtQuick

Rectangle {
    id: root
    property int currentIndex: 0
    signal pageSelected(int index)

    readonly property var items: [
        { label: qsTr("Updates"), icon: "update" },
        { label: qsTr("Installiert"), icon: "package" },
        { label: qsTr("Verlauf"), icon: "history" },
        { label: qsTr("Protokoll"), icon: "log" },
        { label: qsTr("Einstellungen"), icon: "settings" }
    ]

    width: 64
    color: Theme.surface

    // Gleitender Akzent-Indikator
    Rectangle {
        id: indicator
        width: 3
        height: 36
        x: 0
        y: Theme.s4 + (root.currentIndex * 56) + 10
        color: Theme.accent
        radius: 1.5

        Behavior on y {
            enabled: Theme.motionScale > 0
            NumberAnimation { duration: Theme.durationBase; easing.type: Easing.OutQuint }
        }
    }

    Column {
        anchors.top: parent.top
        anchors.topMargin: Theme.s4
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.s2

        Repeater {
            model: root.items
            delegate: Rectangle {
                width: 48
                height: 48
                radius: Theme.radiusControl
                color: mouse.containsMouse ? Theme.surfaceAlt : "transparent"

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.pageSelected(index)
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData.label.substring(0, 1)
                    font.pixelSize: 16
                    font.weight: index === root.currentIndex ? Font.Bold : Font.Normal
                    color: index === root.currentIndex ? Theme.accent : Theme.textMuted
                }
            }
        }
    }
}

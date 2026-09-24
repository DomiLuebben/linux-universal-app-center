import QtQuick

Rectangle {
    id: root

    property double totalProgress: 0.0 // 0.0 bis 1.0
    property double wRefresh: 0.02
    property double wResolve: 0.03
    property double wDownload: 0.40
    property double wVerify: 0.02
    property double wCommit: 0.40
    property double wPost: 0.13

    implicitWidth: 500
    implicitHeight: 12
    radius: height / 2
    color: Theme.surfaceSunken
    clip: true

    // Aktiver Füllbalken mit weichem Übergang
    Rectangle {
        id: fillBar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(parent.width, Math.max(0, parent.width * root.totalProgress))
        radius: parent.radius
        color: Theme.accent

        Behavior on width {
            enabled: Theme.motionScale > 0
            NumberAnimation { duration: Theme.durationBase; easing.type: Easing.OutQuint }
        }
    }

    // Phasenteiler-Linien
    Repeater {
        model: [
            root.wRefresh,
            root.wRefresh + root.wResolve,
            root.wRefresh + root.wResolve + root.wDownload,
            root.wRefresh + root.wResolve + root.wDownload + root.wVerify,
            root.wRefresh + root.wResolve + root.wDownload + root.wVerify + root.wCommit
        ]
        delegate: Rectangle {
            x: Math.round(root.width * modelData)
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1.5
            color: Theme.bg
            z: 2
        }
    }
}

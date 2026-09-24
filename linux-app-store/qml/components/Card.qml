import QtQuick

Rectangle {
    id: root
    property bool sunken: false
    property bool raised: false

    implicitWidth: 200
    implicitHeight: 100

    radius: Theme.radiusCard
    color: sunken ? Theme.surfaceSunken : (raised ? Theme.surfaceRaised : Theme.surface)
    border.color: Theme.separator
    border.width: 1

    Behavior on color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: Theme.durationBase }
    }
}

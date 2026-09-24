import QtQuick
import QtQuick.Shapes

Shape {
    id: root
    property var samples: [] // [bps1, bps2, ...]

    implicitWidth: 120
    implicitHeight: 36

    ShapePath {
        strokeColor: Theme.accent
        strokeWidth: 2
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin

        startX: 0
        startY: root.height

        PathLine {
            x: root.width
            y: root.height / 2
        }
    }
}

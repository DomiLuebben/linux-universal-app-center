import QtQuick

Rectangle {
    id: root
    property string text: ""
    property string variant: "neutral" // "neutral", "security", "kernel", "held"

    implicitWidth: label.implicitWidth + Theme.s3 * 2
    implicitHeight: 26
    radius: Theme.radiusChip

    color: {
        if (variant === "security") return Theme.negative.lighter(1.6)
        if (variant === "kernel") return Theme.accent.lighter(1.6)
        if (variant === "held") return Theme.neutral.lighter(1.6)
        return Theme.surfaceSunken
    }

    border.color: {
        if (variant === "security") return Theme.negative
        if (variant === "kernel") return Theme.accent
        if (variant === "held") return Theme.neutral
        return Theme.separator
    }
    border.width: 1

    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: 12
        font.weight: Font.Medium
        color: {
            if (variant === "security") return Theme.negative
            if (variant === "kernel") return Theme.accent
            if (variant === "held") return Theme.neutral
            return Theme.text
        }
    }
}

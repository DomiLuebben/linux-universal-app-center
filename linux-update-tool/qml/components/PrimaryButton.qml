import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.Button {
    id: control

    property string variant: "primary" // "primary", "destructive", "quiet"
    property string iconName: ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftPadding + rightPadding,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topPadding + bottomPadding,
                             implicitContentHeight + topPadding + bottomPadding)

    leftPadding: Theme.s4
    rightPadding: Theme.s4
    topPadding: Theme.s2
    bottomPadding: Theme.s2

    // Kein anchors.centerIn: die Geometrie des contentItem setzt der Button
    // selbst; ein Anker dagegen streitet mit dieser Zuweisung.
    contentItem: Row {
        spacing: Theme.s2

        Kirigami.Icon {
            visible: control.iconName.length > 0 || (control.icon && control.icon.name.length > 0)
            source: control.iconName.length > 0 ? control.iconName : (control.icon ? control.icon.name : "")
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            visible: control.text.length > 0
            text: control.text
            font: control.font
            color: {
                if (!control.enabled) return Theme.textMuted
                if (control.variant === "primary") return Theme.onAccent
                if (control.variant === "destructive") return Theme.onAccent
                return Theme.text
            }
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        // Diese Werte kommen im QQC2-Größenmodell NOCH das Padding obendrauf.
        // Mit den vorherigen 100x40 ergab das 132x56 px grosse Knöpfe – rund
        // doppelt so hoch wie ein Breeze-Knopf. Kirigami.Units skaliert
        // ausserdem mit Schriftgrösse und DPI des Benutzers.
        implicitWidth: Kirigami.Units.gridUnit * 3
        implicitHeight: Kirigami.Units.gridUnit
        radius: Theme.radiusControl

        color: {
            if (!control.enabled) return Theme.surfaceSunken
            if (control.variant === "primary") {
                return control.down ? Theme.accent.darker(1.1) : (control.hovered ? Theme.accent.lighter(1.08) : Theme.accent)
            }
            if (control.variant === "destructive") {
                return control.down ? Theme.negative.darker(1.1) : (control.hovered ? Theme.negative.lighter(1.08) : Theme.negative)
            }
            // quiet
            return control.hovered ? Theme.surfaceRaised : "transparent"
        }

        border.color: {
            if (control.visualFocus) return Theme.accent
            if (control.variant === "quiet") return Theme.separator
            return "transparent"
        }
        border.width: control.visualFocus ? 2 : (control.variant === "quiet" ? 1 : 0)

        Behavior on color {
            enabled: Theme.motionScale > 0
            ColorAnimation { duration: Theme.durationFast }
        }
    }
}

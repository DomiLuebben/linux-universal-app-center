import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Card {
    id: root

    property string appKey: ""
    property string name: ""
    property string summary: ""
    property string iconSource: ""
    property string actionState: "Available"
    property bool isInstalled: false
    property string origin: ""

    signal clicked()

    implicitWidth: 280
    implicitHeight: 110

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.name.length > 0 ? root.name : qsTr("Anwendung")
    Accessible.description: root.summary

    color: mouseArea.containsMouse ? Theme.surfaceRaised : Theme.surface
    border.color: activeFocus ? Theme.accent : (mouseArea.containsMouse ? Theme.accent : Theme.separator)
    border.width: activeFocus ? 2 : 1

    Behavior on color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: Theme.durationFast }
    }

    Behavior on border.color {
        enabled: Theme.motionScale > 0
        ColorAnimation { duration: Theme.durationFast }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.s4
        spacing: Theme.s3

        // App Icon
        Kirigami.Icon {
            id: icon
            width: 48
            height: 48
            anchors.verticalCenter: parent.verticalCenter
            source: {
                if (root.iconSource && root.iconSource.length > 0) {
                    return root.iconSource
                }
                return "application-x-executable"
            }
        }

        // Text & Status
        Column {
            width: parent.width - icon.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3

            Row {
                width: parent.width
                spacing: Theme.s2

                Text {
                    id: nameLabel
                    text: root.name
                    textFormat: Text.PlainText
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                    elide: Text.ElideRight
                    maximumLineCount: 1
                    width: Math.min(implicitWidth, parent.width - (statusChip.visible ? statusChip.width + Theme.s2 : 0))
                }

                Chip {
                    id: statusChip
                    visible: root.isInstalled || root.actionState === "Installed" || root.actionState === "UpdateAvailable"
                    text: {
                        if (root.actionState === "UpdateAvailable") return qsTr("Update")
                        if (root.isInstalled || root.actionState === "Installed") return qsTr("Installiert")
                        return ""
                    }
                    variant: root.actionState === "UpdateAvailable" ? "kernel" : "neutral"
                }
            }

            Text {
                id: summaryLabel
                text: root.summary
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.textMuted
                elide: Text.ElideRight
                maximumLineCount: 2
                wrapMode: Text.WordWrap
                width: parent.width
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.clicked()
        }
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}

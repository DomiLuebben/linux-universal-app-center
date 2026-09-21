import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    property string pkgName: ""
    property string versionTransition: ""
    property string downloadSizeFormatted: ""
    property string repo: ""
    property bool isSecurity: false
    property bool isKernel: false
    property bool selected: true
    property bool showCheckbox: true

    signal toggled()

    implicitWidth: 600
    implicitHeight: 56
    radius: Theme.radiusControl
    color: mouseArea.containsMouse ? Theme.surfaceAlt : "transparent"

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { if (root.showCheckbox) root.toggled(); }
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.s4
        anchors.rightMargin: Theme.s4
        spacing: Theme.s3

        CheckBox {
            id: chk
            visible: root.showCheckbox
            anchors.verticalCenter: parent.verticalCenter
            checked: root.selected
            onToggled: root.toggled()
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            width: root.width - (chk.visible ? chk.width : 0) - sizeText.implicitWidth - chipRow.implicitWidth - Theme.s6

            Text {
                text: root.pkgName
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                text: root.versionTransition
                font.pixelSize: 12
                font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                font.features: ({ "tnum": 1 })
                color: Theme.textMuted
                elide: Text.ElideRight
                width: parent.width
            }
        }

        Row {
            id: chipRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s2

            Chip {
                visible: root.isSecurity
                text: qsTr("Sicherheit")
                variant: "security"
            }
            Chip {
                visible: root.isKernel
                text: qsTr("Kernel")
                variant: "kernel"
            }
            Chip {
                text: root.repo
                variant: "neutral"
            }
        }

        Text {
            id: sizeText
            anchors.verticalCenter: parent.verticalCenter
            text: root.downloadSizeFormatted
            font.pixelSize: 12
            font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
            font.features: ({ "tnum": 1 })
            color: Theme.textMuted
            horizontalAlignment: Text.AlignRight
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.separator
    }
}

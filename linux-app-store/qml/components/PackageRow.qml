import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    implicitHeight: 68
    radius: Theme.radiusControl
    color: mouseArea.containsMouse ? Theme.surfaceAlt : Theme.surface

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: { if (root.showCheckbox) root.toggled(); }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s4
        anchors.rightMargin: Theme.s4
        spacing: Theme.s3
        CheckBox {
            visible: root.showCheckbox
            checked: root.selected
            onToggled: root.toggled()
            Accessible.name: qsTr("%1 auswählen").arg(root.pkgName)
        }
        Rectangle {
            visible: !root.showCheckbox
            width: 30; height: 30; radius: 8
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
            Text { anchors.centerIn: parent; text: "↑"; color: Theme.accent; font.pixelSize: 18 }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 100
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: root.pkgName; font.pixelSize: 14; font.weight: Font.DemiBold
                color: Theme.text; elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: root.versionTransition; font.pixelSize: 12
                font.features: ({ "tnum": 1 })
                color: Theme.textMuted; elide: Text.ElideRight
            }
        }
        Chip { visible: root.isSecurity; text: qsTr("Sicherheit"); variant: "security" }
        Chip { visible: root.isKernel; text: qsTr("Kernel"); variant: "kernel" }
        Text {
            visible: root.width > 580 && root.repo.length > 0
            text: root.repo
            Layout.maximumWidth: 130
            elide: Text.ElideRight; font.pixelSize: 12; color: Theme.textMuted
        }
        Text {
            Layout.preferredWidth: 86
            text: root.downloadSizeFormatted; font.pixelSize: 12
            font.features: ({ "tnum": 1 })
            color: Theme.text; horizontalAlignment: Text.AlignRight
        }
    }

}

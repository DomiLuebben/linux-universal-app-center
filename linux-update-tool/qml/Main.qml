import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    visible: true
    width: 1100
    height: 760
    minimumWidth: 900
    minimumHeight: 640
    title: qsTr("Linux Update Tool")

    Rectangle {
        anchors.fill: parent
        color: palette.window

        Text {
            anchors.centerIn: parent
            text: qsTr("Linux Update Tool v1.0.0 (M0 Gerüst)")
            color: palette.windowText
            font.pixelSize: 20
        }
    }
}

import QtQuick
import QtQuick.Controls

Card {
    id: root
    sunken: true
    property var logModel: null
    property bool expanded: false

    implicitHeight: expanded ? 260 : 0
    visible: expanded
    clip: true

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s3
        spacing: Theme.s2

        Row {
            width: parent.width
            spacing: Theme.s2

            Text {
                text: qsTr("Live-Protokoll")
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }

            Item { width: Theme.s4; height: 1 }

            PrimaryButton {
                text: qsTr("Alle")
                variant: "quiet"
                onClicked: if (root.logModel) root.logModel.filterMode = 0
            }

            PrimaryButton {
                text: qsTr("Warnungen & Fehler")
                variant: "quiet"
                onClicked: if (root.logModel) root.logModel.filterMode = 1
            }

            PrimaryButton {
                text: qsTr("Nur Fehler")
                variant: "quiet"
                onClicked: if (root.logModel) root.logModel.filterMode = 2
            }

            Item { width: 50; height: 1 }

            PrimaryButton {
                text: qsTr("Kopieren")
                variant: "quiet"
                onClicked: {
                    if (root.logModel) {
                        // In Zwischenablage kopieren
                        var txt = root.logModel.copyAll()
                    }
                }
            }
        }

        ListView {
            id: logList
            width: parent.width
            height: parent.height - 40
            model: root.logModel
            clip: true
            spacing: 2

            delegate: Text {
                width: logList.width
                text: "[" + model.source + "] " + model.text
                font.pixelSize: 12
                font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                color: {
                    if (model.colorType === "negative") return Theme.negative
                    if (model.colorType === "neutral") return Theme.neutral
                    if (model.colorType === "textMuted") return Theme.textMuted
                    return Theme.textOnSunken
                }
                wrapMode: Text.WrapAnywhere
            }

            onCountChanged: {
                logList.positionViewAtEnd()
            }
        }
    }
}

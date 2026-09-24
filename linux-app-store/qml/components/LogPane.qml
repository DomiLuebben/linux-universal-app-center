import QtQuick
import QtQuick.Controls

Card {
    id: root
    sunken: true
    // Bewusst NICHT "logModel": gleicher Name wie die Kontext-Eigenschaft führt bei
    // "logModel: logModel" zur Selbstzuweisung (Bindung landet auf null).
    property var logSource: null
    property bool expanded: false

    implicitHeight: expanded ? 260 : 0
    visible: expanded
    clip: true

    // Unsichtbarer Helfer: TextEdit.copy() ist der Weg in die Zwischenablage,
    // ohne dafür ein eigenes C++-Clipboard-Objekt zu exportieren.
    // Bewusst ausserhalb der Column, damit er keinen Zeilenabstand belegt.
    TextEdit {
        id: clipHelper
        visible: false
        width: 0
        height: 0
    }

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
                onClicked: if (root.logSource) root.logSource.filterMode = 0
            }

            PrimaryButton {
                text: qsTr("Warnungen & Fehler")
                variant: "quiet"
                onClicked: if (root.logSource) root.logSource.filterMode = 1
            }

            PrimaryButton {
                text: qsTr("Nur Fehler")
                variant: "quiet"
                onClicked: if (root.logSource) root.logSource.filterMode = 2
            }

            Item { width: 50; height: 1 }

            PrimaryButton {
                text: qsTr("Kopieren")
                variant: "quiet"
                onClicked: {
                    if (!root.logSource) return;
                    var txt = root.logSource.copyAll();
                    if (txt.length > 0) {
                        clipHelper.text = txt;
                        clipHelper.selectAll();
                        clipHelper.copy();
                    }
                }
            }
        }

        ListView {
            id: logList
            width: parent.width
            height: parent.height - 40
            model: root.logSource
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

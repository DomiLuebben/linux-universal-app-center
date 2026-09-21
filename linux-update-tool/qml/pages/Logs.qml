import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        Row {
            width: parent.width
            spacing: Theme.s4

            Column {
                width: parent.width - 240
                spacing: 4

                Text {
                    text: qsTr("System- & Aktualisierungsprotokoll")
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: Theme.text
                }

                Text {
                    text: qsTr("%1 Protokolleinträge erfasst").arg(logModel.count)
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                }
            }

            PrimaryButton {
                text: qsTr("In Zwischenablage kopieren")
                variant: "quiet"
                anchors.verticalCenter: parent.verticalCenter
                onClicked: {
                    var txt = logModel.copyAll();
                    if (txt.length > 0) {
                        textEditDummy.text = txt;
                        textEditDummy.selectAll();
                        textEditDummy.copy();
                    }
                }
            }
        }

        TextEdit {
            id: textEditDummy
            visible: false
        }

        Card {
            width: parent.width
            height: parent.height - 70
            sunken: true

            LogPane {
                anchors.fill: parent
                anchors.margins: Theme.s2
                expanded: true
                logModel: logModel
            }
        }
    }
}

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

            Text {
                text: qsTr("Aktualisierungsverlauf")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("%1 Transaktionen").arg(historyModel.totalCount)
                font.pixelSize: 13
                font.features: ({ "tnum": 1 })
                color: Theme.textMuted
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        EmptyState {
            visible: historyModel.totalCount === 0
            title: qsTr("Keine früheren Einträge")
            message: qsTr("Durchgeführte Transaktionen werden hier chronologisch aufgeführt.")
        }

        ListView {
            id: historyList
            visible: historyModel.totalCount > 0
            width: parent.width
            height: parent.height - 70
            clip: true
            reuseItems: true
            spacing: 6
            model: historyModel

            delegate: Card {
                width: historyList.width
                height: 64

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s4

                    Text {
                        text: "🕒"
                        font.pixelSize: 16
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Column {
                        width: parent.width - 240
                        spacing: 2
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            text: model.command
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        Text {
                            text: qsTr("%1 Pakete geändert").arg(model.packagesAltered)
                            font.pixelSize: 12
                            font.features: ({ "tnum": 1 })
                            color: Theme.textMuted
                        }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s3

                        Text {
                            text: model.timestampFormatted
                            font.pixelSize: 12
                            font.features: ({ "tnum": 1 })
                            color: Theme.textMuted
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Chip {
                            text: model.source
                            variant: "neutral"
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Chip {
                            text: model.result
                            variant: model.result === "Success" ? "neutral" : "security"
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
    }
}

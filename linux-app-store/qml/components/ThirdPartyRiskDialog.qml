import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "."

Dialog {
    id: root

    property string sourceTitle: ""
    property string customTitle: ""
    property string riskText: ""
    property string actionButtonText: qsTr("Einschalten")
    property var callback: null

    signal confirmed()

    function openForSource(title, risk, buttonText, onConfirmCallback, customHeaderTitle) {
        sourceTitle = title
        riskText = risk
        actionButtonText = (buttonText && buttonText.length > 0) ? buttonText : qsTr("Einschalten")
        customTitle = (customHeaderTitle && customHeaderTitle.length > 0) ? customHeaderTitle : ""
        callback = onConfirmCallback
        open()
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(Overlay.overlay ? Overlay.overlay.width - Theme.s6 : 480, 520)
    closePolicy: Popup.CloseOnEscape

    padding: Theme.s5
    topPadding: 0
    bottomPadding: 0

    background: Rectangle {
        radius: Theme.radiusCard
        color: Theme.surface
        border.color: Theme.separator
        border.width: 1
    }

    header: Item {
        implicitWidth: root.width
        implicitHeight: 54

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.s5
            anchors.rightMargin: Theme.s5
            spacing: Theme.s3

            Kirigami.Icon {
                source: "dialog-warning"
                width: 24
                height: 24
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: root.customTitle.length > 0 ? root.customTitle : qsTr("%1 einschalten?").arg(root.sourceTitle)
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: Theme.text
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                width: parent.width - 40
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.separator
        }
    }

    contentItem: Column {
        id: contentCol
        spacing: Theme.s4
        topPadding: Theme.s4
        bottomPadding: Theme.s4

        Text {
            width: root.availableWidth
            text: root.riskText
            font.pixelSize: 13
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeight: 1.3
        }

        CheckBox {
            id: ackCheck
            text: qsTr("Ich habe das Risiko verstanden")
            contentItem: Text {
                text: ackCheck.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
                color: Theme.text
                verticalAlignment: Text.AlignVCenter
                leftPadding: ackCheck.indicator ? (ackCheck.indicator.width + Theme.s2) : Theme.s4
            }
        }
    }

    footer: Item {
        implicitWidth: root.width
        implicitHeight: 56

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Theme.separator
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.s5
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s3

            PrimaryButton {
                id: cancelButton
                text: qsTr("Abbrechen")
                variant: "quiet"
                focus: true
                onClicked: {
                    root.close()
                }
            }

            PrimaryButton {
                id: confirmButton
                text: root.actionButtonText
                variant: "primary"
                enabled: ackCheck.checked
                onClicked: {
                    root.close()
                    root.confirmed()
                    if (typeof root.callback === "function") {
                        root.callback()
                    }
                }
            }
        }
    }

    onOpened: {
        ackCheck.checked = false
        cancelButton.forceActiveFocus()
    }
}

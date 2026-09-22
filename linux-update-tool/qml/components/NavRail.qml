import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Rectangle {
    id: root
    property int currentIndex: 0
    signal pageSelected(int index)

    readonly property var items: [
        { key: "discover", label: qsTr("Entdecken"), icon: "compass", badgeKey: "none" },
        { key: "search", label: qsTr("Suchen"), icon: "edit-find", badgeKey: "none" },
        { key: "installed", label: qsTr("Installiert"), icon: "package-x-generic", badgeKey: "installed" },
        { key: "updates", label: qsTr("Updates"), icon: "system-software-update", badgeKey: "updates" },
        { key: "history", label: qsTr("Verlauf"), icon: "view-history", badgeKey: "none" },
        { key: "logs", label: qsTr("Protokoll"), icon: "utilities-terminal", badgeKey: "logs" },
        { key: "settings", label: qsTr("Einstellungen"), icon: "preferences-system", badgeKey: "none" }
    ]

    width: 220
    color: Theme.surface

    // Subtiler rechter Rand als Trenner
    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: Theme.separator
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s4
        spacing: Theme.s3

        // Kopfzeile mit App-Branding
        Row {
            width: parent.width
            height: 48
            spacing: Theme.s3

            Kirigami.Icon {
                source: "system-software-update"
                width: 32
                height: 32
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    text: qsTr("Linux Update Tool")
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    color: Theme.text
                }
                Text {
                    text: qsTr("Apps & Updates")
                    font.pixelSize: 11
                    color: Theme.textMuted
                }
            }
        }

        // Trennlinie
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.separator
        }

        // Navigationselemente
        Repeater {
            model: root.items
            delegate: Rectangle {
                width: parent.width
                height: 42
                radius: Theme.radiusControl
                color: {
                    if (index === root.currentIndex) {
                        return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                    }
                    return mouse.containsMouse ? Theme.surfaceAlt : "transparent"
                }

                // Aktiver Akzent-Indikator links
                Rectangle {
                    width: 3
                    height: 24
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    radius: 1.5
                    color: Theme.accent
                    visible: index === root.currentIndex
                }

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.s3
                    anchors.rightMargin: Theme.s3
                    spacing: Theme.s3

                    Kirigami.Icon {
                        source: modelData.icon
                        width: 20
                        height: 20
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: modelData.label
                        font.pixelSize: 13
                        font.weight: index === root.currentIndex ? Font.DemiBold : Font.Normal
                        color: index === root.currentIndex ? Theme.accent : Theme.text
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 60
                        elide: Text.ElideRight
                    }

                    // Badge Pill (z.B. Anzahl Updates)
                    Rectangle {
                        id: badge
                        property int badgeCount: {
                            if (modelData.badgeKey === "updates") return updatesModel.totalCount;
                            if (modelData.badgeKey === "installed") return installedModel.orphanCount;
                            if (modelData.badgeKey === "logs") return logModel.count;
                            return 0;
                        }
                        visible: badgeCount > 0
                        width: Math.max(20, badgeText.implicitWidth + 8)
                        height: 18
                        radius: 9
                        color: modelData.badgeKey === "updates" ? Theme.accent : Theme.surfaceSunken
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            id: badgeText
                            anchors.centerIn: parent
                            text: badge.badgeCount
                            font.pixelSize: 11
                            font.weight: Font.Bold
                            font.features: ({ "tnum": 1 })
                            color: modelData.badgeKey === "updates" ? Theme.onAccent : Theme.textMuted
                        }
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.pageSelected(index)
                }
            }
        }
    }
}

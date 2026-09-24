import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// „Verwalten" nach dem Vorbild des Ubuntu App Center: Aktualisierungen und
// installierte Apps an einer Stelle. Die bisherigen Seiten werden eingebettet,
// damit ihr Verhalten unverändert bleibt.
Item {
    id: root

    signal appSelected(string appKey)
    signal startUpgradeRequested()
    signal refreshRequested()
    signal tabSelected(int tab)
    signal openSettingsTabRequested(int tab)

    // 0 Aktualisierungen, 1 Apps, 2 Alle Pakete & Pflege
    property int tab: 0

    readonly property int updateCount: {
        var sum = (typeof updatesModel !== "undefined") ? updatesModel.totalCount : 0
        if (typeof aurUpdates !== "undefined" && aurUpdates.available) sum += aurUpdates.count
        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available) sum += pacstallUpdates.count
        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available) sum += flatpakUpdates.count
        if (typeof snapUpdates !== "undefined" && snapUpdates.available) sum += snapUpdates.count
        return sum
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Kopfzeile mit Reitern
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s6
            Layout.rightMargin: Theme.s6
            Layout.topMargin: Theme.s5
            spacing: Theme.s4

            Text {
                Layout.fillWidth: true
                text: qsTr("Verwalten")
                font.pixelSize: 26
                font.weight: Font.Bold
                color: Theme.text
            }

            Rectangle {
                Layout.preferredHeight: 36
                Layout.preferredWidth: tabRow.implicitWidth + 4
                radius: Theme.radiusControl
                color: Theme.surfaceSunken
                border.color: Theme.separator
                border.width: 1

                Row {
                    id: tabRow
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2

                    Repeater {
                        model: [
                            { label: root.updateCount > 0 ? qsTr("Aktualisierungen (%1)").arg(root.updateCount) : qsTr("Aktualisierungen") },
                            { label: qsTr("Apps") },
                            { label: qsTr("Alle Pakete & Pflege") }
                        ]
                        delegate: Rectangle {
                            width: segmentLabel.implicitWidth + Theme.s5 * 2
                            height: parent.height
                            radius: Theme.radiusControl - 1
                            color: root.tab === index ? Theme.surface : "transparent"
                            border.color: root.tab === index ? Theme.separator : "transparent"
                            border.width: 1

                            Accessible.role: Accessible.PageTab
                            Accessible.name: modelData.label

                            Text {
                                id: segmentLabel
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pixelSize: 13
                                font.weight: root.tab === index ? Font.DemiBold : Font.Normal
                                color: root.tab === index ? Theme.accent : Theme.textMuted
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.tabSelected(index)
                            }
                        }
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.tab === 0 ? 0 : 1

            Updates {
                compact: true
                onStartUpgradeRequested: root.startUpgradeRequested()
                onRefreshRequested: root.refreshRequested()
                onOpenSettingsTabRequested: function(t) { root.openSettingsTabRequested(t) }
            }

            Installed {
                compact: true
                selectedTab: root.tab === 2 ? 1 : 0
                onAppSelected: function(appKey) { root.appSelected(appKey) }
            }
        }
    }
}

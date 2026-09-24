import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Seitenleiste nach dem Vorbild des Ubuntu App Center: oben Entdecken und die
// Kategorien, unten Verwalten (mit Zahl der Aktualisierungen) und die
// selteneren Seiten. Farben kommen ausschließlich aus dem Plasma-Farbschema.
Rectangle {
    id: root

    // Seitenindizes: 0 Entdecken, 1 Ergebnisse/Kategorie, 2 Verwalten,
    // 3 Verlauf, 4 Protokoll, 5 Einstellungen
    property int currentIndex: 0
    property string currentCategory: ""
    signal pageSelected(int index)
    signal categorySelected(string category)

    readonly property var categories: [
        { label: qsTr("Internet"), icon: "applications-internet" },
        { label: qsTr("Büro"), icon: "applications-office" },
        { label: qsTr("Grafik"), icon: "applications-graphics" },
        { label: qsTr("Audio & Video"), icon: "applications-multimedia" },
        { label: qsTr("Spiele"), icon: "applications-games" },
        { label: qsTr("Entwicklung"), icon: "applications-development" },
        { label: qsTr("Bildung & Wissenschaft"), icon: "applications-science" },
        { label: qsTr("Werkzeuge"), icon: "applications-utilities" }
    ]

    readonly property var bottomItems: [
        { page: 3, label: qsTr("Verlauf"), icon: "view-history" },
        { page: 4, label: qsTr("Protokoll"), icon: "utilities-terminal" },
        { page: 5, label: qsTr("Einstellungen"), icon: "preferences-system" }
    ]

    readonly property int updateCount: {
        var sum = (typeof updatesModel !== "undefined") ? updatesModel.totalCount : 0
        if (typeof aurUpdates !== "undefined" && aurUpdates.available) sum += aurUpdates.count
        if (typeof pacstallUpdates !== "undefined" && pacstallUpdates.available) sum += pacstallUpdates.count
        if (typeof flatpakUpdates !== "undefined" && flatpakUpdates.available) sum += flatpakUpdates.count
        if (typeof snapUpdates !== "undefined" && snapUpdates.available) sum += snapUpdates.count
        return sum
    }

    width: 240
    color: Theme.surface

    Rectangle {
        width: 1
        height: parent.height
        anchors.right: parent.right
        color: Theme.separator
    }

    // Ein Eintrag der Leiste; groß für die Hauptziele, kompakt für die unteren.
    component NavEntry: Rectangle {
        id: entry
        property string label: ""
        property string icon: ""
        property bool active: false
        property bool compact: false
        property int badgeCount: 0
        signal activated()

        Layout.fillWidth: true
        implicitHeight: compact ? 34 : 38
        radius: Theme.radiusControl
        color: active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                      : (entryMouse.containsMouse ? Theme.surfaceAlt : "transparent")

        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: label
        Keys.onReturnPressed: entry.activated()
        Keys.onSpacePressed: entry.activated()

        Rectangle {
            visible: entry.activeFocus
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.s3
            anchors.rightMargin: Theme.s3
            spacing: Theme.s3

            Kirigami.Icon {
                source: entry.icon
                implicitWidth: entry.compact ? 16 : 18
                implicitHeight: entry.compact ? 16 : 18
            }

            Text {
                Layout.fillWidth: true
                text: entry.label
                font.pixelSize: entry.compact ? 12 : 13
                font.weight: entry.active ? Font.DemiBold : Font.Normal
                color: entry.active ? Theme.accent : (entry.compact ? Theme.textMuted : Theme.text)
                elide: Text.ElideRight
            }

            Rectangle {
                visible: entry.badgeCount > 0
                implicitWidth: Math.max(20, badgeLabel.implicitWidth + 8)
                implicitHeight: 18
                radius: 9
                color: Theme.accent

                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: entry.badgeCount > 99 ? "99+" : entry.badgeCount
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                    color: Theme.onAccent
                }
            }
        }

        MouseArea {
            id: entryMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: entry.activated()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.s4
        spacing: 2

        // Kopf mit Programmname
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            Layout.bottomMargin: Theme.s3
            spacing: Theme.s3

            // Aus der eingebetteten Ressource: das Themensymbol gibt es erst nach
            // der Installation, und aus dem Build-Ordner fehlte es sonst.
            Kirigami.Icon {
                source: "qrc:/LinuxAppStore/icons/org.linuxuniversalappcenter.svg"
                fallback: "system-software-update"
                implicitWidth: 30
                implicitHeight: 30
            }

            // Der volle Name passt in 240 px nicht in eine Zeile: fest auf zwei
            // Zeilen umbrechen, statt mitten im Wort abzuschneiden.
            Text {
                Layout.fillWidth: true
                text: qsTr("Linux Universal\nApp Center")
                Accessible.name: qsTr("Linux Universal App Center")
                font.pixelSize: 14
                font.weight: Font.DemiBold
                lineHeight: 1.05
                color: Theme.text
            }
        }

        NavEntry {
            objectName: "navDiscover"
            label: qsTr("Entdecken")
            icon: "compass"
            active: root.currentIndex === 0
            onActivated: root.pageSelected(0)
        }

        Text {
            Layout.topMargin: Theme.s4
            Layout.bottomMargin: 2
            Layout.leftMargin: Theme.s3
            text: qsTr("Kategorien")
            font.pixelSize: 11
            font.weight: Font.DemiBold
            font.letterSpacing: 0.5
            color: Theme.textMuted
        }

        Repeater {
            model: root.categories
            delegate: NavEntry {
                label: modelData.label
                icon: modelData.icon
                active: root.currentIndex === 1 && root.currentCategory === modelData.label
                onActivated: root.categorySelected(modelData.label)
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s2
            implicitHeight: 1
            color: Theme.separator
        }

        NavEntry {
            objectName: "navManage"
            label: qsTr("Verwalten")
            icon: "view-list-details"
            active: root.currentIndex === 2
            badgeCount: root.updateCount
            onActivated: root.pageSelected(2)
        }

        Repeater {
            model: root.bottomItems
            delegate: NavEntry {
                label: modelData.label
                icon: modelData.icon
                compact: true
                active: root.currentIndex === modelData.page
                onActivated: root.pageSelected(modelData.page)
            }
        }
    }
}

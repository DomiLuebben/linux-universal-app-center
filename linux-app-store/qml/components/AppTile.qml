import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Kachel für dichte App-Raster, angeordnet wie im Mac App Store: Symbol links,
// rechts Name und Kurztext, darunter der Knopf; keine Rahmen, nur eine feine
// Trennlinie. Der Knopf "Laden" installiert nicht still: er stößt die
// Planung an, die Vorschau mit Bestätigung folgt wie bisher.
Item {
    id: root

    property string appKey: ""
    property string name: ""
    property string summary: ""
    property string iconSource: ""
    property string actionState: "Available"
    property bool showSeparator: true

    signal clicked()

    implicitWidth: 260
    // Die Kurzbeschreibung bekommt immer Platz für zwei Zeilen: so sind alle
    // Kacheln gleich hoch und die Trennlinien einer Reihe fluchten.
    implicitHeight: Math.max(56, textColumn.implicitHeight) + Theme.s3 * 2 + 1

    FontMetrics {
        id: summaryMetrics
        font.pixelSize: 12
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: root.name.length > 0 ? root.name : qsTr("Anwendung")
    Accessible.description: root.summary
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()

    readonly property bool busyState: ["PreparingPlan", "AwaitingConfirmation", "Progressing", "Reconciling"].indexOf(root.actionState) >= 0
    readonly property bool canInstall: root.actionState === "Available" || root.actionState === "PartiallyInstalled"
    readonly property bool canOpen: root.actionState === "Installed" || root.actionState === "UpdateAvailable" || root.actionState === "MissingSource"

    Rectangle {
        anchors.fill: parent
        anchors.bottomMargin: 1
        radius: Theme.radiusControl
        color: tileMouse.containsMouse ? Theme.surfaceAlt : "transparent"
        border.color: root.activeFocus ? Theme.accent : "transparent"
        border.width: root.activeFocus ? 2 : 0
    }

    MouseArea {
        id: tileMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            root.forceActiveFocus()
            root.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s2
        anchors.rightMargin: Theme.s2
        anchors.topMargin: Theme.s3
        anchors.bottomMargin: Theme.s3
        spacing: Theme.s3

        Kirigami.Icon {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 56
            implicitHeight: 56
            source: {
                if (root.iconSource && root.iconSource.length > 0 && typeof mediaCache !== "undefined") {
                    mediaCache.revision
                    return mediaCache.source(root.iconSource) || "application-x-executable"
                }
                return "application-x-executable"
            }
            fallback: "application-x-executable"
        }

        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.name
                textFormat: Text.PlainText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                Layout.fillWidth: true
                text: root.summary
                textFormat: Text.PlainText
                font.pixelSize: 12
                color: Theme.textMuted
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                verticalAlignment: Text.AlignTop
                Layout.preferredHeight: Math.ceil(summaryMetrics.lineSpacing * 2)
            }

            // Knopf wie "Laden" im Mac App Store: klein, pillenförmig, unter dem Text.
            Button {
                id: actionButton
                objectName: "tileActionButton"
                Layout.topMargin: 4
                visible: root.canInstall || root.canOpen || root.busyState || root.actionState === "InstalledNoLaunch"
                enabled: {
                    if (root.canOpen) return true
                    if (root.canInstall) return typeof daemonClient === "undefined" || !daemonClient.isBusy
                    return false
                }
                text: {
                    if (root.busyState) return qsTr("…")
                    if (root.canOpen) return qsTr("Öffnen")
                    if (root.actionState === "InstalledNoLaunch") return qsTr("Installiert")
                    return qsTr("Laden")
                }
                padding: 0
                leftPadding: Theme.s4
                rightPadding: Theme.s4
                implicitHeight: 24

                contentItem: Text {
                    text: actionButton.text
                    font.pixelSize: 12
                    font.weight: Font.Bold
                    color: actionButton.enabled ? Theme.accent : Theme.textMuted
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: height / 2
                    color: actionButton.hovered && actionButton.enabled
                           ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.24)
                           : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, actionButton.enabled ? 0.14 : 0.06)
                }

                onClicked: {
                    if (root.canOpen) appStore.launchApp(root.appKey)
                    else if (root.canInstall) appStore.requestInstall(root.appKey)
                }
            }
        }
    }

    // Trennlinie ab der Textspalte, wie im Mac App Store
    Rectangle {
        visible: root.showSeparator
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.s2 + 56 + Theme.s3
        height: 1
        color: Theme.separator
    }
}

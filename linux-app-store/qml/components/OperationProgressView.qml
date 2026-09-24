import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Inhalt des gemeinsamen Fortschrittsfensters. Liest ausschließlich
// operationMonitor; ob gerade das System, Flatpak, Snap oder das AUR
// aktualisiert wird, sieht man nur an Titel und Symbol – der Balken ist derselbe.
Item {
    id: root

    signal backgroundRequested()
    signal doneRequested()

    readonly property var monitor: (typeof operationMonitor !== "undefined") ? operationMonitor : null
    readonly property bool isRunning: monitor ? monitor.running : false
    readonly property int outcome: monitor ? monitor.result : 0
    readonly property bool reviewing: monitor ? monitor.awaitingReview : false
    readonly property var reviewItems: monitor ? monitor.reviewItems : []
    property int reviewIndex: 0
    property alias logExpanded: logPane.expanded
    property var activeQuestion: ({ id: "", kind: "", payload: ({}) })

    implicitWidth: 720
    implicitHeight: layout.implicitHeight + Theme.s5 * 2

    function sourceIcon(source) {
        switch (source) {
        case "flatpak": return "flatpak-discover"
        case "snap": return "snap-package"
        case "aur": return "package-x-generic"
        case "pacstall": return "package-x-generic"
        case "store": return "plasmadiscover"
        default: return "system-software-update"
        }
    }

    function sourceLabel(source) {
        switch (source) {
        case "flatpak": return qsTr("Flatpak")
        case "snap": return qsTr("Snap")
        case "aur": return qsTr("AUR")
        case "pacstall": return qsTr("Pacstall")
        case "store": return qsTr("App-Store")
        default: return qsTr("Systempakete")
        }
    }

    function headline() {
        if (!root.monitor) return ""
        if (root.reviewing) return qsTr("Änderungen prüfen")
        if (root.outcome === 1) return qsTr("Fertig")
        if (root.outcome === 2) return qsTr("Fehlgeschlagen")
        if (root.outcome === 3) return qsTr("Abgebrochen")
        return root.monitor.title
    }

    onReviewingChanged: if (reviewing) reviewIndex = 0
    onOutcomeChanged: if (outcome === 2) logPane.expanded = true

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s4

        // Kopfzeile
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s4

            Kirigami.Icon {
                source: root.outcome === 2 ? "dialog-error"
                      : root.outcome === 1 ? "dialog-ok-apply"
                      : root.sourceIcon(root.monitor ? root.monitor.source : "")
                fallback: "system-software-update"
                implicitWidth: 40
                implicitHeight: 40
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: root.headline()
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: root.outcome === 2 ? Theme.negative : Theme.text
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }

                Text {
                    Layout.fillWidth: true
                    text: {
                        if (!root.monitor) return ""
                        var parts = [root.sourceLabel(root.monitor.source)]
                        if (root.outcome !== 0 || root.reviewing) parts.push(root.monitor.title)
                        else if (root.monitor.stepCount > 1)
                            parts.push(qsTr("Paket %1 von %2").arg(Math.max(1, root.monitor.stepIndex)).arg(root.monitor.stepCount))
                        return parts.join(" · ")
                    }
                    font.pixelSize: 13
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
        }

        // Gemeinsamer Fortschrittsbalken
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s2
            visible: !root.reviewing

            Rectangle {
                id: track
                Layout.fillWidth: true
                implicitHeight: 10
                radius: 5
                color: Theme.surfaceSunken
                clip: true
                visible: !(root.monitor && root.monitor.indeterminate && root.isRunning)

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    radius: 5
                    width: parent.width * Math.min(1.0, Math.max(0.0, root.monitor ? root.monitor.progress : 0))
                    color: root.outcome === 2 ? Theme.negative : Theme.accent

                    Behavior on width {
                        enabled: Theme.motionScale > 0
                        NumberAnimation { duration: Theme.durationBase * Theme.motionScale }
                    }
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: root.monitor && root.monitor.indeterminate && root.isRunning
                indeterminate: true
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: {
                        if (!root.monitor) return ""
                        if (root.monitor.indeterminate && root.isRunning) return qsTr("In Bearbeitung …")
                        return Math.round(root.monitor.progress * 100) + " %"
                    }
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                    color: Theme.accent
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: (root.monitor && root.isRunning) ? root.monitor.etaString : ""
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                }
            }

            Text {
                Layout.fillWidth: true
                text: {
                    if (!root.monitor) return ""
                    if (root.outcome !== 0) return root.monitor.resultMessage
                    return root.monitor.detail.length > 0 ? root.monitor.detail : qsTr("Wird vorbereitet \u2026")
                }
                font.pixelSize: 13
                color: root.outcome === 2 ? Theme.negative : Theme.text
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }

        // AUR: alle Änderungen in EINER Ansicht, dann ein Bestätigen für alles.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.s2
            visible: root.reviewing

            Text {
                Layout.fillWidth: true
                text: root.monitor && root.monitor.source === "pacstall"
                    ? qsTr("Pacscripts werden mit Administratorrechten ausgeführt. Bitte die Anleitung ansehen, bevor installiert wird.")
                    : qsTr("Ein AUR-Bau führt fremden Code mit deinen Rechten aus. Bitte die Änderungen ansehen, bevor gebaut wird.")
                font.pixelSize: 12
                color: Theme.textMuted
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Theme.s3

                ListView {
                    id: reviewList
                    Layout.preferredWidth: 210
                    Layout.fillHeight: true
                    clip: true
                    model: root.reviewItems
                    currentIndex: root.reviewIndex
                    spacing: 2

                    delegate: ItemDelegate {
                        width: reviewList.width
                        highlighted: index === root.reviewIndex
                        onClicked: root.reviewIndex = index

                        contentItem: ColumnLayout {
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: modelData.names.join(", ")
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                                color: modelData.error.length > 0 ? Theme.negative : Theme.text
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.error.length > 0 ? modelData.error
                                    : (modelData.fromVersion + " → " + modelData.toVersion
                                       + (modelData.firstBuild ? qsTr(" · neu") : ""))
                                font.pixelSize: 11
                                color: Theme.textMuted
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                            }
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        readOnly: true
                        wrapMode: TextArea.NoWrap
                        textFormat: TextEdit.PlainText
                        font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                        font.pixelSize: 12
                        color: Theme.textOnSunken
                        text: {
                            var items = root.reviewItems
                            if (!items || items.length === 0 || root.reviewIndex >= items.length) return ""
                            var item = items[root.reviewIndex]
                            if (item.error.length > 0) return item.error
                            var header = item.firstBuild
                                ? qsTr("# Erster Bau mit dem Linux Universal App Center: vollständiger PKGBUILD\n\n")
                                : qsTr("# Änderungen seit dem letzten Bau\n\n")
                            return header + item.review
                        }
                    }
                }
            }
        }

        // Rückfragen des Paketmanagers (etwa Schlüssel oder Konflikte)
        QuestionCard {
            Layout.fillWidth: true
            visible: root.activeQuestion.id.length > 0 && !root.reviewing
            questionId: root.activeQuestion.id
            questionKind: root.activeQuestion.kind
            payload: root.activeQuestion.payload
            onAnswered: function(id, answer) {
                daemonClient.answerQuestion(id, answer)
                root.activeQuestion = { id: "", kind: "", payload: ({}) }
            }
        }

        Item { Layout.fillHeight: true; visible: !root.reviewing && !logPane.expanded }

        LogPane {
            id: logPane
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: expanded && !root.reviewing
            logSource: logModel
        }

        // Knöpfe
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3

            PrimaryButton {
                visible: !root.reviewing
                text: logPane.expanded ? qsTr("▲ Protokoll ausblenden") : qsTr("▼ Protokoll anzeigen")
                variant: "quiet"
                onClicked: logPane.expanded = !logPane.expanded
            }

            Item { Layout.fillWidth: true }

            // Prüfansicht
            PrimaryButton {
                visible: root.reviewing
                text: qsTr("Abbrechen")
                variant: "quiet"
                onClicked: root.monitor.rejectReview()
            }
            PrimaryButton {
                objectName: "reviewConfirmButton"
                visible: root.reviewing
                text: qsTr("Bauen und installieren")
                variant: "primary"
                onClicked: root.monitor.confirmReview()
            }

            // Während des Vorgangs
            PrimaryButton {
                visible: root.isRunning && !root.reviewing
                text: qsTr("Abbrechen")
                variant: "destructive"
                enabled: root.monitor ? root.monitor.cancellable : false
                onClicked: root.monitor.cancel()
            }
            PrimaryButton {
                objectName: "backgroundButton"
                visible: root.isRunning && !root.reviewing
                text: qsTr("Im Hintergrund")
                variant: "quiet"
                onClicked: root.backgroundRequested()
            }

            // Nach dem Vorgang
            PrimaryButton {
                objectName: "doneButton"
                visible: !root.isRunning
                text: root.outcome === 1 ? qsTr("Fertig") : qsTr("Schließen")
                variant: "primary"
                onClicked: root.doneRequested()
            }
        }
    }

    Connections {
        target: typeof daemonClient !== "undefined" ? daemonClient : null
        function onQuestionPrompt(q) {
            root.activeQuestion = { id: q.id, kind: q.kind, payload: q.payload }
        }
    }
}

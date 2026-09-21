import QtQuick
import QtQuick.Controls
import "../components"

Item {
    id: root

    signal cancelRequested()
    signal finishAcknowledged()

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s5

        // Titelzeile
        Text {
            text: progressModel.currentPhase === 9 ? qsTr("Aktualisierung abgeschlossen") : qsTr("Aktualisierung läuft")
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: Theme.text
        }

        // Stepper oben
        PhaseStepper {
            width: parent.width
            currentPhaseIndex: {
                var p = progressModel.currentPhase;
                if (p <= 2) return 0; // Refresh / Resolve
                if (p <= 4) return 1; // Download / Verify
                if (p === 6) return 2; // Commit
                if (p === 7) return 3; // PostTransaction
                if (p === 8) return 4; // Cleanup
                return 5; // Finished
            }
        }

        // Hauptbalken
        Column {
            width: parent.width
            spacing: Theme.s2

            SegmentedProgress {
                width: parent.width
                totalProgress: progressModel.totalProgress
            }

            Row {
                width: parent.width
                Text {
                    text: Math.round(progressModel.totalProgress * 100) + " %"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                    color: Theme.accent
                }

                Item { width: 20; height: 1 }

                Text {
                    anchors.right: parent.right
                    text: progressModel.etaString
                    font.pixelSize: 13
                    font.features: ({ "tnum": 1 })
                    color: Theme.textMuted
                }
            }
        }

        // Aktive Operation Karte
        Card {
            width: parent.width
            height: 90
            visible: progressModel.currentPhase !== 9

            Column {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s2

                Row {
                    width: parent.width
                    Text {
                        text: progressModel.currentItemName.length > 0 ? progressModel.currentItemName : progressModel.phaseLabel
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: Theme.text
                        elide: Text.ElideRight
                        width: parent.width - 150
                    }

                    Text {
                        anchors.right: parent.right
                        visible: progressModel.downloadSpeed > 0
                        text: (progressModel.downloadSpeed / (1024 * 1024)).toFixed(1) + " MB/s"
                        font.family: "JetBrains Mono, Hack, Noto Sans Mono, monospace"
                        font.features: ({ "tnum": 1 })
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }

                // Sub-Balken für das aktive Item
                Rectangle {
                    width: parent.width
                    height: 6
                    radius: 3
                    color: Theme.surfaceSunken
                    clip: true

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(1.0, Math.max(0.0, progressModel.currentItemProgress))
                        radius: 3
                        color: Theme.accent
                    }
                }
            }
        }

        // Hinweis bei Kernel-Updates in PostTransaction
        Text {
            visible: progressModel.currentPhase === 7 && progressModel.hasKernelUpdate
            text: progressModel.postTransactionNote
            font.pixelSize: 13
            color: Theme.neutral
            wrapMode: Text.Wrap
            width: parent.width
        }

        // TaskList während PostTransaction
        TaskList {
            visible: progressModel.currentPhase === 7 && progressModel.scriptletTasks.length > 0
            width: parent.width
            tasks: progressModel.scriptletTasks
        }

        // Frage-Karte
        QuestionCard {
            id: qCard
            visible: activeQuestion.id.length > 0
            width: parent.width
            questionId: activeQuestion.id
            questionKind: activeQuestion.kind
            payload: activeQuestion.payload
            onAnswered: function(id, answer) {
                daemonClient.answerQuestion(id, answer);
                activeQuestion = { id: "", kind: "", payload: ({}) };
            }
        }

        // Steuerleiste
        Row {
            width: parent.width
            spacing: Theme.s3

            PrimaryButton {
                text: logPane.expanded ? qsTr("▲ Details ausblenden") : qsTr("▼ Details anzeigen")
                variant: "quiet"
                onClicked: logPane.expanded = !logPane.expanded
            }

            Item { width: 50; height: 1 }

            PrimaryButton {
                text: qsTr("Abbrechen")
                variant: "destructive"
                visible: progressModel.currentPhase !== 9
                enabled: progressModel.isCancellable
                onClicked: root.cancelRequested()
            }

            PrimaryButton {
                text: qsTr("Fertig")
                variant: "primary"
                visible: progressModel.currentPhase === 9
                onClicked: root.finishAcknowledged()
            }
        }

        // LogPane
        LogPane {
            id: logPane
            width: parent.width
            logModel: logModel
        }
    }

    property var activeQuestion: ({ id: "", kind: "", payload: ({}) })

    Connections {
        target: daemonClient
        function onQuestionPrompt(q) {
            root.activeQuestion = { id: q.id, kind: q.kind, payload: q.payload };
        }
    }
}

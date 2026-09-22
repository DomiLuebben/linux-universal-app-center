import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    signal cancelRequested()
    signal finishAcknowledged()

    ScrollView {
        id: scroller
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
    Column {
        width: scroller.availableWidth - Theme.s6 * 2
        x: Theme.s6
        topPadding: Theme.s6
        bottomPadding: Theme.s6
        spacing: Theme.s5

        // Titelzeile
        Text {
            text: {
                if (progressModel.currentPhase === 9) return qsTr("Aktualisierung abgeschlossen");
                if (progressModel.currentPhase === 10) return qsTr("Aktualisierung fehlgeschlagen");
                if (progressModel.currentPhase === 11) return qsTr("Aktualisierung abgebrochen");
                return qsTr("Aktualisierung läuft");
            }
            font.pixelSize: 22
            font.weight: Font.DemiBold
            color: progressModel.currentPhase === 10 ? Theme.negative : Theme.text
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
                visible: !progressModel.isIndeterminate
                width: parent.width
                totalProgress: progressModel.totalProgress
            }

            ProgressBar {
                width: parent.width
                visible: progressModel.isIndeterminate
                indeterminate: true
            }

            Row {
                width: parent.width
                Text {
                    text: progressModel.isIndeterminate ? qsTr("In Bearbeitung …") : Math.round(progressModel.totalProgress * 100) + " %"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.features: ({ "tnum": 1 })
                    color: Theme.accent
                }

                Item { width: 20; height: 1 }

                Text {
                    anchors.right: parent.right
                    text: progressModel.isIndeterminate ? "" : progressModel.etaString
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

        // Fehler-Karte bei Fehlgeschlagener Transaktion
        Card {
            visible: progressModel.currentPhase === 10
            width: parent.width
            height: 80
            color: Theme.surface

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: Theme.negative
                border.width: 1
                radius: Theme.radiusCard
            }

            Row {
                anchors.fill: parent
                anchors.margins: Theme.s4
                spacing: Theme.s4

                Kirigami.Icon {
                    source: "dialog-error"
                    width: 32
                    height: 32
                    anchors.verticalCenter: parent.verticalCenter
                }

                Column {
                    width: parent.width - 60
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Text {
                        text: qsTr("Fehler bei der Aktualisierung")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.negative
                    }

                    Text {
                        text: progressModel.phaseLabel.length > 0 ? progressModel.phaseLabel : qsTr("Ein Fehler ist aufgetreten. Details siehe Protokoll unten.")
                        font.pixelSize: 13
                        color: Theme.text
                        wrapMode: Text.Wrap
                        width: parent.width
                    }
                }
            }
        }

        // Steuerleiste
        Row {
            width: parent.width
            spacing: Theme.s3

            PrimaryButton {
                text: logPane.expanded ? qsTr("▲ Protokoll ausblenden") : qsTr("▼ Protokoll anzeigen")
                variant: "quiet"
                onClicked: logPane.expanded = !logPane.expanded
            }

            PrimaryButton {
                text: qsTr("Protokoll kopieren")
                variant: "quiet"
                visible: progressModel.currentPhase === 10 || logPane.expanded
                onClicked: {
                    var txt = logModel.copyAll();
                    if (txt.length > 0) {
                        clipEdit.text = txt;
                        clipEdit.selectAll();
                        clipEdit.copy();
                    }
                }
            }

            Item { width: 40; height: 1 }

            PrimaryButton {
                text: qsTr("Abbrechen")
                variant: "destructive"
                visible: progressModel.currentPhase < 9
                enabled: progressModel.isCancellable
                onClicked: root.cancelRequested()
            }

            PrimaryButton {
                text: qsTr("Fertig")
                variant: "primary"
                visible: progressModel.currentPhase === 9
                onClicked: root.finishAcknowledged()
            }

            PrimaryButton {
                text: qsTr("Zurück zur Übersicht")
                variant: "primary"
                visible: progressModel.currentPhase >= 10
                onClicked: root.finishAcknowledged()
            }
        }

        TextEdit {
            id: clipEdit
            visible: false
        }

        // LogPane
        LogPane {
            id: logPane
            width: parent.width
            logSource: logModel
        }
    }

    }

    property var activeQuestion: ({ id: "", kind: "", payload: ({}) })

    Connections {
        target: daemonClient
        function onQuestionPrompt(q) {
            root.activeQuestion = { id: q.id, kind: q.kind, payload: q.payload };
        }
    }

    Connections {
        target: progressModel
        function onCurrentPhaseChanged() {
            if (progressModel.currentPhase >= 10) {
                logPane.expanded = true;
            }
        }
    }
}

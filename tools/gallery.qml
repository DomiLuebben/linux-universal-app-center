import QtQuick
import QtQuick.Controls
import "../linux-app-store/qml/components"

ApplicationWindow {
    id: root
    visible: true
    width: 900
    height: 700
    title: qsTr("Linux Update Tool — Komponenten-Galerie")
    color: Theme.bg

    ScrollView {
        anchors.fill: parent
        contentWidth: col.width
        contentHeight: col.height
        clip: true

        Column {
            id: col
            width: root.width - 48
            x: 24
            y: 24
            spacing: Theme.s5

            Text {
                text: qsTr("Komponenten-Galerie (Isolierte UI-Prüfung)")
                font.pixelSize: 20
                font.weight: Font.Bold
                color: Theme.text
            }

            // 1. Buttons
            Card {
                width: parent.width
                height: 80
                Row {
                    anchors.centerIn: parent
                    spacing: Theme.s4
                    PrimaryButton { text: qsTr("Primär"); variant: "primary" }
                    PrimaryButton { text: qsTr("Destruktiv"); variant: "destructive" }
                    PrimaryButton { text: qsTr("Leise"); variant: "quiet" }
                    PrimaryButton { text: qsTr("Deaktiviert"); enabled: false }
                }
            }

            // 2. Chips
            Card {
                width: parent.width
                height: 60
                Row {
                    anchors.centerIn: parent
                    spacing: Theme.s3
                    Chip { text: qsTr("Standard"); variant: "neutral" }
                    Chip { text: qsTr("Sicherheit"); variant: "security" }
                    Chip { text: qsTr("Kernel"); variant: "kernel" }
                    Chip { text: qsTr("Gehalten"); variant: "held" }
                }
            }

            // 3. StatCard
            Row {
                spacing: Theme.s4
                StatCard { value: "23"; label: qsTr("Aktualisierungen"); secondary: "412 MB" }
                StatCard { value: "0"; label: qsTr("Sicherheitslücken"); secondary: qsTr("System sicher") }
            }

            // 4. SegmentedProgress
            Card {
                width: parent.width
                height: 60
                SegmentedProgress {
                    anchors.centerIn: parent
                    width: parent.width - 48
                    totalProgress: 0.68
                }
            }

            // 5. PhaseStepper
            Card {
                width: parent.width
                height: 60
                PhaseStepper {
                    anchors.centerIn: parent
                    currentPhaseIndex: 2
                }
            }

            // 6. PackageRow
            Card {
                width: parent.width
                height: 140
                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.s2
                    PackageRow {
                        width: parent.width
                        pkgName: "kernel-core"
                        versionTransition: "6.17.3 → 6.17.4"
                        downloadSizeFormatted: "38 MB"
                        repo: "updates"
                        isSecurity: true
                        isKernel: true
                    }
                    PackageRow {
                        width: parent.width
                        pkgName: "firefox"
                        versionTransition: "131.0 → 131.0.1"
                        downloadSizeFormatted: "65 MB"
                        repo: "updates"
                        isSecurity: false
                        isKernel: false
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "../components"

Item {
    id: root

    property int selectedTab: (typeof initialSettingsTab !== "undefined") ? initialSettingsTab : 0
    property bool showAddCustomCard: false

    function getRecommendedPresets() {
        if (!repositoriesModel || !repositoriesModel.presets) return []
        var res = []
        for (var i = 0; i < repositoriesModel.presets.length; ++i) {
            var p = repositoriesModel.presets[i]
            if (!p.thirdParty) res.push(p)
        }
        return res
    }

    function getThirdPartyPresets() {
        if (!repositoriesModel || !repositoriesModel.presets) return []
        var res = []
        for (var i = 0; i < repositoriesModel.presets.length; ++i) {
            var p = repositoriesModel.presets[i]
            if (p.thirdParty && p.id !== "pacstall") res.push(p)
        }
        return res
    }

    ThirdPartyRiskDialog {
        id: riskDialog
    }

    Component.onCompleted: {
        var riskSource = (typeof screenshotRiskSource !== "undefined") ? screenshotRiskSource : ""
        if (riskSource === "aur") {
            riskDialog.openForSource(
                qsTr("Arch User Repository (AUR)"),
                qsTr("Pakete im Arch User Repository werden von Nutzern eingestellt und von Arch Linux nicht geprüft. Ein PKGBUILD kann beim Bauen beliebige Befehle ausführen, das fertige Paket wird mit Administratorrechten installiert. Der Store zeigt dir vor jedem Bau die Änderungen, beurteilen musst du sie selbst. AUR-Pakete können nach Systemaktualisierungen brechen."),
                qsTr("Einschalten"),
                null
            )
        } else if (riskSource === "fedora") {
            riskDialog.openForSource(
                qsTr("RPM Fusion (Free)"),
                qsTr("Diese Paketquelle wird nicht vom Fedora-Projekt betrieben. Ihre Pakete werden mit ihrem eigenen Schlüssel signiert und können Fedora-Pakete ersetzen. Du vertraust den Betreibern der Quelle."),
                qsTr("Hinzufügen"),
                null
            )
        } else if (riskSource === "pacstall") {
            riskDialog.openForSource(
                qsTr("Pacstall"),
                qsTr("Pacstall installiert Programme nach Bauanleitungen (Pacscripts) aus einem von Nutzern gepflegten Verzeichnis, nicht aus Debian oder Ubuntu. Pacscripts werden mit Administratorrechten ausgeführt. Der Store zeigt dir jedes Pacscript vor der Ausführung, beurteilen musst du es selbst."),
                qsTr("Einrichten …"),
                null,
                qsTr("Pacstall einrichten?")
            )
        }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.s6
        spacing: Theme.s4

        // Kopfzeile mit Titel und Segment-Umschalter
        Item {
            width: parent.width
            height: 36

            Text {
                text: qsTr("Einstellungen")
                font.pixelSize: 22
                font.weight: Font.DemiBold
                color: Theme.text
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }

            // Segmentierter Umschalter: Allgemein / Paketquellen
            Rectangle {
                width: 320
                height: 36
                radius: Theme.radiusControl
                color: Theme.surfaceSunken
                border.color: Theme.separator
                border.width: 1
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter

                Row {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 2

                    // Reiter 1: Allgemein
                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: root.selectedTab === 0 ? Theme.surface : "transparent"
                        border.color: root.selectedTab === 0 ? Theme.separator : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Allgemein")
                            font.pixelSize: 13
                            font.weight: root.selectedTab === 0 ? Font.DemiBold : Font.Normal
                            color: root.selectedTab === 0 ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedTab = 0
                        }
                    }

                    // Reiter 2: Paketquellen
                    Rectangle {
                        width: (parent.width - 2) / 2
                        height: parent.height
                        radius: Theme.radiusControl - 1
                        color: root.selectedTab === 1 ? Theme.surface : "transparent"
                        border.color: root.selectedTab === 1 ? Theme.separator : "transparent"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Paketquellen")
                            font.pixelSize: 13
                            font.weight: root.selectedTab === 1 ? Font.DemiBold : Font.Normal
                            color: root.selectedTab === 1 ? Theme.accent : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.selectedTab = 1
                        }
                    }
                }
            }
        }

        // Trennlinie
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.separator
        }

        // =====================================================================
        // Reiter 0: Allgemein
        // =====================================================================
        Column {
            visible: root.selectedTab === 0
            width: parent.width
            spacing: Theme.s4

            Card {
                width: parent.width
                height: 90

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2

                    Text {
                        text: qsTr("Erscheinungsbild")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Text {
                        text: qsTr("Folgt den Systemfarben von KDE Plasma / Breeze.")
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }
            }

            Card {
                width: parent.width
                height: 90

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2

                    Text {
                        text: qsTr("Paketmanager-Backend")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Text {
                        text: qsTr("Nativer Zugriff über %1 (ohne PackageKit).").arg(repositoriesModel ? repositoriesModel.backendName : qsTr("Systemdienst"))
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }
            }

            Card {
                width: parent.width
                height: 90

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.s4
                    spacing: Theme.s2

                    Text {
                        text: qsTr("Systeminformationen")
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: Theme.text
                    }

                    Text {
                        text: qsTr("Distribution: %1 · Protokollversion: %2").arg(repositoriesModel ? repositoriesModel.distroName : qsTr("Linux")).arg(daemonClient ? daemonClient.protocolVersion : 1)
                        font.pixelSize: 13
                        color: Theme.textMuted
                    }
                }
            }
        }

        // =====================================================================
        // Reiter 1: Paketquellen (Repositories)
        // =====================================================================
        ScrollView {
            id: repoScrollView
            visible: root.selectedTab === 1
            width: parent.width
            height: parent.height - 48
            clip: true

            Column {
                width: repoScrollView.width - Theme.s4
                spacing: Theme.s5

                // Aktions- und Statusleiste
                Item {
                    width: parent.width
                    height: 38

                    Row {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s3

                        Kirigami.Icon {
                            source: "network-server"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: qsTr("%1 · %2").arg(repositoriesModel ? repositoriesModel.distroName : "").arg(repositoriesModel ? repositoriesModel.backendName : "")
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.text
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.s3

                        PrimaryButton {
                            text: qsTr("Aktualisieren")
                            iconName: "view-refresh"
                            variant: "quiet"
                            onClicked: repositoriesModel.refresh()
                        }

                        PrimaryButton {
                            text: root.showAddCustomCard ? qsTr("Abbrechen") : qsTr("+ Eigene Quelle")
                            variant: "primary"
                            onClicked: root.showAddCustomCard = !root.showAddCustomCard
                        }
                    }
                }

                // Fehlermeldung falls vorhanden
                Card {
                    visible: repositoriesModel && repositoriesModel.errorMessage.length > 0
                    width: parent.width
                    height: 50
                    border.color: Theme.negative

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.s3
                        spacing: Theme.s3

                        Kirigami.Icon {
                            source: "dialog-error"
                            width: 20
                            height: 20
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: repositoriesModel ? repositoriesModel.errorMessage : ""
                            color: Theme.negative
                            font.pixelSize: 13
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            width: parent.width - 40
                        }
                    }
                }

                // Inline-Card: Benutzerdefinierte Paketquelle hinzufügen
                Card {
                    visible: root.showAddCustomCard
                    width: parent.width
                    height: 180
                    raised: true

                    Column {
                        anchors.fill: parent
                        anchors.margins: Theme.s4
                        spacing: Theme.s3

                        Text {
                            text: qsTr("Benutzerdefinierte Paketquelle hinzufügen")
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }

                        Row {
                            width: parent.width
                            spacing: Theme.s3

                            TextField {
                                id: customIdInput
                                width: (parent.width - Theme.s3) * 0.35
                                placeholderText: qsTr("ID / Bezeichner (z.B. my-repo)")
                                font.pixelSize: 13
                                color: Theme.text

                                background: Rectangle {
                                    radius: Theme.radiusControl
                                    color: Theme.surfaceSunken
                                    border.color: customIdInput.activeFocus ? Theme.accent : Theme.separator
                                    border.width: customIdInput.activeFocus ? 2 : 1
                                }
                            }

                            TextField {
                                id: customUrlInput
                                width: (parent.width - Theme.s3) * 0.65
                                placeholderText: qsTr("Server-URL oder Pfad")
                                font.pixelSize: 13
                                color: Theme.text

                                background: Rectangle {
                                    radius: Theme.radiusControl
                                    color: Theme.surfaceSunken
                                    border.color: customUrlInput.activeFocus ? Theme.accent : Theme.separator
                                    border.width: customUrlInput.activeFocus ? 2 : 1
                                }
                            }
                        }

                        Row {
                            anchors.right: parent.right
                            spacing: Theme.s3

                            PrimaryButton {
                                text: qsTr("Abbrechen")
                                variant: "quiet"
                                onClicked: root.showAddCustomCard = false
                            }

                            PrimaryButton {
                                text: qsTr("Quelle hinzufügen")
                                variant: "primary"
                                enabled: customIdInput.text.trim().length > 0 && customUrlInput.text.trim().length > 0
                                onClicked: {
                                    repositoriesModel.addCustom(customIdInput.text.trim(), customIdInput.text.trim(), customUrlInput.text.trim())
                                    customIdInput.text = ""
                                    customUrlInput.text = ""
                                    root.showAddCustomCard = false
                                }
                            }
                        }
                    }
                }

                // -------------------------------------------------------------
                // Drittanbieter-Quellen
                // -------------------------------------------------------------
                Column {
                    id: thirdPartySection
                    visible: repositoriesModel ? (repositoriesModel.distroFamily === "arch" || repositoriesModel.distroFamily === "fedora" || repositoriesModel.distroFamily === "debian") : false
                    width: parent.width
                    spacing: Theme.s3

                    Column {
                        spacing: 2
                        Text {
                            text: qsTr("Drittanbieter-Quellen")
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text {
                            text: qsTr("Paketquellen und Repositories von Drittanbietern. Aktivierung auf eigenes Risiko.")
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }

                    // Arch: AUR-Schalter
                    Card {
                        visible: repositoriesModel && repositoriesModel.distroFamily === "arch"
                        width: parent.width
                        height: 72

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.s4
                            spacing: Theme.s4

                            Kirigami.Icon {
                                source: "package-x-generic"
                                width: 28
                                height: 28
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 240
                                spacing: 2

                                Text {
                                    text: qsTr("Arch User Repository (AUR)")
                                    font.pixelSize: 14
                                    font.weight: Font.DemiBold
                                    color: Theme.text
                                }

                                Text {
                                    text: qsTr("Pakete aus der Arch-Community (PKGBUILDs). Werden lokal kompiliert und installiert.")
                                    font.pixelSize: 12
                                    color: Theme.textMuted
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }

                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 170
                                height: 36

                                Row {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.s2

                                    Switch {
                                        id: aurSwitch
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: appSettings ? appSettings.aurEnabled : false
                                        enabled: aurUpdates ? (aurUpdates.supported && !aurUpdates.busy) : false
                                        onToggled: {
                                            if (checked) {
                                                checked = false
                                                riskDialog.openForSource(
                                                    qsTr("Arch User Repository (AUR)"),
                                                    qsTr("Pakete im Arch User Repository werden von Nutzern eingestellt und von Arch Linux nicht geprüft. Ein PKGBUILD kann beim Bauen beliebige Befehle ausführen, das fertige Paket wird mit Administratorrechten installiert. Der Store zeigt dir vor jedem Bau die Änderungen, beurteilen musst du sie selbst. AUR-Pakete können nach Systemaktualisierungen brechen."),
                                                    qsTr("Einschalten"),
                                                    function() {
                                                        if (appSettings) appSettings.aurEnabled = true
                                                        if (aurUpdates) aurUpdates.setEnabled(true)
                                                        aurSwitch.checked = true
                                                    }
                                                )
                                            } else {
                                                if (appSettings) appSettings.aurEnabled = false
                                                if (aurUpdates) aurUpdates.setEnabled(false)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Debian/Ubuntu: Pacstall-Karte
                    Card {
                        visible: repositoriesModel && repositoriesModel.distroFamily === "debian"
                        width: parent.width
                        height: 72

                        Row {
                            anchors.fill: parent
                            anchors.margins: Theme.s4
                            spacing: Theme.s4

                            Kirigami.Icon {
                                source: "package-x-generic"
                                width: 28
                                height: 28
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 240
                                spacing: 2

                                Row {
                                    spacing: Theme.s2
                                    Text {
                                        text: qsTr("Pacstall")
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                    }
                                    Chip {
                                        visible: !repositoriesModel.pacstallInstalled
                                        text: qsTr("Nicht eingerichtet")
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                Text {
                                    // Debian 13 hat spdx-licenses nicht: pacstall lehnt dann jede
                                    // Lizenzangabe ab. "pacstall -U" lädt es selbst nach (update.sh).
                                    text: repositoriesModel.pacstallLicensesMissing
                                        ? qsTr("Es fehlt spdx-licenses (nicht in Debian 13): Pacscripts mit Lizenzangabe scheitern. Abhilfe im Terminal: sudo pacstall -U")
                                        : repositoriesModel.pacstallInstalled
                                        ? qsTr("Aktualisierungen für installierte Pacstall-Pakete prüfen und verwalten.")
                                        : qsTr("Paketmanager für Pacscripts (AUR-ähnliche Bauanleitungen für Debian und Ubuntu).")
                                    font.pixelSize: 12
                                    color: repositoriesModel.pacstallLicensesMissing ? Theme.neutral : Theme.textMuted
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                            }

                            Item {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 170
                                height: 36

                                Row {
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.s2

                                    PrimaryButton {
                                        visible: !repositoriesModel.pacstallInstalled
                                        text: qsTr("Einrichten …")
                                        variant: "primary"
                                        anchors.verticalCenter: parent.verticalCenter
                                        onClicked: {
                                            riskDialog.openForSource(
                                                qsTr("Pacstall"),
                                                qsTr("Pacstall installiert Programme nach Bauanleitungen (Pacscripts) aus einem von Nutzern gepflegten Verzeichnis, nicht aus Debian oder Ubuntu. Pacscripts werden mit Administratorrechten ausgeführt. Der Store zeigt dir jedes Pacscript vor der Ausführung, beurteilen musst du es selbst."),
                                                qsTr("Einrichten …"),
                                                function() {
                                                    repositoriesModel.addPreset("pacstall")
                                                },
                                                qsTr("Pacstall einrichten?")
                                            )
                                        }
                                    }

                                    Switch {
                                        id: pacstallSwitch
                                        visible: repositoriesModel.pacstallInstalled
                                        anchors.verticalCenter: parent.verticalCenter
                                        checked: appSettings ? appSettings.pacstallEnabled : false
                                        onToggled: {
                                            if (checked) {
                                                checked = false
                                                riskDialog.openForSource(
                                                    qsTr("Pacstall"),
                                                    qsTr("Pacstall installiert Programme nach Bauanleitungen (Pacscripts) aus einem von Nutzern gepflegten Verzeichnis, nicht aus Debian oder Ubuntu. Pacscripts werden mit Administratorrechten ausgeführt. Der Store zeigt dir jedes Pacscript vor der Ausführung, beurteilen musst du es selbst."),
                                                    qsTr("Einschalten"),
                                                    function() {
                                                        if (appSettings) appSettings.pacstallEnabled = true
                                                        pacstallSwitch.checked = true
                                                    }
                                                )
                                            } else {
                                                if (appSettings) appSettings.pacstallEnabled = false
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Drittanbieter-Presets (Chaotic-AUR, RPM Fusion Free/Nonfree, Terra + subrepos)
                    Repeater {
                        model: root.getThirdPartyPresets()

                        delegate: Card {
                            width: parent.width
                            height: 72

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: (modelData.requiresPreset && modelData.requiresPreset.length > 0) ? (Theme.s4 + Theme.s5) : Theme.s4
                                anchors.rightMargin: Theme.s4
                                anchors.topMargin: Theme.s4
                                anchors.bottomMargin: Theme.s4
                                spacing: Theme.s4

                                Kirigami.Icon {
                                    source: "package-x-generic"
                                    width: 28
                                    height: 28
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 240
                                    spacing: 2

                                    Text {
                                        text: modelData.name
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                    }

                                    Text {
                                        text: modelData.description
                                        font.pixelSize: 12
                                        color: Theme.textMuted
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }

                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 170
                                    height: 36

                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.s2

                                        Chip {
                                            visible: modelData.isAdded
                                            text: qsTr("Aktiviert")
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        PrimaryButton {
                                            visible: modelData.isAdded
                                            text: qsTr("Löschen")
                                            variant: "destructive"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: repositoriesModel.removeRepo(modelData.id)
                                        }

                                        PrimaryButton {
                                            visible: !modelData.isAdded
                                            text: qsTr("+ Hinzufügen")
                                            variant: "primary"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: {
                                                riskDialog.openForSource(
                                                    modelData.name,
                                                    modelData.riskNotice || qsTr("Aktivierung auf eigenes Risiko."),
                                                    qsTr("Hinzufügen"),
                                                    function() {
                                                        repositoriesModel.addPreset(modelData.id)
                                                    }
                                                )
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // -------------------------------------------------------------
                // 1-Klick-Vorschläge (Empfohlene Repositories)
                // -------------------------------------------------------------
                Column {
                    width: parent.width
                    visible: root.getRecommendedPresets().length > 0
                    spacing: Theme.s3

                    Column {
                        spacing: 2
                        Text {
                            text: qsTr("Empfohlene Paketquellen")
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text {
                            text: qsTr("Häufig benötigte Paketquellen mit einem Klick hinzufügen")
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }

                    Repeater {
                        model: root.getRecommendedPresets()

                        delegate: Card {
                            width: parent.width
                            height: 72

                            Row {
                                anchors.fill: parent
                                anchors.margins: Theme.s4
                                spacing: Theme.s4

                                Kirigami.Icon {
                                    source: "package-x-generic"
                                    width: 28
                                    height: 28
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 240
                                    spacing: 2

                                    Text {
                                        text: modelData.name
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        color: Theme.text
                                    }

                                    Text {
                                        text: modelData.description
                                        font.pixelSize: 12
                                        color: Theme.textMuted
                                        elide: Text.ElideRight
                                        width: parent.width
                                    }
                                }

                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 170
                                    height: 36

                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.s2

                                        Chip {
                                            visible: modelData.isAdded
                                            text: qsTr("Aktiviert")
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        PrimaryButton {
                                            visible: modelData.isAdded
                                            text: qsTr("Löschen")
                                            variant: "destructive"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: repositoriesModel.removeRepo(modelData.id)
                                        }

                                        PrimaryButton {
                                            visible: !modelData.isAdded
                                            text: qsTr("+ Hinzufügen")
                                            variant: "primary"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: repositoriesModel.addPreset(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // -------------------------------------------------------------
                // Liste aller konfigurierten Repositories
                // -------------------------------------------------------------
                Column {
                    width: parent.width
                    spacing: Theme.s3

                    Column {
                        spacing: 2
                        Text {
                            text: qsTr("Konfigurierte Paketquellen (%1)").arg(repositoriesModel ? repositoriesModel.count : 0)
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: Theme.text
                        }
                        Text {
                            text: qsTr("Im Betriebssystem gefundene Paketquellen")
                            font.pixelSize: 12
                            color: Theme.textMuted
                        }
                    }

                    Repeater {
                        model: repositoriesModel

                        delegate: Card {
                            width: parent.width
                            height: 68

                            Row {
                                anchors.fill: parent
                                anchors.margins: Theme.s4
                                spacing: Theme.s4

                                Kirigami.Icon {
                                    source: model.isSystem ? "drive-harddisk-root" : "network-server"
                                    width: 24
                                    height: 24
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: parent.width - 240
                                    spacing: 2

                                    Row {
                                        spacing: Theme.s2

                                        Text {
                                            text: model.name
                                            font.pixelSize: 14
                                            font.weight: Font.DemiBold
                                            color: Theme.text
                                            anchors.verticalCenter: parent.verticalCenter
                                        }

                                        Chip {
                                            visible: model.isSystem
                                            text: qsTr("System")
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }

                                    Text {
                                        text: model.url.length > 0 ? model.url : model.filePath
                                        font.pixelSize: 12
                                        color: Theme.textMuted
                                        elide: Text.ElideMiddle
                                        width: parent.width
                                    }
                                }

                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 170
                                    height: 36

                                    Row {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.s2

                                        PrimaryButton {
                                            text: model.enabled ? qsTr("Aktiv") : qsTr("Inaktiv")
                                            variant: "quiet"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: repositoriesModel.toggleRepo(model.id, !model.enabled)
                                        }

                                        PrimaryButton {
                                            visible: !model.isSystem
                                            text: qsTr("Löschen")
                                            variant: "destructive"
                                            iconName: "edit-delete"
                                            anchors.verticalCenter: parent.verticalCenter
                                            onClicked: repositoriesModel.removeRepo(model.id)
                                        }

                                        Text {
                                            visible: model.isSystem
                                            text: qsTr("Geschützt")
                                            font.pixelSize: 12
                                            color: Theme.textMuted
                                            anchors.verticalCenter: parent.verticalCenter
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Platzhalter am Ende des Scroll-Bereichs
                Item {
                    width: parent.width
                    height: Theme.s5
                }
            }
        }
    }
}

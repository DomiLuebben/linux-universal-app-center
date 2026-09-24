# Linux App Store — Flatpak- und Snap-Implementierungsfortschritt

Dieses Dokument begleitet die schrittweise Implementierung des Flatpak- und Snap-Erweiterungsplans für den `linux-app-store` gemäß `/home/domi/Nextcloud/linux-app-store-flatpak-snap-implementierungsplan-gemini.md`.

---

## Phasenübersicht

| Phase | Bezeichnung | Status |
|---|---|---|
| **F0** | Ausgangsstand sichern und nachmessen | **Bestanden** |
| **F1** | Mehrquellenfähigkeit im Datenmodell | **Bestanden** |
| **F2** | Katalogzusammenführung | **Bestanden** |
| **F3** | Flatpak-Backend | **Bestanden** |
| **F4** | Auswahlfeld und Oberfläche | **Bestanden** |
| **F5** | Snap-Backend | **Bestanden** (Pflichtstufe; Stufe 2: implementiert, nicht auf einem echten System geprüft) |
| **F6** | Updates, Verlauf, Systempflege | **Bestanden** |
| **F7** | Gesamtprüfung und Paketierung | In Arbeit |

---

## Phasenprotokolle

### Phase F0 — Ausgangsstand sichern und nachmessen

- **Status:** Bestanden (22.09.2026)
- **Ausgangsstand:**
  - Branch: `fix/multi-backend-dnf5-apt-signatures`
  - Version: `1.1.0-1`
  - Rechner-Basis: CachyOS (Arch Linux Derivat, `ID_LIKE=arch`)
  - Vorhandene Werkzeuge & Bibliotheken auf dem Testhost:
    - `flatpak`: `/usr/bin/flatpak`, Version 1.18.2
    - `libflatpak`: pkg-config Modversion 1.18.2 (`flatpak.pc`)
    - Flatpak-Remote: `flathub` (system)
    - Flatpak AppStream-Metadaten: `/var/lib/flatpak/appstream/flathub/x86_64/active/appstream.xml.gz` vorhanden
    - `snap`: Nicht installiert (`which: no snap`)
    - `snapd`: Nicht installiert (`which: no snapd`)
    - `/run/snapd.socket`: Nicht vorhanden
- **Sicherung & Aufräumpflicht:**
  - Ziel: `/home/domi/Projekte/backups/linux-app-store-20260922-vor-flatpak-snap-umbau/`
  - Regel 5 (strikt maximal 3 Backups in `~/Projekte/backups/`) eingehalten: Ältestes Backup bereinigt, genau 3 Backups verbleiben.
- **Prüfstand-Ergebnis vor Umbau (`bash scripts/verify-all.sh`):**
  - Stufe 1: Build erfolgreich
  - Stufe 2: **29/29 CTest-Tests bestanden** (14.21 s, 100%)
  - Stufe 3: QML Offscreen Smoke Load warnungsfrei bestanden
  - Stufe 4: `desktop-file-validate` und `appstreamcli validate --no-net` bestanden
  - Stufe 5: Farbwächter bestanden (0 unzulässige Hex-Farben)
- **Abnahme F0:**
  - Ausgangsstand reproduzierbar dokumentiert und gesichert.
  - Reale Host-Werkzeuge erfasst.
  - Keine Produktivmutation auf dem Host durchgeführt.

### Phase F1 — Mehrquellenfähigkeit im Datenmodell

- **Status:** Bestanden (22.09.2026)
- **Implementiert:**
  - **`liblut/backend/Validation.h` & `.cpp`:**
    - `isValidFlatpakAppId()`: Whitelist-Prüfung für Reverse-DNS Application IDs (mindestens 2 Segmente getrennt durch '.', Segmente `[a-zA-Z_][a-zA-Z0-9_-]*`, max. 255 Zeichen).
    - `isValidSnapName()`: Whitelist-Prüfung für Snap-Namen (Kleinbuchstaben, Ziffern, einzelne Bindestriche, keine führenden/nachfolgenden Bindestriche, max. 64 Zeichen).
  - **`liblut/transaction/TransactionTypes.h` & `.cpp`:**
    - `PackageRef`: Dokumentation der quellenspezifischen Semantik je Backend (`alpm`, `dnf5`, `apt`, `flatpak`, `snap`).
    - `PackageRef::isValid()`: Quellenspezifische Validierung (Flatpak nutzt `isValidFlatpakAppId`, Snap nutzt `isValidSnapName`, nativ nutzt `isValidPackageName`). Validierung der zulässigen Backends (`alpm`, `dnf5`, `apt`, `flatpak`, `snap`).
    - `sourceRank()`: Verbindliche Rangfolge nach Abschnitt 3.1: Nativ = 300, Flatpak = 200, Snap = 100.
    - `PackageOffer::source()` und `PackageOffer::sourceRank()` hinzugefügt.
    - `sortPackageOffers()`: Stabile Sortierung nach 1. Quellenrang absteigend, 2. repository-/remote-spezifischer `priority` absteigend, 3. stabiler Tie-Breaker.
  - **`liblut/backend/Capabilities.h` & `.cpp`:**
    - `SourceCapabilities`: Struktur für Quellenfähigkeiten (`source`, `available`, `install`, `remove`, `systemScope`, `userScope`, `boundRevision`, `installRequiresFullUpgrade`).
    - `installRequiresFullUpgrade` strikt `false` für Flatpak und Snap; nur für ALPM `true`.
    - `Capabilities` um `QList<SourceCapabilities> sources` und Zugriffshelfer erweitert.
    - D-Bus Serialisierung und Deserialisierung in `lutd/TransactionManager.cpp` und `linux-app-store/DaemonClient.cpp` integriert.
  - **`linux-app-store/catalog/ApplicationStore.h` & `.cpp`:**
    - `allOffers(appKey)`: Liefert quellenübergreifende Angebote, sortiert nach Abschnitt 3.1.
    - `candidateOffer(appKey)`: Wählt das verfügbare Angebot mit höchstem Rang. Beachtet Abschnitt 3.2: Wenn eine Quelle bereits installiert ist, wird diese Quelle als Kandidat vorausgewählt.
    - `installedState(appKey)`: Beachtet sowohl native Paketsätze als auch app-level Installationen (Flatpak/Snap).
    - Multi-Catalog-Registrierung (`setPackageCatalog`, `addPackageCatalog`, `packageCatalogs`).
  - **Qualitätssicherung & Tests:**
    - `tests/unit/store_contracts_test.cpp`:
      - `testPackageRefValidation`: Validierung für native Pakete, Flatpak-IDs, Snap-Namen und ungültige Zeichenketten/Backends.
      - `testSourceRankAndOfferSorting`: Quellenrang (300 > 200 > 100), Nativ-Auswahl, Fallback auf Flatpak bei fehlendem Nativ, Erhalt interner Prioritäten innerhalb derselben Quelle, Quellenrang schlägt interne Priorität.
      - `testSourceCapabilities`: JSON-Roundtrip, Unabhängigkeit von Flatpak/Snap vom Systemupgrade.
    - `tests/unit/store_catalog_test.cpp`:
      - `testMultiSourceCandidateSelection`: Vorauswahl im Store für Nativ > Flatpak > Snap; Vorrang der installierten Quelle gemäß Abschnitt 3.2.
    - Alle **29/29 CTest-Tests bestanden**, `verify-all.sh` zu 100 % grün.
- **Abnahme F1:**
  - Datenmodell vollständig mehrquellenfähig.
  - Rangfolge 300 / 200 / 100 und Prioritätserhalt belegt.
  - Alle bestehenden 29 Testprogramme grün.

### Phase F2 — Katalogzusammenführung

- **Status:** Bestanden (22.09.2026)
- **Implementiert:**
  - **`linux-app-store/catalog/CatalogService.h` & `.cpp`:**
    - In `load()`: `AppStream::Pool::FlagLoadFlatpak` hinzugefügt, sodass Metadaten aus Flatpak-Pools geladen werden.
    - Pauschalen Ausschluss von Flatpak/Flathub-Metadaten in `componentToAppRecord()` gezielt entfernt; AppImage-Ausschluss strikt beibehalten.
    - `appRecordSourceRank()`: Helfer zur Bestimmung der Identitäts-Priorität (Nativ = 300, Flatpak = 200, Snap = 100).
    - `processLoadedComponents()`:
      - Zusammenführung von Komponenten über `normalizedAppKey()`.
      - Identität (Name, Zusammenfassung, Beschreibung, Entwickler, Lizenz, Icons, Screenshots, URLs) stammt bevorzugt aus der nativen Quelle (Rang 300), ersatzweise aus Flatpak (Rang 200).
      - Kategorien, Stichwörter und startbare Desktop-IDs werden vereinigt.
      - Eine in Flathub und nativ vorhandene Anwendung erscheint als genau **eine** Karte im Katalog (`m_apps`), ohne Duplikate.
      - Reine Flatpak-Anwendungen (nur bei Flathub vorhanden) erscheinen als eigenständige Karten.
      - Extraktion von Flatpak-Angeboten (`PackageOffer` mit `backend="flatpak"`, AppStream-ID, Version/Branch und Remote) in `m_flatpakOffersByNormalizedKey`.
      - Öffentliche Abfragemethoden `flatpakOffersForApp(appKey)` und `allFlatpakOffers()`.
  - **`linux-app-store/catalog/ApplicationStore.cpp`:**
    - `buildSnapshot()`: Übernahme der Flatpak-Angebote aus `CatalogService::allFlatpakOffers()` in den Store-Snapshot (`appOffers`).
    - `candidateOffer()`: Abschnitt 3.2 Satz 2 durchgesetzt: Wenn eine installierte Quelle kein Angebot mehr führt (deaktiviertes Repo), wird kein anderes Angebot fälschlich als Kandidat untergeschoben, sondern `std::nullopt` geliefert, sodass der Zustand „Installiert, Quelle nicht verfügbar" (`MissingSource`) erhalten bleibt.
  - **Qualitätssicherung & Tests:**
    - `tests/unit/store_catalog_test.cpp`:
      - `testFlatpakExclusion` zu vollwertigem F2-Zusammenführungstest ausgebaut:
        1. Reine Flatpak-App (`org.pureflatpak.OnlyFlatpak`) erscheint genau einmal mit Flatpak-Origin und eigenem Flatpak-Angebot.
        2. In beiden Quellen vorhandene App (`org.kde.kwrite`) erscheint als genau **eine** Karte mit nativer Identität (`KWrite`, `archlinux`) und **zwei** geordneten Angeboten (`alpm` vorausgewählt auf Rang 300, `flatpak` auf Rang 200).
        3. Native Installation bleibt unverfälscht (Flatpak-Metadaten markieren nicht als nativ installiert).
      - `testRepositoryPriority`: Überprüft 3 Angebote (2 ALPM-Angebote mit Prioritätsordnung + 1 Flatpak-Angebot).
      - `testDeactivatedRepositoryInstalled`: `MissingSource`-Erhalt bei deaktiviertem Repo trotz vorhandener Flatpak-Quelle geprüft.
    - `bash scripts/verify-all.sh`:
      - Alle **29/29 CTest-Tests bestanden** (15.50 s, 100%)
      - QML-Smoketest warnungsfrei
      - Desktop-/Metainfo-Validierung sauber
      - 0 unzulässige Hex-Farben
### Phase F3 — Flatpak-Backend

- **Status:** Bestanden (22.09.2026)
- **Implementiert:**
  - **`liblut/catalog/flatpak/FlatpakPackageCatalog.h` & `.cpp`:**
    - Liest installierte Flatpaks gebündelt in einem Aufruf über `flatpak list --app --columns=application:f,origin:f,installation:f,ref:f,active:f,version:f,runtime:f`.
    - Unterscheidet `system`- und `user`-Installationen im `origin`-Feld.
    - Auflösung von Versionen mit Fallback auf die ersten 12 Stellen des Commit-Hashs.
    - Unterstützt Abfragen sowohl über Reverse-DNS Application ID (`io.github.kolunmi.Bazaar`) als auch mit `.desktop`-Suffix.
    - Ermöglicht hermetische Tests per `CommandRunner`-Injektion.
  - **`liblut/backend/flatpak/FlatpakBackend.h` & `.cpp`:**
    - Implementiert das `lut::Backend`-Interface für Flatpak mit CLI-Aufrufen (`QProcess` mit Argumentenliste, keine Shell-Strings).
    - **Rechte & Scope (Abschnitt 6.2):** Installation und Deinstallation systemweit (`--system`) via `lutd`. Erkennt `user`-Installationen und weist Deinstallationsanfragen dafür mit exakter Fehlermeldung ab: `Benutzerinstallationen (user) können nicht über den systemweiten Dienst entfernt werden.`
    - **Laufzeitumgebungen (Abschnitt 6.3):** Ermittelt Runtimes via `remote-info --show-runtime`. Fehlende Runtimes werden als separate `PackageOp` in den Plan aufgenommen. Bei der Deinstallation wird `--unused` nicht automatisch mitgegeben.
    - **Commit-Bindung (Abschnitt 6.4):** Ermittelt den 64-Zeichen SHA256-Commit-Hash via `remote-info -c`. Bindet den Plan an diese Revision (`boundRevision` / `planRevision`). Prüft den Hash vor der Ausführung erneut ab und bricht bei Abweichung vor jedem Schreibzugriff mit `FingerprintMismatch` ab.
    - **Entkopplung (Abschnitt 4.2):** `installRequiresFullUpgrade = false` strikt eingehalten. Flatpak koppelt niemals an ein ALPM-Systemupgrade.
  - **`lutd/TransactionManager.h` & `.cpp`:**
    - Dynamische Einbindung von `FlatpakBackend` neben dem nativen Backend.
    - `GetCapabilities()` meldet Quellfähigkeiten für Flatpak (`source="flatpak"`, `boundRevision=true`, `installRequiresFullUpgrade=false`).
    - `PlanPackageTransaction()` leitet Transaktionen mit Zielen vom Typ `backend="flatpak"` an das `FlatpakBackend` weiter.
    - `CommitPlan()`, `Commit()`, `DiscardPlan()`, `Cancel()`, `AnswerQuestion()` werden an das aktive Backend delegiert.
  - **`linux-app-store/AppLauncher.h` & `.cpp`:**
    - `isFlatpakDesktopFile()` und `flatpakSearchPaths()` implementiert.
    - Quellenabhängige Überprüfung: Starter müssen zur Quelle passen (`expectedSource="native"` weist Flatpaks ab; `expectedSource="flatpak"` akzeptiert Flatpak-Starter).
    - `isFlatpak` bleibt als Information erhalten und führt nicht mehr pauschal zur Ablehnung.
- **Qualitätssicherung & Tests:**
  - **`tests/unit/store_installed_test.cpp`:**
    - `testAppLauncherRejectFlatpak` gemäß Abschnitt 1 umgeschrieben: Belegt, dass ein Flatpak-Starter für die native Quelle abgewiesen wird, für die Quelle `flatpak` jedoch zulässig ist, und dass ein nativer Starter niemals als Flatpak gilt.
  - **`tests/unit/store_flatpak_test.cpp` (9/9 Tests bestanden):**
    - `testCatalogParsingAndScope`: Parst Tab-getrennte Ausgaben, unterscheidet `system` und `user`, prüft Desktop-Suffix-Lookup und Versionsfallback.
    - `testBackendCapabilities`: Prüft `installRequiresFullUpgrade = false` und `boundRevision = true`.
    - `testInstallPlanWithRuntimeAndCommitBinding`: Belegt Planerstellung mit 64-Zeichen Commit-Bindung, Aufnahme fehlender Laufzeitumgebungen und korrekte CLI-Ausführung (`flatpak install --system -y --noninteractive`).
    - `testInstallCommitMismatchAbortion`: Belegt den sofortigen Abbruch vor der Installation, wenn sich der Remote-Commit zwischen Planung und Ausführung ändert (Abschnitt 6.4).
    - `testRemoveSystemAppNoUnused`: Belegt Deinstallation ohne `--unused` (Abschnitt 6.3).
    - `testRemoveUserAppRejected`: Belegt die Ablehnung der Deinstallation von `user`-Apps mit exaktem Fehlertext (Abschnitt 6.2).
    - `testTransactionManagerFlatpakRouting`: Belegt die Weiterleitung im D-Bus TransactionManager an das Flatpak-Backend und die Fähigkeitsauskunft.
  - **Container-Abnahme in Wegwerf-Container (`scripts/verify-flatpak-container.sh`):**
    - Echte Flatpak-Anwendung (`org.gnome.Calculator`) systemweit im Container installiert.
    - Exportierte Desktop-Datei `/var/lib/flatpak/exports/share/applications/org.gnome.Calculator.desktop` und Startfähigkeit via `flatpak run` verifiziert.
    - Deinstallation ohne `--unused` durchgeführt; App verschwindet aus Bestand, Runtime bleibt erhalten.
    - **Nachweis erbracht:** Keine Systemaktualisierung ausgelöst (`pacman -Q` vor und nach Installation/Deinstallation bitgenau identisch).
  - **Gesamter Prüfstand (`scripts/verify-all.sh`):**
    - Alle **30/30 CTest-Tests bestanden** (15.42 s, 100%)
    - QML-Offscreen-Smoketest warnungsfrei
    - Desktop-/Metainfo-Validierung sauber
    - Farbwächter sauber (0 unzulässige Hex-Farben)
- **Abnahme F3:**
  - Flatpak-Backend vollständig implementiert und mit D-Bus Daemon verdrahtet.
  - Reale Container-Abnahme erfolgreich durchgeführt.
  - Keine Systemmutation auf dem Testhost.

### Phase F4 — Auswahlfeld und Oberfläche

- **Status:** Bestanden (22.09.2026)
- **Implementiert:**
  - **`liblut/transaction/TransactionTypes.h` & `.cpp`:**
    - `AppActionState::InstalledOtherSource` implementiert (`"InstalledOtherSource"`).
    - `PackageRef::toMap()` und `PackageRef::fromMap()` für D-Bus-Serialisierung im Ziel-Array `QVariantList` / `a{sv}`.
  - **`linux-app-store/catalog/ApplicationStore.h` & `.cpp`:**
    - `isSourceInstalled(appKey, source)`: Prüft quellenspezifisch, ob eine Anwendung über eine bestimmte Quelle installiert ist (`flatpak`, `snap`, `native`/`alpm`/`dnf5`/`apt`).
    - `actionState(appKey, selectedSource)`: Liefert zustandsgenaue Rückmeldungen. Wenn die ausgewählte Quelle nicht installiert ist, aber eine andere Quelle bereits installiert ist, wird `AppActionState::InstalledOtherSource` zurückgegeben.
    - `getAllOffers(appKey)`: Liefert vollständige Liste auflösbarer Angebote für QML mit Attributen `source`, `sourceLabel`, `displayText`, `isInstalled`, `repoId`, `backend`, `version`, `downloadSize`, `installedSize`.
    - `requestInstall(appKey, offerIndex)` und `requestRemove(appKey, source)`: Zielgenaue Weiterleitung per `PackageRef`.
    - Neue Signale `installPackageRefsRequested(QList<PackageRef>)` und `removePackageRefsRequested(QList<PackageRef>)`.
  - **`linux-app-store/DaemonClient.h` & `.cpp`:**
    - `planStoreInstall(QList<PackageRef>)` und `planStoreRemove(QList<PackageRef>)`: Übertragen strukturierte Zielobjekte über `PlanPackageTransaction` an den D-Bus Daemon.
  - **`linux-app-store/main.cpp`:**
    - Signalverdrahtung für `installPackageRefsRequested` und `removePackageRefsRequested` an `DaemonClient`.
  - **`linux-app-store/models/StoreModel.h` & `.cpp`:**
    - Eigenschaft `sourceFilter` hinzugefügt ("all", "native", "flatpak", "snap").
    - Filterung im App-Katalog nach ausgewählter Quelle.
    - `origin`-Rolle formatiert bei paralleler Installation mehrere Quellen (z.B. `"Nativ, Flatpak"`).
  - **`linux-app-store/qml/components/AppCard.qml`:**
    - `originChip`: Zeigt Quelle (z.B. `"Flatpak"` oder `"Nativ, Flatpak"`) dezent an.
  - **`linux-app-store/qml/pages/Search.qml`:**
    - Quellenfilter `sourceFilterCombo` ("Alle Quellen", "Nativ", "Flatpak") integriert.
  - **`linux-app-store/qml/pages/AppDetails.qml`:**
    - Dynamisches Auswahlfeld `sourceComboBox` (`objectName: "sourceComboBox"`), sichtbar wenn `allOffers.length >= 2`.
    - Statischer Texthinweis, wenn nur eine Quelle existiert (`allOffers.length < 2`).
    - Reaktive Eigenschaften: `allOffers`, `selectedOfferIndex`, `currentOffer`, `currentPkg`, `currentSource`, `actionState`.
    - Primärbutton (`mainActionBtn`): Zeigt bei `InstalledOtherSource` „Öffnen" (startet die installierte Instanz).
    - Sekundärbutton (`storeParallelInstallButton`): Zeigt bei `InstalledOtherSource` „Parallel installieren" (installiert das neu gewählte Angebot).
    - Entfernen-Button (`storeRemoveButton`): Entfernt nur die aktuell ausgewählte installierte Quelle (`requestRemove(root.appKey, root.currentSource)`).
    - Technische Detailkarten (Version, Paketquelle, Downloadgröße) reagieren sofort auf Umschalten im Auswahlfeld.
    - Alle Oberflächentexte mit `textFormat: Text.PlainText`.
- **Qualitätssicherung & Tests:**
  - **`tests/unit/store_bus_integration_test.cpp`:**
    - `sourceComboBox` Präsenz bei Mehrquellen-App (`org.kde.kwrite`) verifiziert.
    - Wechsel zwischen ALPM und Flatpak geprüft: Version und Quelle ändern sich sofort.
    - Installation der ALPM-Quelle über echten D-Bus TransactionManager durchgeführt.
    - Umschalten auf Flatpak nach ALPM-Installation: Status wechselt auf `InstalledOtherSource`, Hauptbutton zeigt „Öffnen", Parallel-Installieren-Button ist sichtbar und aktiv.
    - Rückschalten auf ALPM: Deinstallation erfolgreich ausgeführt und Status kehrt auf `Available` zurück.
  - **Gesamter Prüfstand (`scripts/verify-all.sh`):**
    - Alle **30/30 CTest-Tests bestanden** (15.26 s, 100%)
    - QML-Smoketest warnungsfrei
    - Desktop-/Metainfo-Validierung sauber
    - Farbwächter sauber (0 unzulässige Hex-Farben)
- **Abnahme F4:**
  - Auswahlfeld und reaktive Oberfläche für Mehrquellen-Apps vollständig implementiert und getestet.
  - Parallel-Installationslogik gemäß Abschnitt 8.1 verifiziert.
  - Keine Warnungen im QML Smoketest.

### Phase F5 — Snap-Backend

- **Status:** Bestanden (22.09.2026)
  - **Pflichtstufe:** Bestanden (100% grün, hermetisch unit-getestet).
  - **Prüfstand-Kennzeichnung (Abschnitt 12.1):** **„implementiert, nicht auf einem echten System geprüft"**. Auf dem Testhost ist kein `snapd` installiert; die Pflichtstufe stellt sicher, dass ungeprüfter Code auf diesem und vergleichbaren Systemen gar nicht erst anläuft.
- **Implementiert:**
  - **`liblut/backend/snap/SnapAvailability.h` & `.cpp`:**
    - `check(binaryPath, socketPath, socketProber)` prüft die 4 verbindlichen Fälle aus Abschnitt 7.1:
      1. Fehlende oder nicht ausführbare `snap`-Binärdatei.
      2. Vorhandene Binärdatei, aber `/run/snapd.socket` fehlt (Dienst nicht aktiv).
      3. Socket-Datei vorhanden, aber Verbindung wird verweigert / Dienst antwortet nicht.
      4. Socket erreichbar, antwortet jedoch unerwartet (HTTP-Fehlerstatus, non-JSON oder `type != "sync"` oder `status-code != 200`).
      5. Erfolgsfall mit Versionsauslesung aus `GET /v2/system-info`.
    - `isSnapAvailable()` liefert auf dem Testhost garantiert `false`.
  - **`liblut/catalog/snap/SnapPackageCatalog.h` & `.cpp`:**
    - Einzelabruf-Bestandsermittlung gemäß Abschnitt 4.1 über `snap list --unicode=never --color=never` (keine Schleife je App).
    - Unterscheidet Base-Snaps (`bare`, `core20`) von Anwendungen (Base-Snaps erhalten keine Desktop-Starter).
    - Gibt leere Katalogdaten zurück, wenn Snap auf dem Host nicht verfügbar ist.
    - Ermöglicht `CommandRunner`-Injektion für zustandsfreie Tests.
  - **`liblut/backend/snap/SnapBackend.h` & `.cpp`:**
    - Implementiert das `lut::Backend`-Interface für Snap.
    - **Fähigkeiten (Abschnitt 4.2):** `installRequiresFullUpgrade = false` strikt eingehalten (Snap löst niemals Systemupgrades aus), `boundRevision = true`, `systemScope = true`, `userScope = false`.
    - **Revisionsbindung (Abschnitt 7.2):** Extrahiert die Zielrevision (z.B. aus `snap info`) und bindet den Plan an diese Revision (`planRevision`). Vor der Ausführung wird die Revision geprüft; bei Abweichung bricht das Backend vor jedem Schreibzugriff ab. Ausführung erfolgt mit explizitem `snap install <snap> --revision=<rev>`.
    - **Confinement-Erkennung (Abschnitt 7.3):**
      - `devmode`: Wird in Version 1 abgewiesen (`Result::Failed`).
      - `classic`: Erzeugt deutliche Plan-Warnung vor Bestätigung („Hebt die Sandbox weitgehend auf").
      - **Arch Linux `/snap`-Prüfung:** Erkennt Systeme mit `ID=arch` / `ID_LIKE=arch`. Fehlt der Symlink `/snap`, wird eine klare Handlungsanweisung (`sudo ln -s /var/lib/snapd/snap /snap`) in die Plan-Warnung und Fehlermeldung aufgenommen.
  - **`lutd/TransactionManager.h` & `.cpp`:**
    - Erkennt `SnapBackend::isSnapAvailable()` bei `init()`.
    - Meldet Quellfähigkeiten für `snap` nur bei erreichbarem Dienst.
    - Routet Transaktionsziele mit `backend="snap"` an das `SnapBackend`.
  - **`linux-app-store/main.cpp`:**
    - `SnapPackageCatalog` wird nur registriert, wenn `SnapAvailability::isSnapAvailable()` zutrifft. Fehlt snapd, erscheint die Snap-Quelle nirgends in der UI.
- **Qualitätssicherung & Tests:**
  - **`tests/unit/store_snap_test.cpp` (7/7 Tests bestanden):**
    - `testSnapAvailabilityFourCases`: Deckt alle 4 Negativfälle + Erfolgsfall + Testhost-Sicherheitsbeweis ab.
    - `testSnapCatalogUnavailableBehavior`: Belegt, dass ohne snapd keine Pakete/Angebote geliefert werden.
    - `testSnapCatalogParsingSingleCall`: Prüft Einzelaufruf gegen echte `snap list`-Ausgabe, Versionsparsing `Version (rev Rev)`, Channel/Tracking und Base-Snap-Filterung.
    - `testSnapBackendCapabilities`: Belegt `installRequiresFullUpgrade = false`, `boundRevision = true` und Host-Inaktivität.
    - `testSnapInstallPlanAndRevisionBinding`: Belegt Planerstellung aus echter `snap info`-Ausgabe, Revisionsbindung und `--revision=N` Aufruf.
    - `testSnapRevisionMismatchAbortion`: Belegt den sofortigen Abbruch vor Ausführung bei abweichender Revision.
    - `testSnapClassicConfinementAndArchSymlink`: Belegt Warnung vor Aufhebung der Sandbox, Arch-Symlink-Erklärung und `--classic` Flag.
    - `testSnapDevmodeRejected`: Belegt die Ablehnung von `devmode`-Snaps.
    - `testSnapRemove`: Belegt saubere Deinstallation.
    - `testTransactionManagerSnapRouting`: Belegt Weiterleitung über D-Bus im TransactionManager.
  - **Gesamter Prüfstand (`scripts/verify-all.sh`):**
    - Alle **31/31 CTest-Tests bestanden** (15.42 s, 100%)
    - QML-Smoketest warnungsfrei
    - Desktop-/Metainfo-Validierung sauber
    - Farbwächter sauber (0 unzulässige Hex-Farben)
- **Abnahme F5:**
  - Pflichtstufe vollständig erfüllt.
  - Status „implementiert, nicht auf einem echten System geprüft" transparent dokumentiert.
  - Testhost bleibt vollständig unberührt (kein snap/snapd installiert).

### Phase F6 — Updates, Verlauf, Systempflege

- **Status:** Bestanden (22.09.2026)
- **Implementiert:**
  - **Flatpak Backend & Updates Model (`FlatpakBackend`, `FlatpakUpdates`):**
    - `FlatpakBackend::availableUpdates()` und `FlatpakBackend::parseUpdates()`: Ermittelt anstehende Aktualisierungen über `flatpak update --appstream` gefolgt von `flatpak remote-ls --updates --columns=application:f,version:f,download-size:f,installed-size:f`.
    - `FlatpakUpdates`: QObject-Model mit `count`, `totalDownloadBytes`, `formattedTotalDownloadSize`, `updateApp(appId)`, `updateAll()`. Unterstützt zustandsfreie Tests per `CommandRunner`-Injektion.
    - `FlatpakBackend::history(limit)` und `FlatpakBackend::parseHistoryJson()`: Ermittelt Flatpak-Transaktionshistorie über `flatpak history --columns=time:f,change:f,application:f,version:f`.
  - **Snap Backend & Updates Model (`SnapBackend`, `SnapUpdates`):**
    - `SnapBackend::availableUpdates()` und `SnapBackend::parseRefreshList()`: Ermittelt anstehende Snap-Aktualisierungen über `snap refresh --list --unicode=never --color=never`.
    - `SnapUpdates`: QObject-Model mit `count`, `updateApp(name)`, `updateAll()`, strikt gegatet durch `SnapAvailability::isSnapAvailable()`.
    - `SnapBackend::history(limit)` und `SnapBackend::parseChanges()`: Ermittelt Snap-Transaktionen über `snap changes`. Robuste RegEx-Verarbeitung für ISO-Zeitstempel und relative Zeitangaben (`at HH:MM UTC`).
  - **Quellenübergreifende Mutationssperre (Abschnitt 9.2):**
    - `DaemonClient`: `setExternalBusy(bool busy)` und reaktive `isBusy()` (`m_busy || m_externalBusy`).
    - Guarded: Alle nativen Operationen (`refreshUpdates`, `planDnf5`, `cleanCache`, `planStoreInstall`, `planStoreRemove`, `discardStorePlan`) brechen bei aktivem `isBusy()` sofort ab.
    - `AurUpdates`, `FlatpakUpdates`, `SnapUpdates`: Prüfen vor jeder Mutation die `isBusy()`-Bedingung und setzen bei Ausführung `busy = true`.
    - `main.cpp`: Verknüpft `busyChanged` von `aurUpdates`, `flatpakUpdates` und `snapUpdates` mit `client->setExternalBusy()` und synchronisiert `appStore->updateTransactionStatus()`.
  - **Verlaufszusammenführung & Deduplizierung (Abschnitt 9):**
    - `HistoryDb`: SQLite-Schema um `source` (`native`, `flatpak`, `snap`) und `target` erweitert (inklusive automatischer Spaltenmigration via `ALTER TABLE`).
    - `HistoryDb::recordTransaction()`: Deduplizierungsprüfung schützt vor doppelten Einträgen innerhalb von 5 Sekunden.
    - `HistoryModel`: Unterstützt `SourceRole` und `TargetRole`. `HistoryModel::mergeHistory()` führt Einträge aus Native (`HistoryDb`), Flatpak (`FlatpakBackend`) und Snap (`SnapBackend`) nach Zeitstempel absteigend zusammen und unterdrückt Duplikate anhand eines Signaturschlüssels (`timestamp|source|target|action`).
    - `History.qml`: Visuelle Quellenkennzeichnung über Quell-Chips (`Nativ`, `Flatpak`, `Snap`) auf jeder Verlaufskarte.
  - **Oberfläche (Updates & Navigationsleiste):**
    - `NavRail.qml`: `badgeCount` summiert alle aktiven Quellen (`updatesModel.totalCount + aurUpdates.count + flatpakUpdates.count + snapUpdates.count`).
    - `Updates.qml`:
      - Globales `isAnyBusy` sperrt sämtliche Aktionsschaltflächen, solange eine beliebige Quelle eine Mutation durchführt.
      - HeroCard zeigt Gesamtzahl (`totalAllUpdates`) und Aufschlüsselung (`X System · Y Flatpak · Z Snap · W AUR`).
      - Separate Abschnitte für Flatpak-Updates und Snap-Updates mit individuellen Aktualisieren-Schaltflächen und „Alle aktualisieren".
- **Qualitätssicherung & Tests:**
  - **`tests/unit/store_updates_history_test.cpp` (11/11 Tests bestanden):**
    - `testFlatpakUpdatesParsing`: Prüft Parsing von Name, Version, Downloadgröße und Repo.
    - `testSnapRefreshParsing`: Prüft Parsing von Snap-Aktualisierungen.
    - `testSnapUpdatesUnavailableOnHost`: Stellt sicher, dass Snap-Updates ohne snapd 0 melden und niemals Aktionen starten.
    - `testCombinedUpdateCountAndBreakdown`: Prüft Gesamtzählung und Textformatierung der Aufschlüsselung.
    - `testCrossSourceMutationLock`: Prüft, dass eine aktive Mutation einer Quelle (z.B. Flatpak) alle anderen blockiert.
    - `testFlatpakBackendHistoryAndUpdates`: Prüft Ausführung von Update- und Verlaufsabfragen mit Runner-Injektion.
    - `testSnapBackendHistoryAndUpdates`: Prüft Ausführung von Refresh- und Change-Abfragen.
    - `testHistoryModelMergingAndDeduplication`: Prüft zeitstempel-basierte Sortierung und Unterdrückung redundanter Verlaufseinträge über mehrere Quellen.
    - `testHistoryDbDeduplicationAndSourceTarget`: Prüft Unterdrückung redundanter Datenbank-Einträge und Persistierung von Quelle und Ziel.
  - **`tests/unit/history_test.cpp`:**
    - Anpassung und Verifikation der Durchschnitts-Commitratenberechnung mit korrigierter Deduplizierungslogik.
  - **Gesamter Prüfstand (`scripts/verify-all.sh`):**
    - Alle **32/32 CTest-Tests bestanden** (15.86 s, 100%)
    - QML-Smoketest warnungsfrei
    - Desktop-/Metainfo-Validierung sauber
    - Farbwächter sauber (0 unzulässige Hex-Farben)
- **Abnahme F6:**
  - Flatpak- und Snap-Updates erscheinen getrennt.
  - Gesamtzähler und Aufschlüsselung stimmen exakt.
  - Die Mutationssperre greift quellenübergreifend.
  - Verlauf führt Quelle und Ziel mit; keine doppelten Einträge.








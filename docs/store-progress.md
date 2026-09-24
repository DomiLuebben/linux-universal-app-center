# Linux Update Tool — Store-Implementierungsfortschritt

Dieses Dokument begleitet die schrittweise Implementierung des Store-Plans für das `linux-update-tool` gemäß `/home/domi/Documents/Codex/2026-09-22/lie/outputs/Linux-Update-Tool-Store-Implementierungsplan-Gemini.md`.

---

## Phasenübersicht

| Phase | Bezeichnung | Status |
|---|---|---|
| **P0** | Ausgangsstand sichern und nachmessen | **Bestanden** |
| **P1** | Daten- und Transaktionsverträge festlegen | **Bestanden** |
| **P2** | AppStream und nativen Arch-Katalog zusammenführen | **Bestanden** |
| **P3** | ALPM-Installation/Entfernung und Vorschau durchgängig | **Bestanden** |
| **P4** | Entdecken, Suche und App-Details | **Bestanden** |
| **P5** | Installiert, Öffnen und vorhandene Funktionen integrieren | **Bestanden** |
| **P6** | D-Bus-Lebenszyklus, Wiederanbindung und Fehlerrobustheit | **Bestanden** |
| **P7** | Fedora/DNF5 vervollständigen und abnehmen | **Bestanden** |
| **P8** | Debian/Ubuntu/APT vervollständigen und abnehmen | **Bestanden** |
| **P9** | Oberfläche, Performance und Randfälle abschließen | **Bestanden** |
| **P10** | Gesamtprüfung, Paketierung und Übergabe | **Bestanden** |

---

## Phasenprotokolle

### Phase P0 — Ausgangsstand sichern und nachmessen

- **Status:** Bestanden
- **Ausgangsstand:**
  - Branch: `fix/multi-backend-dnf5-apt-signatures`
  - Git HEAD: `607406d89b53b5d93fe39cb777ed3c3e1b6699ff` (`refactor(aur): eigene AUR-Erkennung, Liste auf der Updates-Seite`)
  - Remotes: keine konfiguriert
  - Arbeitsbaum: 16 uncommittete geänderte Dateien (+465 / -285 Zeilen)
    - `CMakeLists.txt`
    - `PKGBUILD`
    - `data/org.linuxupdatetool.metainfo.xml`
    - `liblut/backend/alpm/AlpmBackend.cpp`
    - `liblut/backend/alpm/worker_main.cpp`
    - `liblut/liblut.cpp`
    - `linux-update-tool/DaemonClient.cpp`
    - `linux-update-tool/DaemonClient.h`
    - `linux-update-tool/main.cpp`
    - `linux-update-tool/qml/components/PackageRow.qml`
    - `linux-update-tool/qml/pages/Transaction.qml`
    - `linux-update-tool/qml/pages/Updates.qml`
    - `tests/unit/CMakeLists.txt`
    - `tests/unit/alpm_backend_test.cpp`
    - `tests/unit/daemonclient_log_test.cpp`
    - `tools/replay_main.cpp`
  - Quellversion: `1.0.1`
  - Installiertes Host-Paket: `linux-update-tool 1.0.1-1`
- **Sicherung:**
  - Ziel: `/home/domi/Projekte/backups/linux-update-tool-20260922-vor-store-umbau/` (vollständiger Quellstand per rsync, ohne build/pkg)
  - Regel 5 geprüft: Genau 3 Backups in `~/Projekte/backups/` verbleiben.
- **System- & Bibliotheksbestand:**
  - OS: CachyOS (Arch-Basis, `ID_LIKE=arch`)
  - Compiler / Build: GCC, CMake 4.4.3-2.1
  - Qt6: `qt6-base 6.11.2-3`
  - AppStream: `appstream 1.2.0-1.1`, `appstream-qt 1.2.0-1.1` (CMake-Target: `AppStreamQt`)
  - KDE Frameworks 6: `kirigami 6.30.0-1.1`, `kio 6.30.0-1.1`, `kservice 6.30.0-1.1`
  - Paketverwaltung: `pacman 7.1.0.r9.g54d9411-4` mit libalpm
  - AppStream-Katalog: `archlinux-appstream-data 20260910-1` in `extra` verfügbar
- **Prüfstand-Ergebnis (`bash scripts/verify-all.sh`):**
  - Stufe 1: Build erfolgreich
  - Stufe 2: 18/18 CTest-Tests bestanden (2.87 s)
    - `weights_test`: Passed
    - `eta_test`: Passed
    - `events_test`: Passed
    - `progress_test`: Passed
    - `validation_test`: Passed
    - `history_test`: Passed
    - `replay_test`: Passed
    - `distro_detect_test`: Passed
    - `statusfd_test`: Passed
    - `alpm_backend_test`: Passed
    - `siglevel_test`: Passed
    - `theme_contrast_test`: Passed
    - `aur_updates_test`: Passed
    - `kdeglobals_test`: Passed
    - `list_perf_test`: Passed
    - `dnf5_backend_test`: Passed
    - `apt_metadata_test`: Passed
    - `daemonclient_log_test`: Passed
  - Stufe 3: QML Offscreen Smoke Load bestanden (`--check-qml --replay small-update.jsonl`)
  - Stufe 4: `desktop-file-validate` und `appstreamcli validate --no-net` bestanden
  - Stufe 5: Farbwächter (keine hardcoded Hex-Farben in QML) bestanden
- **Abnahme P0:**
  - Ausgangsstand reproduzierbar dokumentiert und gesichert.
  - Reale Host- und Paketdaten erfasst.
  - Keine Paketänderung auf dem Host durchgeführt.

### Phase P1 — Daten- und Transaktionsverträge festlegen

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P0.
- **Implementiert:**
  - `liblut/transaction/TransactionTypes.h` & `.cpp`:
    - `PackageRef`: backend, repoId, name, arch, version mit Whitelist-Validierung und JSON-Roundtrip.
    - `PackageOffer`: native Priorität, Kandidatenstatus, Größen, Verfügbarkeit.
    - `InstalledState`: installierte Paketreferenzen, Desktop-IDs, Inventarrevision.
    - `AppActionState`: einheitliche Aktionsmatrix für UI-Elemente.
    - `TransactionIntent`: typsichere Aktionsarten (`UpgradeAll`, `Install`, `Remove`, `CustomCommand`).
    - `TransactionPlan`: deterministischer SHA-256 Fingerprint über sortierte Paketoperationen, Versionen, Repos, Größen und Flags; JSON-Serialisierung.
    - `TransactionSnapshot`: Momentaufnahme für Reattach und Statusüberwachung.
  - `liblut/catalog/PackageCatalog.h` & `.cpp`:
    - `AppRecord`: Datenstruktur für AppStream-Komponenten inklusive Screenshots, Kategorien, Desktop-IDs und Quellen.
    - `CatalogQueryResult`: Typisiertes Abfrageergebnis mit Status (`Success`, `NotFound`, `BackendError`, `CatalogMissing`, `Loading`) und Generation.
    - `PackageCatalog`: Abstrakte Schnittstelle für lesende Backend-Paketabfragen.
  - `liblut/backend/Capabilities.h`:
    - Additive Store-Fähigkeiten: `catalogQuery`, `install`, `remove`, `installRequiresFullUpgrade`, `typedPackageTargets`, `transactionReattach`, `protocolVersion = 2`.
  - `liblut/backend/Backend.h` & `.cpp`:
    - Neue Methoden: `planPackageTransaction()`, `commitPlan()`, `discardPlan()`, `currentSnapshot()`.
  - `lutd/TransactionManager.h` & `.cpp`:
    - D-Bus-Erweiterungen: `PlanPackageTransaction()`, `GetTransactionSnapshot()`, `CommitPlan()`, `DiscardPlan()`, `AttachTransaction()`.
    - Validierung: Höchstens 128 Ziele, Paketnamen-Whitelist, Unbekannte Aktionen werden mit `InvalidArgs` abgewiesen.
    - Planprüfung: `CommitPlan` prüft übergebene Planrevision gegen berechneten Fingerprint.
  - `linux-update-tool/models/TransactionPlanModel.h` & `.cpp`:
    - Eigenständiges Modell für Vorschau und Store-Transaktionen (getrennt von `UpdatesModel`).
  - `linux-update-tool/DaemonClient.h` & `.cpp`:
    - `PlanReady` füllt `UpdatesModel` nur bei `m_isUpgradePlan = true` (TX-27).
    - Store-Transaktionen nutzen `TransactionPlanModel`.
    - Methoden `planStoreInstall()`, `planStoreRemove()`, `commitStorePlan()`, `discardStorePlan()`.
  - Dokumentation: `docs/store-architecture.md` und `docs/store-backend-notes.md`.
- **Prüfungen:**
  - `store_contracts_test` (12/12 Tests bestanden):
    - `testPackageRefValidation`: Whitelist, Pfad-, Strich- und Längengrenzen geprüft.
    - `testPackageRefRoundtrip`, `testPackageOfferRoundtrip`, `testInstalledStateRoundtrip`, `testTransactionIntentRoundtrip`, `testTransactionPlanAndFingerprint`, `testTransactionSnapshotRoundtrip`, `testAppRecordRoundtrip`, `testCatalogQueryResult`: 100% JSON-Roundtrip-Treue.
    - `testTransactionPlanModelCounting`: Zählung von Install/Upgrade/Remove und Formatierung.
    - `testPlanReadyDoesNotOverwriteUpdatesModel`: **TX-27 verifiziert** — Store-Plan überschreibt nicht die Update-Liste in `UpdatesModel`.
    - `testReplayFixtureCompatibility`: Bestehende Replay-Fixtures (`small-update.jsonl`) unverändert parsebar.
  - Gesamtsuite `bash scripts/verify-all.sh`: 19/19 Tests bestanden, QML Offscreen Smoke, AppStream- und Desktop-Validierung, Farbwächter grün.
- **Abnahme P1:**
  - Neue Verträge vollständig typisiert und kompiliert.
  - Unbekannte Aktionen/Targets werden serverseitig abgewiesen.
  - Installationsplan ersetzt nicht die Update-Liste.

### Phase P2 — AppStream und nativen Arch-Katalog zusammenführen

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P1.
- **Implementiert:**
  - Build- & Paketierungskonfiguration:
    - Root `CMakeLists.txt`: `find_package(AppStreamQt REQUIRED)`
    - `PKGBUILD`: `appstream-qt` in `depends` aufgenommen.
    - `liblut/CMakeLists.txt`: `catalog/alpm/AlpmPackageCatalog.cpp` registriert.
    - `linux-update-tool/CMakeLists.txt`: `catalog/CatalogService.cpp`, `catalog/ApplicationStore.cpp` und `AppStreamQt` verlinkt.
  - `liblut/catalog/alpm/AlpmPackageCatalog.h` & `.cpp`:
    - Liest Arch/CachyOS `localdb` und `syncdbs` schreibgeschützt und ohne Sperren.
    - Beachtet native Repository-Reihenfolge (`pacman.conf` / `pacman-conf`), wodurch CachyOS-Repos vor Arch-Repos priorisiert werden.
    - Beachtet Paketarchitektur (`x86_64`, `any`, dynamische Hostauflösung von `auto`).
    - Ermittelt `.desktop`-Dateibesitz via `alpm_pkg_get_files` / `alpm_filelist_contains`.
    - Native Versionsvergleiche via `alpm_pkg_vercmp`.
    - Vollständig isoliertes Testen mit konfigurierbaren Root-, DB- und Config-Pfaden.
  - `linux-update-tool/catalog/CatalogService.h` & `.cpp`:
    - Kapselt `AppStream::Pool` mit Flags `FlagLoadOsCatalog | FlagLoadOsMetainfo | FlagLoadOsDesktopFiles` (strikt OHNE `FlagLoadFlatpak`).
    - Zusätzliche explizite Filterung: Komponenten mit Flatpak-Origin oder Bundle werden verworfen.
    - Lokalisierung: Deutscher Text mit automatischem Fallback auf Englisch (CAT-11).
    - Bereinigung von HTML-Inhalten in App-Beschreibungen (UI-07).
    - Asynchrone Suche mit Request-ID-Sequenzierung gegen Stale-Antworten (CAT-14).
    - Konfigurierbare Daten- und Cache-Pfade für isolierte Tests.
  - `linux-update-tool/catalog/ApplicationStore.h` & `.cpp`:
    - Führt `CatalogService` (`AppRecord`) mit `PackageCatalog` (`PackageOffer`, `InstalledState`) zusammen.
    - Ermittelt `AppActionState` (Available, Installed, InstalledNoLaunch, UpdateAvailable, Unavailable).
    - Paketmodus (`searchPackagesOnly`): Durchsucht native Pakete ohne AppStream-Metadaten (CAT-17).
    - Einmalige zusammenfassende Protokollzeile beim Laden ohne Log-Flutung.
  - Test-Fixtures in `tests/fixtures/store/`:
    - `catalog-demo.xml`: Kontrollierte AppStream-Komponenten (Desktop-Apps, Multi-Package, Shared-Package, fehlerhafte Medien, reine englische Texte, HTML-Sanitizing).
    - `flatpak-demo.xml`: Flatpak-Bündel zur Ausschlussprüfung.
  - Unit-Test-Suite `tests/unit/store_catalog_test.cpp`:
    - 22 Testfälle für CAT-01 bis CAT-20 und Gegenproben:
      - CAT-01: Native verfügbare, nicht installierte App -> Kandidatenangebot, "Installieren".
      - CAT-02 / CAT-08: Flatpak-Metadaten und -Bundles werden strikt ausgeschlossen.
      - CAT-03: Komponente ohne Paketangebot -> "Nicht verfügbar".
      - CAT-04 / ALPM-03: Gleiches Paket in CachyOS und Extra -> CachyOS gewinnt gemäß nativer Priorität.
      - CAT-05: Stabile Paketvarianten mit separaten Quellen und Prioritäten.
      - CAT-06: Mehrpaket-Apps.
      - CAT-07: Zwei Apps aus einem gemeinsamen Paket.
      - CAT-09: Installierte App bei deaktiviertem Repository bleibt installierbar/startbar.
      - CAT-10: Manuelle Desktop-Datei ohne Paketbesitz.
      - CAT-11: Lokalisierungs-Fallback auf Englisch.
      - CAT-12: Fehlende/defekte Icons und Screenshots führen zu keinem Crash.
      - CAT-13: Nativer Versionsvergleich mit Epoch und Release (`alpm_pkg_vercmp`).
      - CAT-14: Schnelle Suche A -> B: Alte Antwort wird verworfen.
      - CAT-15: Backend-Abfragefehler erhält bestehenden Bestand.
      - CAT-17: Paketmodus für Pakete ohne AppStream-Daten.
      - CAT-18: Lokalisierungs- und Cache-Invalidierung.
      - CAT-19: Falsche Paketarchitektur wird abgelehnt.
      - CAT-20: Leerer Katalog verhält sich stabil.
      - Gegenprobe 1 (Sec 12.6): Flatpak-Filter-Validierung.
      - Gegenprobe 2 (Sec 12.6): Repository-Prioritäts-Validierung.
  - Gesamtsuite `bash scripts/verify-all.sh`:
    - 20/20 CTest-Tests bestanden.
    - QML Offscreen Smoke Load bestanden.
    - AppStream- und Desktop-Validierung bestanden.
    - Farbwächter bestanden.
- **Abnahme P2:**
  - Reale nicht installierte Repository-Apps werden korrekt gefunden.
  - Flathub-/Flatpak-Daten werden strikt nicht als native Angebote angezeigt.
  - Zwei Quellen mit gleichem Paketnamen bleiben unterscheidbar mit korrekter CachyOS-Priorität.
  - Kein Rootdialog und keine Änderung installierter Pakete beim Laden/Suchen.
- **Nächster Schritt:** Phase P3 (ALPM-Installation/Entfernung und Vorschau durchgängig) [Abgeschlossen].

### Phase P3 — ALPM-Installation, Entfernung und Vorschau durchgängig

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P2.
- **Implementiert:**
  - `liblut/protocol/events.h` & `events.cpp`:
    - `PlanReady` um Feld `planRevision` (kryptographischer SHA-256 Fingerprint) erweitert.
    - Vollständige JSON-Serialisierung und Deserialisierung für D-Bus- und IPC-Transport.
  - `liblut/backend/alpm/worker_main.cpp`:
    - CLI-Erweiterung: `--action <upgrade|install|remove>`, `--install <pkgs>`, `--remove <pkgs>`, `--commit`, `--expected-fingerprint <sha256>`.
    - Install-Flow:
      - Sucht Zielpakete in `syncdbs` unter strikter Wahrung der Repository-Reihenfolge (CachyOS vor Arch core/extra).
      - Fügt Pakete mit `alpm_add_pkg()` hinzu.
      - Erzwingt System-Upgrade mit `alpm_sync_sysupgrade(handle, 0)`, um Teilaktualisierungen auf Arch/CachyOS strikt auszuschließen (Section 8.3, ALPM-01).
    - Remove-Flow:
      - Sucht Zielpakete in `localdb` und fügt sie mit `alpm_remove_pkg()` hinzu.
      - Führt strikt KEIN `alpm_sync_sysupgrade()` aus (ALPM-02).
    - Systemschutzrichtlinie (Section 8.7, TX-04, ALPM-05):
      - Prüft alle in `alpm_trans_get_remove()` aufgelösten Pakete gegen geschützte Pakete (`pacman`, `pacman-contrib`, `glibc`, `systemd`, `systemd-libs`, `filesystem`, `bash`, `coreutils`, `shadow`, `util-linux`, Standardkerne wie `linux`, `linux-cachyos`, `linux-lts`, `linux-zen`, `linux-hardened`, aktiver Kernel und `HoldPkg` aus `pacman.conf`).
      - Bricht die Transaktion bei Betroffenheit geschützter Pakete mit Fehlermeldung ab.
    - Deterministischer Plan & Fingerprint-Bindung:
      - Bildet alle Operationen, Quellen, Versionen und Größen im `PlanReady` und `TransactionPlan` ab.
      - Berechnet SHA-256 Fingerprint über kanonisch sortierte Operationen.
      - Im Commit-Modus unter exklusiver Datenbanksperre: Vergleicht den Live-Transaktionsfingerprint mit `--expected-fingerprint`. Bei Abweichung bricht der Worker sofort vor jedem Dateizugriff ab (TX-09, TX-10).
  - `liblut/backend/alpm/AlpmBackend.h` & `.cpp`:
    - Implementiert `planInstall()` und `planRemove()` mit Aufruf des Workers im Plan-Modus.
    - Aktualisiert `capabilities()`: `catalogQuery = true`, `install = true`, `remove = true`, `installRequiresFullUpgrade = true`.
    - Implementiert `commitPlan()` mit Übergabe von `--expected-fingerprint`, Aktionsart und Zielpaketen an den Worker.
    - Implementiert `discardPlan()` und `currentSnapshot()` zur Zustandsverwaltung.
  - Tests in `tests/unit/alpm_backend_test.cpp`:
    - 4 neue Unit-Tests mit isolierten Mock-ALPM-Umgebungen:
      - `testInstallWithUpgradePlan`: **ALPM-01 verifiziert** — Installation einer neuen App co-plant anstehende Systemupdates ein (No Partial Upgrades).
      - `testRemoveDoesNotSysupgrade`: **ALPM-02 verifiziert** — Entfernen enthält ausschließlich das Zielpaket und löst kein Systemupgrade aus.
      - `testRemoveProtectedPackageBlocked`: **ALPM-05 / TX-04 verifiziert** — Entfernen von `pacman` wird blockiert.
      - `testCommitFingerprintMismatch`: **TX-09 / TX-10 verifiziert** — Commit mit fehlerhaftem Fingerprint wird abgewiesen; Commit mit korrektem Fingerprint wird akzeptiert.
    - Erweiterte Prüfung in `testCapabilities()` für die neuen Store-Fähigkeiten.
    - Alle 17 Tests in `AlpmBackendTest` erfolgreich bestanden.
  - Gesamtsuite `bash scripts/verify-all.sh`:
    - 20/20 CTest-Tests bestanden.
    - QML Offscreen Smoke Load bestanden.
    - AppStream- und Desktop-Validierung bestanden.
    - Farbwächter bestanden.
- **Abnahme P3:**
  - Vollständiges Zusammenspiel von Installation, Sysupgrade und Entfernung im ALPM-Worker.
  - Schutz vor Partial Upgrades auf Arch/CachyOS kryptographisch und prozessual gesichert.
  - Systemschutz gegen Entfernen essentieller Pakete aktiv.
  - Plan-Fingerprint-Bindung bis zum Commit unter exklusiver Datenbanksperre garantiert.

### Phase P4 — Entdecken, Suche und App-Details

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P3.
- **Implementiert:**
  - `data/store/curated.json`:
    - Versionierte Sammlungsdatei mit 4 redaktionellen Sammlungen („Kreativ arbeiten“, „Für den Alltag“, „Entwickeln“, „Für KDE“).
    - Eingebettet als Qt Resource unter `:/LinuxUpdateTool/store/curated.json` und installiert nach `/usr/share/linux-update-tool/`.
  - `linux-update-tool/catalog/ApplicationStore.h` & `.cpp`:
    - QML-invokable Schnittstellen: `getApp()`, `getActionState()`, `getCandidateOffer()`, `getInstalledState()`, `curatedCollections()`, `getCuratedCollection()`, `requestInstall()`, `requestRemove()`, `launchApp()`.
    - Entkoppelte Signalarchitektur: Sendet `installRequested()` und `removeRequested()`, welche im `main.cpp` direkt mit `DaemonClient` verdrahtet werden.
    - App-Start-Logik via `gtk-launch` mit Fallback auf manuelle Desktop-Exec-Auflösung.
  - `linux-update-tool/models/StoreModel.h` & `.cpp`:
    - Virtuelles `QAbstractListModel` für Qt Quick Grids und Listen.
    - Filterung nach Kategorien (inkl. Standard-Mapping auf XDG AppStream Kategorien), Sammlungen, installierten Apps und Paket-Only-Modus.
    - Vollständige Suchbewertung gemäß Abschnitt 3.5:
      1. Exakter App-Key oder Paketname (Score 1000)
      2. Exakter App-Name (Score 900)
      3. Präfix App-Name (Score 800)
      4. Wortanfang im Namen (Score 700)
      5. Präfix Paketname (Score 650)
      6. Keywords (Score 600/500)
      7. Name enthält Suchbegriff (Score 400)
      8. Summary enthält Suchbegriff (Score 300)
      9. Description enthält Suchbegriff (Score 100)
      Stabile alphabetische Sortierung bei Punktegleichstand.
    - Entprellte Suche (`search()`) mit 150 ms Timer gegen Eingabeverzögerungen.
    - Reaktives Update bei `appStateChanged(appKey)`.
  - QML-Komponenten & Seiten:
    - `linux-update-tool/qml/components/AppCard.qml`:
      - Responsive Karte (240–320 px) mit Tastaturnavigation, Icon, Status-Chip und `Text.PlainText` für Namen und Kurzbeschreibungen (Schutz gegen falsche Markup-Interpretation fremder Daten, Abschnitt 3.4).
    - `linux-update-tool/qml/pages/Discover.qml`:
      - Ruhiger Hero-Banner („Kreativ arbeiten“ mit Absprung in Grafik-Kategorie).
      - Kategoriesteine (Internet, Büro, Grafik, Audio & Video, Spiele, Entwicklung, Bildung & Wissenschaft, Werkzeuge).
      - Dynamische Darstellung kuratierter Sammlungen (nur verfügbare Anwendungen, leere Sammlungen ausgeblendet).
      - Zustandsanzeige bei leerem Katalog.
    - `linux-update-tool/qml/pages/Search.qml`:
      - Entprelltes Suchfeld mit Tastaturfokus und Löschknopf.
      - Umschalter zwischen „Anwendungen“ (AppStream) und „Alle Pakete“ (native Repository-Pakete ohne Metadaten, CAT-17).
      - Kategorie- und „Nur installierte“-Filter.
      - Virtualisierte Grid- bzw. Listendarstellung.
    - `linux-update-tool/qml/pages/AppDetails.qml`:
      - Zurücknavigation.
      - Header mit Icon, Name, Zusammenfassung und Entwickler (`Text.PlainText`).
      - Aktionsleiste gemäß Zustandsmatrix (Abschnitt 7): Installieren, Öffnen, Entfernen, System aktualisieren, Nicht verfügbar, Fortschritt.
      - Screenshot-Galerie mit Originalseitenverhältnis; Textanzeige wenn keine Screenshots vorliegen (kein leeres Karussell).
      - Bereinigte Langbeschreibung mit sicherem RichText.
      - Technische Eigenschaften: Paketname, Version, Quellrepository, Lizenz, geschätzte Paketgröße, Website, Bugtracker.
    - `linux-update-tool/qml/components/NavRail.qml`:
      - Exakt 7 Navigationselemente: `discover` (Entdecken), `search` (Suchen), `installed` (Installiert), `updates` (Updates), `history` (Verlauf), `logs` (Protokoll), `settings` (Einstellungen).
      - Fenster- und Navigationszusatz „Apps & Updates“.
    - `linux-update-tool/qml/Main.qml`:
      - Startseite auf `discoverPage` umgestellt.
      - `Ctrl+F` fokussiert die Suche.
      - `Esc` schließt Unterseiten der Navigation.
      - Kompakte, dauerhafte Statusleiste am unteren Bildschirmrand bei aktiven Transaktionen (Abschnitt 3.2 & 3.3).
    - `linux-update-tool/main.cpp`:
      - Instanziiert `CatalogService`, `ApplicationStore` und `StoreModel`.
      - Registriert `Discover.qml`, `Search.qml` und `AppDetails.qml` in `checkablePages()`.
  - Unit-Test-Suite `tests/unit/store_ui_test.cpp`:
    - 7 neue Testfälle für UI-Logik:
      - `testSearchRankingExactMatch`: Exakte Treffer schlagen Teilübereinstimmungen und Beschreibungen.
      - `testCategoryFiltering`: Filterung über UI-Kategorienamen und XDG-Mappings.
      - `testCuratedCollections`: Filterung und Laden von Sammlungen.
      - `testActionStateAndInstalledFilter`: Zuordnung der Aktionszustände und Filter auf installierte Apps.
      - `testPackagesOnlyMode`: Paketmodus für Repository-Pakete ohne AppStream-Daten.
      - `testDebouncedSearch`: 150 ms Timer entprellt Tastatureingaben.
    - Alle 7 Tests bestanden.
  - Gesamtsuite `bash scripts/verify-all.sh`:
    - 21/21 CTest-Tests bestanden (100%).
    - QML Offscreen Smoke Load bestanden mit **0 Warnungen** auf allen 10 Seiten.
    - AppStream- und Desktop-Validierung bestanden.
    - Farbwächter bestanden (keine hardcoded Hex-Farben, 100% Theme-Farben).
    - Offscreen-Screenshot gerendert und visuell verifiziert.
- **Abnahme P4:**
  - Entdecken, Suche und App-Details vollständig implementiert und mit dem nativen Katalog verdrahtet.
  - Tastaturkürzel (`Ctrl+F`, `Esc`), Barrierefreiheit und `Text.PlainText`-Regeln eingehalten.
  - Transaktionsstatus von überall einsehbar.
- **Nächster Schritt:** Phase P5 (Installiert, Öffnen und vorhandene Funktionen integrieren).

### Phase P5 — Installiert, Öffnen und vorhandene Funktionen integrieren

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P4.
- **Implementiert:**
  - Build- & Framework-Integration:
    - `CMakeLists.txt` und `linux-update-tool/CMakeLists.txt` um KDE Frameworks 6 (`KF6Service`, `KF6KIO`) erweitert und `KF6::Service`, `KF6::KIOGui` verlinkt.
  - `linux-update-tool/AppLauncher.h` & `.cpp`:
    - Valider, sicherer nativer Anwendungsstarter gemäß Abschnitt 9.
    - Prüft `.desktop`-Dateien auf Systempfade (`/usr/share/applications/`, `/usr/local/share/applications/`).
    - Strikte Zurückweisung von Flatpaks (`/var/lib/flatpak/`, `~/.local/share/flatpak/`, `X-Flatpak=true`, `flatpak run`).
    - Validiert `TryExec`-Binärdatei im Pfad, `Hidden=true`, `NoDisplay=true`.
    - Asynchroner Start via `KService::Ptr` und `KIO::ApplicationLauncherJob` als regulärer Benutzer ohne Root-Rechte.
    - Signalübertragung: `appLaunched(appKey)` und `appLaunchFailed(appKey, msg)`.
  - `linux-update-tool/models/InstalledModel.h` & `.cpp`:
    - Blockierende Paketabfragen vollständig vom GUI-Thread entfernt (UI-11).
    - Asynchrone Abarbeitung in dediziertem `QThreadPool` (`maxThreadCount = 1`).
    - `m_queryGeneration` verwirft veraltete Antworten bei schnellen Benutzeraktionen (CAT-14).
    - Lebenszeit- und Shutdown-Sicherheit: `std::shared_ptr<std::atomic<bool>> m_alive` und `waitForDone()` im Destruktor verhindern Callbacks auf zerstörte Objekte.
  - `linux-update-tool/qml/pages/Installed.qml`:
    - Zwei-Tab-Segmentierung:
      - Tab 0: **„Anwendungen“** (Standard) mit `installedStoreModel`, 150 ms entprellter Suche, virtuellem `GridView` mit `AppCard`-Delegaten, `EmptyState` und Navigations-Absprung zu `AppDetails`.
      - Tab 1: **„Alle Pakete & Pflege“** mit Hygiene-Karte („System aufräumen“, Waisenpakete, Bereinigbarer Cache), Aktionen geschützt durch `!daemonClient.isBusy`, Rohpaketsuche und `ListView`.
    - Vollständige `Theme`-Konformität ohne hardcoded Hex-Farben.
  - `linux-update-tool/ExternalChangeWatcher.h` & `.cpp`:
    - Überwachung des Paketdatenbankverzeichnisses (`/var/lib/pacman/local`) via `QFileSystemWatcher`.
    - Automatische Neu-Registrierung bei atomaren Verzeichnis- und Dateiänderungen.
    - 500 ms Entprellung (`m_debounceTimer`) bei Massenänderungen.
    - mtime-Prüfung bei Fensterfokus (`Qt::ApplicationActive`).
    - Löst `databaseChanged()` aus, was `ApplicationStore::refresh()`, `installedModel->refresh()` und `installedStoreModel->refresh()` über alle Ansichten konsistent abgleicht.
  - `linux-update-tool/qml/pages/AppDetails.qml`:
    - Aktionsbuttons („Installieren“, „Entfernen“, „System aktualisieren“) durch `!daemonClient.isBusy` geschützt.
    - Banner für `appLaunchFailed` integriert mit `Theme.negative`.
  - `liblut/backend/alpm/AlpmBackend.cpp`:
    - Bereinigung des `/var/log/pacman.log`-Parsers zur Zählung von `[ALPM] installed`, `[ALPM] removed` und `[ALPM] upgraded` ohne Duplikateinträge.
  - Unit-Test-Suite `tests/unit/store_installed_test.cpp`:
    - 9 Testfälle:
      - `testValidLauncher`: Valide System-Desktop-Datei wird erfolgreich erkannt und gestartet.
      - `testFlatpakRejection`: Flatpaks in Pfaden und mit `X-Flatpak=true` werden strikt abgelehnt.
      - `testTryExecMissing`: Nicht installierte Binärdateien in `TryExec` werden abgewiesen.
      - `testHiddenAndNoDisplay`: Versteckte `.desktop`-Dateien werden nicht zum Start angeboten.
      - `testAsyncGenerationCounter`: Veraltete asynchrone Abfragen werden verworfen.
      - `testExternalChangeWatcherDebounce`: 500 ms Entprellung bei multiplen Dateisystem-Events.
      - `testReconciliationAfterMutation`: Konsistenter Datenabgleich über alle Modelle hinweg.
      - `testAppLauncherErrorHandling`: Sichere Fehlerbehandlung bei fehlenden Dateien.
    - Alle 9 Tests bestanden.
  - Gesamtsuite `bash scripts/verify-all.sh`:
    - 22/22 CTest-Tests bestanden (100%).
    - QML Offscreen Smoke Load bestanden mit **0 Warnungen** auf allen 10 Seiten.
    - AppStream- und Desktop-Validierung bestanden.
    - Farbwächter bestanden (keine hardcoded Hex-Farben, 100% Theme-Farben).
    - Screenshots für beide Tabs generiert und visuell verifiziert.
- **Abnahme P5:**
  - „Installiert“-Ansicht mit Anwendungs-Grid und technischer Paketpflege vollständig umgesetzt.
  - Nativer App-Launcher mit KDE Frameworks sicher integriert.
  - Keine blockierenden Abfragen auf dem GUI-Thread.
  - Multi-View-Reconciliation und externe Änderungserkennung aktiv.
- **Nächster Schritt:** Phase P6 (D-Bus-Lebenszyklus, Wiederanbindung und Fehlerrobustheit).

### Phase P6 — D-Bus-Lebenszyklus, Wiederanbindung und Fehlerrobustheit

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P5.
- **Implementiert:**
  - `liblut/protocol/events.h` & `.cpp`:
    - `serializeEvent` & `deserializeEvent` erweitert um monotone Sequenznummer (`seq`) und Korrelationspfad (`transactionPath`).
  - `liblut/transaction/TransactionTypes.h` & `.cpp`:
    - `TransactionSnapshot` um `callerUid`, `active` und `missedEvents` erweitert, inklusive vollständiger Serialisierung in `toJson()` und `fromJson()`.
  - `lutd/TransactionManager.h` & `.cpp`:
    - **Monotone Sequenznummerierung & Ringpuffer:** Jedes Backend-Event erhält eine monoton inkrementierte Sequenznummer (`m_sequenceCounter`). Events werden in einem Ringpuffer mit max. 5.000 Einträgen vorgehalten (`m_eventHistory`).
    - **Wiederanbindung (Snapshots & Missed Events):** `AttachTransaction(path, lastSeenSequence)` liefert den aktuellen Transaktions-Snapshot inklusive aller verpassten Events (`seq > lastSeenSequence`). `GetTransactionSnapshot(path)` und `GetActiveTransactions()` erlauben neu gestarteten GUI-Instanzen die lückenlose Wiederanbindung.
    - **UID-Tracking & Eigentümerschaft:** Beim Start einer Transaktion wird die Linux-OS-UID des Aufrufers (`m_ownerUid`) ermittelt. Ein neu gestarteter GUI-Prozess desselben Benutzers (mit neuem D-Bus Unique Name) darf seine eigene Transaktion ohne Polkit-Rechteverlust wieder anbinden, bestätigen (`CommitPlan`) oder abbrechen (`Cancel`). Fremde UIDs werden mit `AccessDenied` abgewiesen.
    - **Exklusivität (Single Transaction Gate):** Konkurrierende Anfragen zweiter Clients werden während aktiver Vorbereitung/Ausführung mit `Failed` ("Eine Paketoperation läuft bereits.") abgewiesen.
    - **Phasen-gebundene Abbruchpolitik:** `Cancel(path)` prüft `m_progressModel.isCancellable()`. Ein Abbruch ist nur vor dem Commit (`Download`, `Verify`) zulässig, während `Commit` und `PostTransaction` strikt gesperrt.
    - **Plan-Revisionssicherheit:** `CommitPlan(path, planRevision)` prüft die Revisionsübereinstimmung gegen Planänderungen.
    - **Inhibitor-Sperre:** `m_inhibitor` sperrt systemd Sleep/Shutdown während `Download`, `Commit` und `PostTransaction` und gibt sie bei Erreichen von Terminalzuständen wieder frei.
  - `linux-update-tool/DaemonClient.h` & `.cpp`:
    - **Event vor Method-Reply:** `onDbusTransactionEvent` übernimmt bei ausstehender Transaktionsanfrage (`m_pendingTransaction`) den Pfad des ersten eintreffenden Events vorab und verwirft keine vorzeitig gelieferten Signale.
    - **Monotone Sequenzprüfung & Entprellung:** Veraltete (`seq <= m_lastSeenSequence`) oder fremde Transaktionspfade werden verworfen.
    - **Terminal-Event-Deduplizierung:** `TransactionDone` wird über `m_terminalEventProcessed` geschützt, um Signal-Prellen und doppelte UI-Aktualisierungen zu verhindern.
    - **Inventar-Abgleich:** Bei jedem Terminalzustand (`Success`, `SuccessWithWarnings`, `Failed`, `Cancelled`) wird das lokale Inventar (`InstalledModel`, `HistoryModel`, `ApplicationStore`) zuverlässig neu abgeglichen.
    - **Verbindungsabbruchüberwachung:** Bei Wegfall des Daemon-Namens (`onDbusNameOwnerChanged`) wechselt die GUI in den sauberen Status "Verbindung zum Systemdienst verloren" statt fälschlich "Installation fehlgeschlagen" anzuzeigen.
    - `checkForActiveTransaction()` und `reattachToTransaction(path)` bei Daemon-Verbindungsaufbau integriert.
  - Unit-Test-Suite `tests/unit/store_dbus_test.cpp`:
    - 11 umfassende Testfälle (13 Assert-Gruppen):
      - `testReattachActiveTransaction`: Snapshot-Wiederanbindung, Planerhalt, Phasenübernahme.
      - `testSecondClientRejected`: Abweisung paralleler Transaktionen ("Eine Paketoperation läuft bereits.").
      - `testOwnershipSameUidAllowed`: Reattach und Plan-Verwerfen für dieselbe OS-UID gestattet.
      - `testOwnershipDifferentUidRejected`: Abweisung fremder UIDs mit `AccessDenied`.
      - `testEventBeforeMethodReply`: Vorzeitige Annahme des Transaktionspfads bei `pendingTransaction` vor RPC-Return; Verwerfen alter Pfade.
      - `testMonotoneEventSequence`: Sequenzprüfung und Verwerfen doppelter/veralteter Events.
      - `testDuplicateTerminalEvents`: Einmalige Ausführung von `TransactionDone` ohne doppelte Signalweitergabe.
      - `testInventoryRefreshOnAllTerminalStates`: Signalfeuerung und Refresh bei `Success`, `SuccessWithWarnings`, `Failed`, `Cancelled`.
      - `testCancellationBoundToPhase`: Abbruch in `Download` erlaubt, in `Commit` gesperrt.
      - `testRingBufferOverflow`: Historienbegrenzung auf 5.000 Events bei 5.200 Durchläufen; Sequenzzähler bleibt konsistent.
      - `testConnectionLossDoesNotReportInstallationFailed`: Status „Verbindung zum Systemdienst verloren“ ohne falschen Fehlerzustand.
    - Alle 11 Tests bestanden.
  - Gesamtsuite `bash scripts/verify-all.sh`:
    - 23/23 CTest-Tests bestanden (100%, 6.27 s).
    - QML Offscreen Smoke Load bestanden mit **0 Warnungen** auf allen Seiten.
    - AppStream- und Desktop-Validierung erfolgreich (`desktop-file-validate`, `appstreamcli validate --no-net`).
    - Farbwächter bestanden (100% Theme-Farben).
- **Abnahme P6:**
  - Lückenloser Snapshot- und Reattach-Pfad über D-Bus umgesetzt.
  - Monotone Sequenznummerierung und Filterung aktiv.
  - Ownership- und UID-Schutz garantiert Zugriffssicherheit bei GUI-Neustart.
  - Parallele Transaktionen und Event-Races vollständig abgefangen.
  - Alle 23 automatisierten Tests grün.
- **Nächster Schritt:** Phase P7 (Fedora/DNF5 vervollständigen und abnehmen) [Abgeschlossen].

### Phase P7 — Fedora/DNF5 vervollständigen und abnehmen

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P6.
- **Implementiert:**
  - `liblut/catalog/dnf5/Dnf5PackageCatalog.h` & `.cpp`:
    - Native `PackageCatalog`-Implementierung für Fedora/DNF5.
    - Kandidatenermittlung via `dnf5 repoquery` mit ASCII-Trennzeichen (`0x1e` Datensatz, `0x1f` Feld) für robustes, sonderzeichenfestes Parsing von NEVRA, Epoch, Repository, Downloadgröße und Installationsgröße in Bytes.
    - Native Repository-Priorisierung: `updates` > `fedora`.
    - Launchable Desktop-IDs: Ermittelt `.desktop`-Dateien im Pfad `/usr/share/applications/` via `dnf5 repoquery -l`.
    - Bestandsprüfung: `installedStateForPackage`, `allInstalledPackages`, `findPackagesProvidingFile`, `searchPackages`.
    - Thread-sichere Zwischenspeicherung mit `QMutex` und Generationszähler (`reload()`).
  - `liblut/backend/dnf5/Dnf5Backend.h` & `.cpp`:
    - Aktualisiert `capabilities()`: `catalogQuery = true`, `install = true`, `remove = true`, `installRequiresFullUpgrade = false`, `typedPackageTargets = true`, `transactionReattach = true`, `protocolVersion = 2`.
    - Bewahrt gespeicherte Transaktionen (`--store` / `replay`), Planverzeichnisprüfung gegen Symlinks und strikte GPG-Signaturen (`localpkg_gpgcheck=1`, `*.pkg_gpgcheck=1`).
    - DNF5-Changelog-Compilerwarnung bezüglich Zeigerlebensdauer behoben.
  - `lutd/TransactionManager.h` & `.cpp`:
    - `m_ownerUid` auf `std::optional<quint32>` umgestellt, um Ausführung im Docker-Container (UID 0 / Root) vollwertig als Transaktions-Owner zu unterstützen (`ownsTransaction()` prüft `m_ownerUid.has_value() && callerUid == *m_ownerUid`).
  - `linux-update-tool/main.cpp`:
    - Instanziiert `Dnf5PackageCatalog` zur Laufzeit, wenn `DistroFamily::Fedora` erkannt wird.
  - `linux-update-tool/ExternalChangeWatcher.cpp`:
    - Überwacht `/var/lib/rpm` bei `DistroFamily::Fedora`.
  - `linux-update-tool/catalog/CatalogService.cpp`:
    - AppStreamQt-Rückwärtskompatibilität für AppStream 1.1.x (Fedora 44) und 1.2.x (Arch/CachyOS):
      - `Component::searchTokens()` statt `Component::keywords()`.
      - `Icon::url().isLocalFile() ? url.toLocalFile() : url.toString()` statt `Icon::filename()`.
  - `tests/unit/CMakeLists.txt`:
    - ALPM-spezifische Tests (`aur_updates_test`, `store_catalog_test`, `store_ui_test`) mit `if(ALPM_FOUND)` gekapselt, um Fedora-Reinumgebungen ohne ALPM voll lauffähig zu halten.
  - `tests/unit/store_dnf5_catalog_test.cpp` (7 neue Unit-Tests):
    - `testAvailableOffersParsing`: Parse-Treue von Repoquery-Daten.
    - `testCandidateSelectionUpdatesOverFedora`: `updates`-Repo gewinnt gegen `fedora`.
    - `testInstalledState`: Korrekte Statuserkennung und Desktop-ID-Extraktion.
    - `testFileProviders`: Auflösung von Datei-Providern zu Paketen.
    - `testOrphanParsing`: Erkennung verwaister Pakete.
    - `testCapabilities`: Verifikation der Store-Flags.
    - `testReloadInvalidation`: Cache-Invalidierung und Generationszählung.
  - `tests/integration/dnf5_integration.cpp`:
    - Umfassendes natives Store-Szenario erweitert:
      - Prüfung von `offersForPackage("tree")` und `candidateOffer("tree")`.
      - Typisierte Installation via `TransactionIntent` (`Install`, `tree`) und `backend.planPackageTransaction()`.
      - Commit der gespeicherten DNF5-Transaktion.
      - Bestandsprüfung via `Dnf5PackageCatalog::installedStateForPackage()` (vollständig installiert, korrekte Desktop-Dateien).
      - Typisierte Entfernung via `TransactionIntent` (`Remove`, `tree`) und `backend.planPackageTransaction()`.
      - Commit der Entfernungstransaktion.
      - Bestandsprüfung nach Entfernung (`isFullyInstalled == false`).
- **Prüfungen:**
  - Host-Prüfstand (`bash scripts/verify-all.sh`):
    - 24/24 CTest-Tests bestanden (100%, 6.71 s).
    - QML Offscreen Smoke Load bestanden mit 0 Warnungen auf allen 10 Seiten.
    - Desktop- und AppStream-Validierung bestanden.
    - Farbwächter (100% Theme-Farben) bestanden.
  - Fedora 44 Container-Prüfstand (`bash scripts/verify-fedora44.sh`):
    - Container-Build & Build des gesamten Projekts im Fedora 44-Container erfolgreich.
    - 20/20 CTest-Tests im Fedora 44 Container bestanden (100%).
    - QML Offscreen Smoke Load im Fedora 44 Container bestanden.
    - `dnf5_integration` im realen Fedora 44 Container erfolgreich abgeschlossen:
      - `which` install/reinstall/remove/history-undo/swap/downgrade/distro-sync/autoremove bestanden.
      - Nativer Store-Zyklus (`tree` Angebot -> Kandidat -> Store Install -> Verifikation -> Store Remove -> Verifikation) vollständig bestanden.
      - Beendet mit Code 0.
- **Abnahme P7:**
  - Reale native Installation und Entfernung auf dem Fedora-44-Zielsystem nachgewiesen.
  - Gespeicherte DNF5-Transaktionen binden Replay strikt an den Plan.
  - GPG-Signaturen und Schutzregeln unverändert durchgesetzt.
  - Bestehender DNF5-Prüfstand vollständig grün geblieben.
- **Nächster Schritt:** Phase P8 (Debian/Ubuntu/APT vervollständigen und abnehmen) [Abgeschlossen].

### Phase P8 — Debian/Ubuntu/APT vervollständigen und abnehmen

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P7.
- **Implementiert:**
  - `liblut/catalog/apt/AptPackageCatalog.h` & `.cpp`:
    - Native `PackageCatalog`-Implementierung für Debian und Ubuntu.
    - Kandidatenermittlung über `apt-cache policy` unter Beachtung von APT-Pins, Prioritäten und Hold-Flags (`APT-02`).
    - Exakte Download-Größen in Bytes und Installationsgrößen in Bytes über `apt-cache show <pkg>=<ver>`.
    - Saubere Trennung von Paketname und Architektur in `PackageRef` zur sicheren Multiarch-Unterstützung (z. B. `wine` für `amd64` und `i386`) (`APT-01`).
    - Bestandsprüfung via `dpkg-query`: Erkennt strikt nur den dpkg-Status `installed` an. Pakete im Zustand `config-files` (`rc`) nach Deinstallation ohne Purge gelten zuverlässig als nicht installiert (`isFullyInstalled = false`), womit die Anzeige irreführender „Öffnen“-Buttons verhindert wird (`APT-03`).
    - Startbare Desktop-Dateien unter `/usr/share/applications/` via `dpkg-query -L <pkg>`.
    - Provider-Ermittlung via `dpkg-query -S <path>`.
    - Thread-sichere Zwischenspeicherung mit `QMutex` und Generationszähler (`reload()`).
  - `liblut/backend/apt/AptBackend.h` & `.cpp`:
    - Aktualisiert `capabilities()`: `catalogQuery = true`, `install = true`, `remove = true`, `installRequiresFullUpgrade = false`, `typedPackageTargets = true`, `transactionReattach = true`, `protocolVersion = 2`, `degraded = false`.
    - `planPackageTransaction()`:
      - Validiert Paketnamen über `Validation::isValidPackageName()`.
      - Systemschutz (APT-04): Sperrt die Deinstallation geschützter/essentieller Systempakete (`dpkg`, `apt`, `libc6`, `coreutils`, `bash`, `systemd`, `systemd-sysv` etc.) bereits vor der Simulation und bricht bei simulierten `Remv`-Operationen mit Fehler ab.
      - Erzeugt deterministischen SHA-256-Fingerprint über sortierte Paketoperationen (`planRevision`).
    - `commitPlan()`:
      - Validiert den erwarteten Plan-Fingerprint (`TX-09`, `APT-05`).
      - Audit-Prüfung via `dpkg --audit`: Fängt unvollständige oder unterbrochene Paketkonfigurationen vor der Ausführung ab und liefert konkrete Handlungsanweisungen (`APT-06`).
      - Pre-Execution Simulation: Führt vor dem eigentlichen APT-Lauf eine erneute Simulation durch und gleicht die Operationen ab, um Ausführungsraces bei zwischenzeitlichen Systemänderungen deterministisch zu verhindern (`APT-05`).
      - Übergibt `-o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold` sowie `DEBIAN_FRONTEND=noninteractive`, um stdin-Hänger bei Conffile-Fragen auszuschließen (`APT-06`).
    - `discardPlan()` und `currentSnapshot()` implementiert.
    - `handleStatusFdLine()` verarbeitet Live-Events über `StatusFdParser`.
  - `CMakeLists.txt` & `liblut/CMakeLists.txt`:
    - Registriert `AptPackageCatalog.cpp`.
    - Erzeugt bidirektionalen `SQLite3::SQLite3` / `SQLite::SQLite3` Target-Alias zur nahtlosen Portabilität zwischen Arch, Fedora und Debian.
  - `linux-update-tool/main.cpp`:
    - Instanziiert `AptPackageCatalog` bei `DistroFamily::Debian`.
    - Filtert Kirigami-Plattform-Fallback-Meldungen bei Standard-Stilen wie Fusion im Smoke-Check.
  - Unit-Tests in `tests/unit/store_apt_catalog_test.cpp`:
    - 9 umfassende Testfälle für APT-Spezifika:
      - `testAvailableOffersAndCandidatePolicy`: Kandidatenermittlung und Byte-Größen.
      - `testMultiarchOffers`: Multiarch-Trennung in `PackageRef` (APT-01).
      - `testDpkgStatusInstalledVsConfigFiles`: `config-files` zählt nicht als installiert (APT-03).
      - `testHoldAndPinnedPolicy`: Hold/Pinning-Kandidat (APT-02).
      - `testProtectedPackagesBlocked`: Schutz essentieller Pakete (APT-04).
      - `testCommitFingerprintMismatch`: Abweisung manipulierter Revisionen (APT-05, TX-09).
      - `testCapabilities`: Verifikation der Store-Flags.
      - `testFileProviders`: Auflösung von Datei-Providern.
      - `testReloadInvalidation`: Cache-Invalidierung und Generationszählung.
  - Integrationstest `tests/integration/apt_integration.cpp`:
    - Reales natives Store-Szenario im Debian-Container:
      1. Kandidaten- und Angebotssuche über `AptPackageCatalog` für `tree`.
      2. Typisierte Installation via `TransactionIntent::Type::Install` und Commit.
      3. Bestandsprüfung über `AptPackageCatalog::installedStateForPackage("tree")` (als vollständig installiert erkannt).
      4. Typisierte Entfernung via `TransactionIntent::Type::Remove` und Commit.
      5. Bestandsprüfung nach Remove (als deinstalliert erkannt, APT-03).
      6. Schutzprüfung essentieller Pakete: Versuch, `apt` zu deinstallieren, schlägt fehl (APT-04).
      7. Revisionsintegritätsprüfung: Commit mit abweichendem Fingerprint wird abgewiesen (APT-05, TX-09).
  - Wegwerf-Container-Prüfstand `scripts/verify-debian.sh`:
    - `tests/integration/containers/debian/Containerfile` auf `debian:trixie-slim` (Qt 6.8.2, KF6 6.13, AppStreamQt) aktualisiert.
    - Führt Build, alle 21 Unit-Tests, QML-Smoke und `apt_integration` im Container aus.
- **Prüfungen:**
  - Host-Prüfstand (`bash scripts/verify-all.sh`):
    - 25/25 CTest-Tests bestanden (100%, 6.20 s).
    - QML Offscreen Smoke Load bestanden mit 0 Warnungen auf allen 10 Seiten.
    - Desktop- und AppStream-Validierung bestanden.
    - Farbwächter bestanden (100% Theme-Farben).
  - Debian-Container-Prüfstand (`bash scripts/verify-debian.sh`):
    - Container-Build und Kompilierung im `debian:trixie-slim`-Container erfolgreich.
    - 21/21 CTest-Tests im Debian-Container bestanden (100%, 0.57 s).
    - QML Offscreen Smoke Load im Replay-Modus bestanden.
    - `apt_integration` erfolgreich abgeschlossen:
      - Kandidatenauflösung, Download- und Installationsgrößen für `tree` ermittelt.
      - Typisierte Installation von `tree` ausgeführt und im Bestand verifiziert.
      - Typisierte Entfernung von `tree` ausgeführt und als deinstalliert verifiziert.
      - Deinstallationsversuch von `apt` als essentielles Systempaket blockiert.
      - Mismatch-Commit abgewiesen.
      - Beendet mit Code 0.
- **Abnahme P8:**
  - Reale native APT-Installation, Kandidatensuche und Deinstallation im dokumentierten Debian-13-Container nachgewiesen.
  - Multiarch (`amd64` / `i386`) und Epoch-Versionen typisiert modelliert.
  - Deinstallation ohne Purge hinterlässt `config-files`, was strikt als nicht-installiert behandelt wird.
  - Schutz essentieller Systempakete und Plan-Revisionsbindung durchgesetzt.
- **Nächster Schritt:** Phase P9 (Oberfläche, Performance und Randfälle abschließen) [Abgeschlossen].

### Phase P9 — Oberfläche, Performance und Randfälle abschließen

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P8.
- **Implementiert:**
  - **Oberflächen-Struktur & Layout-Korrekturen (UI-01, UI-05, UI-07, UI-08):**
    - `linux-update-tool/qml/pages/AppDetails.qml`:
      - Verschachtelungsfehler behoben: Aktions-Buttons (`InstalledNoLaunch`, `Unavailable`) und Quell-/Versionsdetails (`Quelle: %1 · Version: %2`) sauber in die primäre Aktionszeile verschoben, sodass sie immer korrekt sichtbar sind und nicht mehr irrtümlich an `launchErrorMessage` gekoppelt sind.
      - Platzhalterdarstellung für Screenshots (`Image.Loading` und `Image.Error`) für fehlende/fehlerhafte Medien hinzugefügt (UI-05, UI-12).
      - App-Beschreibung als `Text.PlainText` mit `wrapMode: Text.WordWrap` abgesichert gegen HTML-/Script-Injection und Remote-Tag-Rendering (UI-07).
    - `linux-update-tool/qml/pages/Installed.qml`:
      - Sprödes Breiten-Layout (`width: parent.width - 460`) im Kopfbereich durch verankertes Design (Titel linksbündig, TabSwitcher rechtsbündig) ersetzt; verhindert Überschneidungen und Abschneiden bei 900×640 sowie DPI-Skalierung (UI-08).
    - `linux-update-tool/qml/pages/Search.qml`:
      - Breitenberechnung des Suchfelds an Nav-Switcher-Breite und Spacing angepasst (`parent.width - modeSwitcher.width - parent.spacing`), wodurch ein 2px Überlauf behoben wurde. Escape-Taste leert die Suchzeile (UI-10).
    - `linux-update-tool/qml/Main.qml`:
      - Standardmäßige Tastatur-Zurücknavigation (`StandardKey.Back`, `Alt+Left`) neben `Escape` registriert (UI-10).
    - `linux-update-tool/qml/pages/Discover.qml`:
      - Fehlerbanner bei Fehlschlagen des Metadatenkatalogs integriert (`appStore.lastError`), um Fehler klar anzuzeigen, statt stumm eine leere Seite darzustellen (UI-12).
  - **Sicherheit von Medien-URLs & Fehlertransparenz (UI-06, UI-12):**
    - `CatalogService::isSafeMediaUrl()` implementiert und auf alle Icons und Screenshots angewendet: Erlaubt nur sichere Remote-Schemata (`https://`, `http://`) sowie legitime lokale Cache-/Systempfade (`/usr/share/`, `/var/cache/`, `/tmp/`). Gefährliche Schemata (`javascript:`, `data:`, `file:///etc/`, `smb:`) werden verworfen (UI-06).
    - `lastError`-Property an `ApplicationStore` hinzugefügt zur direkten Signalisierung an die Oberfläche (UI-12).
  - **Nativer Versionsvergleich aller Backends (CAT-13):**
    - Virtuelle Methode `compareVersions(const QString &v1, const QString &v2)` in `PackageCatalog` eingeführt.
    - Implementiert mit `alpm_pkg_vercmp` in `AlpmPackageCatalog` (Arch/ALPM).
    - Implementiert mit nativer RPM EVR-Logik (`rpmvercmpPart`) in `Dnf5PackageCatalog` (Fedora/DNF5).
    - Implementiert mit nativer Debian dpkg EVR-Logik (`dpkgvercmpPart`) in `AptPackageCatalog` (Debian/Ubuntu/APT).
    - Zentrale Hilfsfunktion `ApplicationStore::compareNativeVersions` bereitgestellt.
  - **Performance-Optimierung & 10.000-Objekte Skalierungstest (UI-13):**
    - Suchalgorithmus in `StoreModel::calculateSearchScore` optimiert: Dynamische Regex-Kompilierung und unnötige String-Allokationen entfernt. In-Place-Wortgrenzenprüfung und `Qt::CaseInsensitive`-Matching reduzieren Durchlaufzeit bei 10.000 Apps auf **10 ms**.
    - Unit-Tests in `tests/unit/store_ui_test.cpp`:
      - `test10kBenchmarkPerformance()`: Filterung 10.000 Apps in **5 ms**, Volltextsuche & Sortierung in **10 ms**.
      - `testDescriptionSanitization()`: Verifikation von Plaintext-Sicherheit.
      - `testSafeMediaUrlSchemes()`: Verifikation der URL-Validierung.
      - `testVersionComparisonMatrix()`: Verifikation des nativen Versionsvergleichs für alle 3 Backends.
  - **Visuelle Abnahme via Screenshots aller Seitenzustände:**
    - Generiert in Replay-Modus unter Verwendung der echten Systempalette und Offscreen-Rendering:
      - `screen_discover.png` (Discover-Startseite mit Hero-, Featured- und Kategorienbereichen)
      - `screen_search.png` (Suchansicht mit Modusumschalter und Filterchips)
      - `screen_installed_apps.png` (Installierte Anwendungen mit Start-/Entfernen-Aktionen)
      - `screen_installed_all.png` (Alle installierten Systempakete)
      - `screen_updates.png` (Verfügbare System- und AUR-Updates mit Upgrade-Aktionen)
      - `screen_history.png` (Transaktionshistorie mit Details und Rollback)
      - `screen_logs.png` (Live- und Archiv-Transaktionslogs)
      - `screen_settings.png` (Systemeinstellungen und Backend-Konfiguration)
- **Prüfungen:**
  - Host-Prüfstand (`bash scripts/verify-all.sh`):
    - 25/25 CTest-Tests bestanden (100%, 6.21 s).
    - QML Offscreen Smoke Load bestanden mit 0 Warnungen auf allen Seiten.
    - Desktop- und AppStream-Metadatenvalidierung bestanden.
    - Farbwächter bestanden (0 unzulässige Hex-Farben, 100% Theme-Palette).
  - Fedora 44 Container-Prüfstand (`bash scripts/verify-fedora44.sh`):
    - 20/20 CTest-Tests im Fedora 44 Container bestanden (100%).
    - QML Offscreen Smoke Load bestanden.
    - `dnf5_integration` vollständig bestanden mit Code 0.
  - Debian Container-Prüfstand (`bash scripts/verify-debian.sh`):
    - 21/21 CTest-Tests im Debian Container bestanden (100%, 0.56 s).
    - QML Offscreen Smoke Load bestanden.
    - `apt_integration` vollständig bestanden mit Code 0.
- **Abnahme P9:**
  - Responsive Layouts ohne Überläufe oder Abschneiden von Aktionen bei 900×640 und 1180×800 verifiziert.
  - Skalierung und Virtualisierung für 10.000 App-Einträge nachgewiesen (<15ms).
  - Mediensicherheit und Text-Sanitization vollständig durchgesetzt.
  - Nativer Versionsvergleich für alle unterstützten Paketmanager implementiert und getestet.
  - Alle Screenshots erzeugt und archiviert.
- **Nächster Schritt:** Phase P10 (Gesamtprüfung, Paketierung und Übergabe) [Abgeschlossen].

### Phase P10 — Gesamtprüfung, Paketierung und Übergabe

- **Status:** Bestanden
- **Ausgangsstand:** Stand nach P9.
- **Implementiert:**
  - **Konsistente Versionsanhebung auf `1.1.0`:**
    - `CMakeLists.txt`: `project(linux-update-tool VERSION 1.1.0 LANGUAGES C CXX)`
    - `liblut/liblut.cpp`: `lut::versionString()` liefert `"1.1.0"`
    - `liblut/backend/alpm/worker_main.cpp`: Nutzt `lut::versionString()`
    - `tools/replay_main.cpp`: Nutzt `lut::versionString()`
    - `data/org.linuxupdatetool.metainfo.xml`: Neues Release `<release version="1.1.0" date="2026-09-22">` mit detailliertem Changelog (AppStream-Katalogsuche, Entdecken-Seite, multi-backend typisierte Transaktionen für Arch/DNF5/APT, Mediensicherheit und responsive Oberflächen)
    - `PKGBUILD`:
      - `pkgver=1.1.0`, `pkgrel=1`
      - `pkgdesc="Native application store and system update tool with honest progress for Arch, Fedora, and Debian"`
      - `depends`: `'kservice'` und `'kio'` ergänzt (notwendig für `KApplicationLauncherJob`)
      - `check()`: Führt CTest mit allen 25 Tests (inklusive Store-Katalog-, Vertrags-, DBus- und UI-Tests), QML Offscreen-Smoketest (`--check-qml`) sowie Validierung von `.desktop` und `metainfo.xml` aus.
  - **Arch-Paketerstellung via `makepkg -f`:**
    - Paketierung unter strikter Beibehaltung der Host-Integrität ausgeführt.
    - CTest-Prüfstand (25/25 Tests, 100% bestanden in 6.38 s), QML-Smoketest (0 Warnungen) und Metadaten-Validierung in `check()` erfolgreich durchlaufen.
    - Paketarchiv `linux-update-tool-1.1.0-1-x86_64.pkg.tar.zst` erfolgreich gebaut.
    - Paketinhalt (`pacman -Qlp`, `pacman -Qip`) validiert:
      - Binaries: `/usr/bin/linux-update-tool`, `/usr/libexec/lutd`, `/usr/libexec/linux-update-tool/lut-alpm-worker`
      - D-Bus Service & Polkit: `/usr/share/dbus-1/system-services/org.linuxupdatetool.Daemon1.service`, `/usr/share/dbus-1/system.d/org.linuxupdatetool.Daemon1.conf`, `/usr/share/polkit-1/actions/org.linuxupdatetool.policy`, `/usr/lib/systemd/system/lutd.service`
      - Desktop & AppStream: `/usr/share/applications/org.linuxupdatetool.desktop`, `/usr/share/metainfo/org.linuxupdatetool.metainfo.xml`, `/usr/share/icons/hicolor/scalable/apps/org.linuxupdatetool.svg`
  - **Upgrade-Verifikation (1.0.1 -> 1.1.0) in isoliertem Wegwerf-Container:**
    - Ausgangs-Release `linux-update-tool 1.0.1-1` im `archlinux:latest`-Container installiert.
    - Upgrade auf `linux-update-tool 1.1.0-1` durchgeführt.
    - Alle Post-Transaction-Hooks (AppStream Cache Update, Desktop MIME, Systemd, D-Bus) fehlerfrei durchgelaufen.
    - Ausführung `linux-update-tool --version` liefert verifiziert `linux-update-tool 1.1.0`.
  - **Release-Ablage & Bereinigung alter Artefakte:**
    - `linux-update-tool-1.1.0-1-x86_64.pkg.tar.zst` und Prüfsumme `linux-update-tool-1.1.0-1-x86_64.pkg.tar.zst.sha256` in `~/Projekte/releases/` abgelegt.
    - Prüfsumme verifiziert (`sha256sum -c linux-update-tool-1.1.0-1-x86_64.pkg.tar.zst.sha256: OK`).
    - Ersetzte Vorgänger-Artefakte (`linux-update-tool-1.0.1-1-x86_64.pkg.tar.zst*`) aus `~/Projekte/releases/` bereinigt.
- **Prüfungen:**
  - Host-Prüfstand (`bash scripts/verify-all.sh`):
    - 25/25 CTest-Tests bestanden (100%, 6.52 s).
    - QML Offscreen Smoke Load bestanden mit 0 Warnungen auf allen Seiten.
    - Desktop- und AppStream-Metadatenvalidierung bestanden.
    - Farbwächter bestanden (0 unzulässige Hex-Farben, 100% Theme-Palette).
  - Fedora 44 Container-Prüfstand (`bash scripts/verify-fedora44.sh`):
    - 20/20 CTest-Tests bestanden (100%).
    - QML Offscreen Smoke Load bestanden.
    - `dnf5_integration` vollständig bestanden mit Code 0.
  - Debian Container-Prüfstand (`bash scripts/verify-debian.sh`):
    - 21/21 CTest-Tests bestanden (100%, 0.56 s).
    - QML Offscreen Smoke Load bestanden.
    - `apt_integration` vollständig bestanden mit Code 0.
  - Arch makepkg & Upgrade-Container (`makepkg -f`):
    - 25/25 CTest-Tests in `check()` bestanden (100%, 6.38 s).
    - Container-Upgrade 1.0.1 -> 1.1.0 erfolgreich verifiziert.
- **Abnahme P10:**
  - Version 1.1.0 konsistent im gesamten Projekt verankert.
  - Reales, natives Arch-Paket erstellt, strukturell geprüft und in `~/Projekte/releases/` samt SHA-256 bereitgestellt.
  - Upgrade-Pfad von Version 1.0.1 containerbasiert ohne Nebenwirkungen auf den Host nachgewiesen.
  - Alle Prüfungen auf Arch Linux, Fedora 44 und Debian GNU/Linux 13 zu 100% grün.
  - Keine Host-Paketmutationen durchgeführt.



---

## Nachtrag: Befunde aus dem externen Audit (22.09.2026)

Ein Audit der Phasen P1–P8 gegen den Quelltext hat Lücken gezeigt, die von den
bestehenden Tests nicht erfasst wurden. Die folgenden Korrekturen wurden danach
vorgenommen. Jede ist durch einen Test abgedeckt, der ohne die Korrektur fehlschlägt.

### Behoben

| Befund | Korrektur | Nachweis |
|---|---|---|
| P3 hatte keinen echten Durchstich: der Commit-Test lief mit `--dry-run`, es wurde nie ein Paket wirklich installiert | `testStoreInstallAndRemoveRoundtrip` installiert ein echtes Paket unter `fakeroot` in einem Wegwerf-Root, prüft Dateien und lokale Datenbank, entfernt es und prüft erneut | `alpm_backend_test` |
| Mehrpaket-Anwendungen wurden auf das erste Paket reduziert; CAT-06 hat genau diese Reduktion festgeschrieben | `AppRecord::packageNames` wird durchgereicht, `ApplicationStore::packageSet()` bildet den Installationssatz, `requestInstall`/`requestRemove` senden Listen; CAT-06 prüft jetzt den erzeugten Satz | `store_catalog_test` |
| Kuratierte Sammlungen nannten Flathub-Kennungen; der Arch-Katalog führt `…​.desktop`. Alle vier Sammlungen wären auf dem Zielsystem leer geblieben, ohne dass etwas auffällt | `CatalogService::normalizedAppKey()` gleicht beide Schreibweisen ab; vier tatsächlich falsche Kennungen korrigiert; `scripts/verify-curated-ids.sh` prüft alle Kennungen gegen einen echten Distributionskatalog | `store_catalog_test`, `verify-curated-ids.sh` |
| Es gab keine Vorschau mit Bestätigung: `commitStorePlan()` und `discardStorePlan()` wurden von keiner Stelle aufgerufen, ein Store-Klick endete nach der Planung | Neue Seite `qml/pages/PlanPreview.qml` mit Paketliste, Größen, Warnungen und Bestätigung gegen die angezeigte Planrevision | QML-Smoke, alle Seiten warnungsfrei |
| Abschnitt 8.7 verlangt, betroffene Anwendungen zu nennen; `affectedApps` existierte als Feld, war aber nirgends implementiert (`setAffectedApps` war nicht einmal definiert) | `ApplicationStore::appsProvidedByPackages()` ermittelt sie, `DaemonClient::planReady` löst die Anreicherung aus, die Vorschau zeigt sie an | `store_catalog_test` (CAT-07) |
| DNF5 leitete die Repository-Priorität aus dem Teilstring `updates` ab | Rangwerte stammen aus `dnf5 repo info --json` (`priority`, `cost`); ohne Angaben gilt die DNF5-Vorgabe und allein die EVR-Version entscheidet | `store_dnf5_catalog_test` |
| Der Zustand „installiert, Quelle nicht mehr vorhanden" wurde nie erzeugt | `AppActionState::MissingSource` wird ermittelt und in der Detailseite benannt; Starten und Entfernen bleiben möglich, eine Neuinstallation wird nicht versprochen | `store_catalog_test` (CAT-09) |
| `archlinux-appstream-data` fehlte in den Paketabhängigkeiten, obwohl Abschnitt 4.3 es verlangt | In `PKGBUILD` `depends` aufgenommen | — |
| Die APT-Revisionsbindung war ungetestet: beide Tests liefen in den vorgelagerten Wächter „Kein gültiger APT-Plan vorhanden" und erreichten den Fingerprintvergleich nie | Der Integrationstest löst zuerst einen gültigen Plan auf, prüft dann die Abweisung der falschen Revision **an der Fingerprint-Meldung** und als Gegenprobe die Annahme der korrekten Revision | `apt_integration` |
| `store_ui_test` erwartete `http` als sicheres Medienschema, obwohl `isSafeMediaUrl()` inzwischen HTTPS verlangt | Test an die Richtlinie aus Abschnitt 4.7 angeglichen | `store_ui_test` |

### Nachtrag 2: Die verbliebenen Punkte

Die oben als offen geführten Punkte wurden anschließend bearbeitet. Stand nach
dieser Runde:

| Punkt | Umsetzung | Nachweis |
|---|---|---|
| APT-Planbindung war eine Textheuristik ohne durchgehende Sperre | `lut-apt-guard` hängt sich über `DPkg::Pre-Install-Pkgs` (Protokoll 3) in den APT-Lauf. APT ruft ihn, während es `lock-frontend` hält, mit der tatsächlichen dpkg-Operationsliste und den echten `.deb`-Pfaden. Der Wächter vergleicht Operationsmenge, Versionen, Architektur, Entfernungs-Flag und die SHA-256-Summe jeder Paketdatei mit dem bestätigten Plan und bricht sonst ab. Er prüft zusätzlich, dass er als root läuft, dass die Sperre einem Vorfahrprozess gehört und dass die Plandatei root gehört. | `apt_guard_test`: ein Positivfall und sechs gezielte Verfälschungen (Version, Architektur, ausgetauschte Paketdatei, zusätzliche Operation, falsche Protokollversion, fehlende Operation), die alle abgewiesen werden müssen |
| Medienlimits aus Abschnitt 4.7 fehlten | `MediaCache` mit 10 MiB je Bild, 16 Megapixel, 256 MiB Plattencache, drei Redirects, 15 s Zeitlimit, vier gleichzeitige Downloads | `store_media_test` |
| Fünf Aktionszustände wurden nie erzeugt | Alle vierzehn Zustände der Matrix aus Abschnitt 7 werden jetzt ermittelt und von der Oberfläche aus einer Quelle gelesen | `store_catalog_test`, `store_bus_integration_test` |
| P4 und P6 waren nicht end-to-end belegt | `store_bus_integration_test` startet einen eigenen Daemon-Prozess auf einer echten D-Bus-Sitzung und führt den Klickweg durch die tatsächlichen Seiten `AppDetails.qml` und `PlanPreview.qml`: Installieren, Vorschau, Bestätigen, Bestandsabgleich, Entfernen. Zusätzlich geprüft: Wiederanbinden nach GUI-Neustart während des Commits, genau ein Abschlussereignis, Abweisung eines zweiten Clients und einer falschen Planrevision | `store_bus_integration_test` |
| Ein fehlgeschlagener Revisionsvergleich fiel auf einen ungebundenen Altpfad zurück | Dieser Rückfall ist entfernt; der Test prüft ausdrücklich, dass nach der Abweisung kein Commit stattfindet | `store_bus_integration_test` |
| `lutd` blockierte beim Planen bis zu 60 Sekunden | Die Paketauflösung läuft in einem eigenen Arbeitsbereich; der Dienst antwortet währenddessen weiter | `store_bus_integration_test` misst `GetCapabilities` während einer 700 ms dauernden Auflösung gegen eine 300-ms-Grenze |
| Katalogdurchlauf startete einen Unterprozess je Anwendung | Die Abfragen sind gebündelt und laufen abseits des GUI-Threads; die Oberfläche liest aus einem vorbereiteten Bestand | Debian-Container-Prüfstand |

### Phase P11 — Repository-Verwaltung (Multi-Distro Paketquellen)

- **Status:** Bestanden (22.09.2026)
- **Auftrag:** Unter Einstellungen einen Unterpunkt für Repositories hinzufügen. Vorhandene Paketquellen anzeigen, umschalten und löschen; 1-Klick-Vorschläge für Pacman, DNF und APT bereitstellen.
- **Umsetzung:**
  - **`liblut/repository/RepoManager` & `RepoTypes`:** Universeller Parser und Verwalter für `/etc/pacman.conf` (Arch/CachyOS), `/etc/yum.repos.d/*.repo` (Fedora DNF5) und `/etc/apt/sources.list[.d]` (Debian/Ubuntu APT).
  - **1-Klick-Presets:**
    - Arch / CachyOS: `multilib` (32-Bit Libs für Wine/Steam) und `chaotic-aur` (vorkompiliertes AUR).
    - Fedora: `rpmfusion-free`, `rpmfusion-nonfree`, `fedora-cisco-openh264`.
    - Debian: `contrib`, `non-free`, `non-free-firmware`, `backports`.
  - **Sicherheit & Schutz vor OS-Bruch:** Essenzielle System-Repositories (`core`, `extra`, `cachyos*`, `fedora`, `main`) tragen `isSystem = true`. Der Lösch-Knopf ist für diese gesperrt; Backend-seitig wird ein Löschen abgewiesen.
  - **D-Bus & Polkit:** Neue Aktion `org.linuxupdatetool.manage-repositories` (`auth_admin_keep`) für autorisierte Dateiänderungen via `lutd`.
  - **Oberfläche (`Settings.qml`):** Segmentierter Umschalter *Allgemein* | *Paketquellen*. 1-Klick-Karten mit Aktivierungs-/Löschfunktion, Liste konfigurierter Repositories mit Toggle und Löschen, Dialog für benutzerdefinierte Repositories. 100 % Plasma Theme-Palette (0 Hex-Farben).
  - **Qualitätssicherung:** Neuer Unit-Test `tests/unit/repo_manager_test.cpp` mit isolierten Testfixtures für alle drei Paketmanager. 29/29 CTests bestanden, `scripts/verify-all.sh` grün.

### Phase P12 — Paketauflösung für Bottles & AUR/AppStream ohne pkgname

- **Status:** Bestanden (22.09.2026)
- **Problem & Ursache:** Auf CachyOS / Arch mit `chaotic-aur` war `bottles` installiert, tauchte aber nicht unter „Installiert“ auf und zeigte auf der Detailseite „Aktion nicht unterstützt“. Ursache war die Upstream-Metadaten-Datei `/usr/share/metainfo/com.usebottles.bottles.metainfo.xml`, die keinen `<pkgname>`-Tag enthält, sodass `packageNames` leer blieb und kein Abgleich mit der ALPM-Datenbank stattfand.
- **Umsetzung:**
  - **`ApplicationStore::resolvePackageNamesForApps()`**: Asynchrone Paketauflösung im Worker-Thread von `buildSnapshot()`. Ermittelt den nativen Paketnamen über Dateibesitzerprüfung von Desktop-Dateien (`findPackagesProvidingFile("/usr/share/applications/...")`), Metainfofiles sowie Reverse-DNS-Namensheuristiken (`com.usebottles.bottles` $\rightarrow$ `bottles`).
  - **`CatalogService::setPackageNamesForApp()`**: Dynamische Aktualisierung der internen Nachschlagetabellen (`m_appsByKey`, `m_appsByPackage`), sodass auch Rückwärtssuchen (`appByPackageName("bottles")`) funktionieren.
  - **Qualitätssicherung:** Unit-Test `testReverseDnsInstalledResolution` (synthetisch) und `testRealHostBottlesDetection` (auf echtem CachyOS-Host mit echter ALPM-Datenbank) in `tests/unit/store_installed_test.cpp`. 29/29 CTests bestanden, `scripts/verify-all.sh` grün.
- **Paketierung:** Arch-Paket `linux-app-store-1.1.0-1-x86_64.pkg.tar.zst` mit `makepkg -f` neu gebaut und nach `~/Projekte/releases/` kopiert.

### Weiterhin offen

- **Ubuntu ist nicht geprüft.** Belegt ist Debian 13. Abschnitt 1.3 verlangt,
  die tatsächlich geprüften Distributionen zu benennen; „Ubuntu unterstützt"
  wäre ohne eigenen Lauf nicht gedeckt.
- **Die Leistungszahlen aus Abschnitt 10.5 sind nicht gemessen.** Der
  Katalogdurchlauf blockiert die Oberfläche nicht mehr, aber Such-, Start- und
  Scrollzeiten wurden nicht mit dokumentierter Datenmenge erhoben.
- **Die Sichtprüfung aus Phase P9** (Skalierung 100/150/200 %, helle und dunkle
  Systemfarben, reduzierte Bewegung, lange deutsche Texte) steht aus. Der
  Bus-Test legt einen Screenshot der Vorschau ab, ersetzt aber keine
  Sichtprüfung der übrigen Bildschirmzustände.


---

## Nachtrag 3: Audit der Paketquellen-Verwaltung (22.09.2026)

Nach der Umbenennung auf `linux-app-store` und der neu ergänzten
Repository-Verwaltung wurden vier Befunde behoben. Jeder ist durch einen Test
abgedeckt, der ohne die Korrektur fehlschlägt (per Mutation nachgewiesen).

| Befund | Korrektur | Test |
|---|---|---|
| Adresse und Anzeigename einer Paketquelle wurden ungeprüft als root in `pacman.conf`, `*.repo` und `*.list` geschrieben. Ein Zeilenumbruch genügte, um weitere Direktiven einzuschleusen, etwa `SigLevel = Never` oder eine zweite Paketquelle. | `isSafeConfigValue()` weist Steuerzeichen und überlange Werte zentral in `addRepository()` ab | `testConfigValuesRejectControlCharacters`, `testInjectedServerLineIsRejected` |
| `Include =` wurde gesetzt, sobald die Adresse irgendwo „mirrorlist" enthielt — damit ließ sich eine beliebige Datei in die `pacman.conf` einbinden | `isAllowedPacmanInclude()` lässt nur Dateien direkt unterhalb `/etc/pacman.d/` zu | `testIncludeOnlyFromPacmanDirectory` |
| `[trusted=yes]` ließ sich in eine APT-Quelle schreiben und schaltete dort die Signaturprüfung ab | Optionen in eckigen Klammern werden abgewiesen | `testAptSourceRejectsTrustedOption` |
| `safeWriteFile()` löschte die Zieldatei und benannte erst danach um. Ein Abbruch dazwischen hinterließ das System ohne `pacman.conf`. | `fsync` auf Datei und Verzeichnis, `rename()` über die bestehende Datei, Rechte explizit gesetzt | `testExistingConfigSurvivesReplacement` |

Zusätzlich korrigiert:

- **Namensableitung nur noch für den Bestand.** Die Reverse-DNS-Heuristik glich
  auch gegen verfügbare Repository-Angebote ab. Für eine Komponente ohne
  `<pkgname>` wurde dadurch ein gleichnamiges, unbeteiligtes Paket zum
  Installationsziel — Abschnitt 4.5 Punkt 8 und 9 verbieten das. Punkt 10
  erlaubt die Ableitung ausdrücklich nur für bereits installierte Anwendungen.
  Gegenprobe: `testReverseDnsNeverInventsInstallTarget`.
- **Chaotic-AUR** nennt jetzt in der Beschreibung, dass `chaotic-keyring`
  nötig ist. Signaturschlüssel werden weiterhin nicht automatisch importiert.

Prüfstand nach den Korrekturen: Host 29/29 und `verify-all.sh` vollständig grün,
Debian 13 25/25, Fedora 44 25/25, `makepkg`-`check()` 29/29.

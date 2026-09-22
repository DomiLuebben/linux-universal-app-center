# Linux Update Tool — Store-Architektur

Dieses Dokument beschreibt die Architektur, Datenflüsse, Typen und Prozessgrenzen der Store-Erweiterung für das `linux-update-tool`.

---

## 1. Übersicht und Datenfluss

```text
Distributions-AppStream ──> CatalogService ───────┐
                                                 ├─> ApplicationStore ─> Modelle (Apps, Filter, Details) ─> QML
Native Paketdaten ───────> PackageCatalog ───────┘           ▲
                                                             │ Neuer tatsächlicher Bestand
QML-Aktion ──> StoreController ──> DaemonClient ──> lutd ──> natives Backend / Worker
                                                      │
                                                      └─> Plan / Ereignisse / Ergebnis
```

### Prozessgrenzen & Rechtetrennung (ADR-2)
1. **Unprivilegierte GUI (`linux-update-tool`):**
   - Läuft unter Benutzerrechten.
   - Lädt und parst AppStream-Metadaten via `AppStreamQt` (nur OS-Metainfo/Distro-Kataloge, kein Flatpak-Flag).
   - Verwaltet Bild- und Screenshot-Cache unter `QStandardPaths::CacheLocation`.
   - Führt Suchen asynchron (Off-Thread) aus; blockiert niemals die Qt-Ereignisschleife.
   - Startet installierte Apps über KDE Frameworks (`KService` / `KIO::ApplicationLauncherJob`) ausschließlich als Benutzer.
2. **Privilegierter Systemdienst (`lutd`):**
   - Läuft als Root-Dienst auf dem System-D-Bus (`org.linuxupdatetool.Daemon1`).
   - Schützt Operationen über Polkit (`org.linuxupdatetool.policy`).
   - Erzwingt Planbindung (`TransactionPlan` mit SHA-256 Fingerprint) vor schreibenden Operationen.
   - Kontrolliert Transaktions-Lebenszyklus und Inhibitor-Sperren gegen Systemabschaltung während Commits.
3. **Isolierte Worker (`lut-alpm-worker` etc.):**
   - Führen privilegierte Paketänderungen unter nativer Datenbanksperre (`/var/lib/pacman/db.lck`) aus.
   - Kommunizieren über strukturierte, gefilterte JSON-Ereignisse über stdin/stdout mit dem Daemon.

---

## 2. Kern-Datenmodelle (`liblut/transaction/TransactionTypes.h` & `liblut/catalog/PackageCatalog.h`)

| Typ | Bedeutung & Felder |
|---|---|
| `AppRecord` | Anwendungsmetadaten aus AppStream: `appKey`, `componentId`, `name`, `summary`, `description`, `developer`, `license`, `urlHomepage`, `urlBugtracker`, `iconSource`, `screenshots`, `categories`, `keywords`, `launchableDesktopIds`, `origin`, `defaultPackageName`. |
| `PackageRef` | Eindeutige native Paketidentität: `backend`, `repoId`, `name`, `arch`, `version`. Getrennte Felder, kein Shell-String. |
| `PackageOffer` | Paketangebot: `packages` (Liste), `priority`, `isCandidate`, `downloadSize`, `installedSize`, `available`, `unavailabilityReason`. |
| `InstalledState` | Tatsächlicher Installationsstatus: `installedPackages`, `isFullyInstalled`, `isPartiallyInstalled`, `origin`, `launchableDesktopIds`, `inventoryRevision`. |
| `AppActionState` | Abgeleiteter Aktionszustand: `Available`, `Installed`, `InstalledNoLaunch`, `UpdateAvailable`, `PartiallyInstalled`, `MissingSource`, `Unavailable`, `ActionUnsupported`, `PreparingPlan`, `AwaitingConfirmation`, `Progressing`, `OtherTransactionRunning`, `Reconciling`, `ErrorOrCancelled`. |
| `TransactionIntent` | Absicht einer Transaktion: `Type` (`UpgradeAll`, `Install`, `Remove`, `CustomCommand`), `targets` (Liste validierter `PackageRef`), `appKey` (nur UI), `options`. |
| `TransactionPlan` | Aufgelöster Paketplan: `id`, `planRevision` (Fingerprint), `backendRevision`, `ops` (`PackageOp`-Liste), `downloadBytes`, `installedSizeDelta`, `warnings`, `hasProtectedPackageConflict`, `affectedApps`. |
| `TransactionSnapshot` | Momentaufnahme für Reattach: `transactionPath`, `intent`, `phase`, `plan`, `canCancel`, `sequenceNumber`, `lastResult`, `statusMessage`, `progressPercent`, `timestamp`. |

---

## 3. Planbindung und Schutz vor Race Conditions

Zwischen der Anzeige einer Vorschau und der Ausführung können sich Pakete, Spiegelserver oder lokale Daten ändern.

1. **Fingerprint-Berechnung:**
   `TransactionPlan::calculateFingerprint()` berechnet einen deterministischen SHA-256-Hash über:
   - Backend-Revision
   - Sortierte Liste aller Paketoperationen (`kind`, `name`, `version`, `newVersion`, `arch`, `repo`)
   - Geplante Downloadgrößen und Speicherplatz-Deltas
   - Systemschutz-Flags
2. **Prüfung unter Schreibsperre:**
   Beim Aufruf von `CommitPlan(path, planRevision)` prüft `lutd` die übergebene Revision gegen den aktiven Plan. Vor der tatsächlichen Paketänderung wird unter der nativen Datenbanksperre verifiziert, dass die Resolver-Ergebnisse identisch sind.
3. **Abbruch bei Abweichung:**
   Bei materieller Drift (neue Version, geänderte Abhängigkeiten) stoppt die Ausführung, und der geänderte Plan muss vom Benutzer neu bestätigt werden.

---

## 4. Trennung von Transaktionsplan und Updatebestand (TX-27)

- `UpdatesModel`: Hält ausschließlich die Liste der verfügbaren System- und AUR-Updates.
- `TransactionPlanModel`: Hält die konkreten Operationen der aktuell geplanten Transaktion (z. B. Installation oder Deinstallation einer App).
- Store-Installationen überschreiben niemals die Update-Liste in `UpdatesModel`.
- Nach erfolgreicher Transaktion werden sowohl Bibliothek (`InstalledModel`) als auch der Updatebestand konsistent abgeglichen.

---

## 5. D-Bus-Schnittstelle (`org.linuxupdatetool.Daemon1`)

Additive Methoden in Version 2:
```text
PlanPackageTransaction(s action, av targets, a{sv} options) -> o transactionPath
GetTransactionSnapshot(o transactionPath) -> s snapshotJson
CommitPlan(o transactionPath, s planRevision) -> void
DiscardPlan(o transactionPath) -> void
AttachTransaction(o transactionPath, x lastSeenSequence) -> s snapshotJson
```
Bestehende Methoden (`PlanUpgrade`, `PlanInstall`, `PlanRemove`, `PlanDnf5`, `Commit`, `Cancel`, `AnswerQuestion`, `GetCapabilities`) bleiben für Abwärtskompatibilität vollständig erhalten.

# Linux Update Tool — Backend-Notizen für den Store

Dieses Dokument dokumentiert die Besonderheiten, Befunde und Integrationsdetails der nativen Paket-Backends für die Store-Erweiterung.

---

## 1. Arch Linux / CachyOS (ALPM-Backend)

### 1.1 Ausgangslage & Komponenten
- Hostsystem: CachyOS (`ID_LIKE=arch`)
- Paketmanager: `pacman 7.1.0`, `libalpm 15.0.0`
- AppStream: `appstream 1.2.0-1.1`, `appstream-qt 1.2.0-1.1` (CMake: `AppStreamQt`)
- Distro-Katalogpaket: `archlinux-appstream-data` (in `extra` bereitgestellt)
- Worker: `/usr/libexec/linux-update-tool/lut-alpm-worker` mit nativer `libalpm`-Integration.

### 1.2 Keine Teilupdates (Partial Upgrade Policy)
- **ArchWiki-Grundsatz:** Teilweise Systemaktualisierungen (`pacman -Sy paket` ohne `-u`) sind auf Arch/CachyOS nicht unterstützt und führen zu Bibliotheksinkompatibilitäten.
- **Store-Strategie:** Wenn beim Installieren einer Anwendung unvollständige oder veraltete lokale Paketdatenbanken vorliegen bzw. Systemaktualisierungen anstehen, wird die Installation mit dem erforderlichen Systemupgrade gekoppelt. Die Vorschau zeigt ehrlich „System aktualisieren und App installieren“ an.
- **Verbot:** Kein heimliches `pacman -Sy` im Hintergrund beim Öffnen des Stores oder bei der Suche.

### 1.3 Repository-Prioritäten & CachyOS
- CachyOS konfiguriert eigene Repositories (`cachyos`, `cachyos-v3`, etc.) vor den offiziellen Arch-Repositories (`core`, `extra`).
- Bei Namensgleichheiten (z. B. angepasste Browser- oder Kernelpakete) muss der native Resolver die Reihenfolge aus `/etc/pacman.conf` exakt respektieren.
- Kein hardcodierter Bevorzugungs-Filter für „extra“.

### 1.4 Native Fragen & Rückkanal
- Paketkonflikte, virtuelle Provider (z. B. `ffmpeg` vs `ffmpeg-full`) und GPG-Schlüsselimporte erfordern einen Rückkanal (`Question` / `AnswerQuestion`).
- Fragen müssen mit der eindeutigen Transaktions- und Frage-ID synchronisiert werden.

---

## 2. Fedora / DNF5-Backend

### 2.1 Ausgangslage & Komponenten
- DNF5 CLI (`/usr/bin/dnf5`) mit nativer Transaktionsspeicherung (`--store`) und Replay (`replay`).
- Epoch- und Multiarch-Semantik (`name:arch=epoch:version-release`).

### 2.2 Transaktionsspeicherung & Replay
- `planCommand` speichert Transaktionen in einem daemon-kontrollierten Verzeichnis.
- Replay führt den vorbereiteten Plan exakt aus.
- Signaturprüfungen dürfen für lokal zwischengespeicherte RPMs nicht deaktiviert werden.
- Bei geänderter Systembasis zwischen Plan und Replay bricht DNF5 ab und erfordert Neuauflösung.

---

## 3. Debian / Ubuntu / APT-Backend

### 3.1 Ausgangslage & Komponenten
- Metadaten über AppStream / DEP-11 (`/var/lib/app-info/yaml/`, `/var/lib/swcatalog/`).
- APT-Policy (`apt-cache policy`), Pins und Hold-Zustände.
- Paketstatus über `dpkg`: Status `config-files` nach `remove` zählt ausdrücklich **nicht** als installierte App.

### 3.2 Planbindung & Schutz
- Simulation über `apt-get -s` und Statusauswertung über `StatusFdParser`.
- Standardmäßig `remove` (kein `purge`, kein ungewolltes `autoremove`).
- Schutz essentieller und geschützter Pakete (`Essential: yes`, `Protected: yes`).
- Conffile-Fragen konservativ behandeln; kein unbedientes Blockieren auf `stdin`.

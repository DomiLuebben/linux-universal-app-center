#!/usr/bin/env bash
set -eo pipefail

echo "=== [F3 Container-Abnahme] Starte Flatpak-Prüflauf in Wegwerf-Container ==="

docker run --rm --privileged archlinux:latest bash -s << 'EOF'
set -eo pipefail

echo "--- 1. Flatpak im Wegwerf-Container einrichten ---"
pacman -Sy --noconfirm flatpak > /dev/null 2>&1
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo

echo "--- 2. Ausgangszustand prüfen ---"
INITIAL_COUNT=$(flatpak list --app | wc -l)
echo "Installierte Flatpak-Anwendungen anfangs: ${INITIAL_COUNT}"

# Erfassung der installierten Arch-Systempakete vor der Flatpak-Aktion
PACMAN_BEFORE=$(pacman -Q | sha256sum)

echo "--- 3. Echte Flatpak-Anwendung installieren (systemweit) ---"
flatpak install --system -y --noninteractive flathub org.gnome.Calculator

echo "--- 4. Bestand und exportierte Dateien prüfen ---"
flatpak list --app --columns=application,origin,installation,ref,version
if ! flatpak list --app | grep -q "org.gnome.Calculator"; then
    echo "FEHLER: org.gnome.Calculator nicht in flatpak list!"
    exit 1
fi

DESKTOP_FILE="/var/lib/flatpak/exports/share/applications/org.gnome.Calculator.desktop"
if [ ! -f "${DESKTOP_FILE}" ]; then
    echo "FEHLER: Exportierte Desktop-Datei ${DESKTOP_FILE} fehlt!"
    exit 1
fi
echo "Gefunden: ${DESKTOP_FILE}"

echo "--- 5. Startfähigkeit (Exec-Prüfung) prüfen ---"
EXEC_LINE=$(grep "^Exec=" "${DESKTOP_FILE}")
echo "Exec-Zeile: ${EXEC_LINE}"
if ! echo "${EXEC_LINE}" | grep -q "flatpak run"; then
    echo "FEHLER: Desktop-Datei enthält kein flatpak run!"
    exit 1
fi

echo "--- 6. Deinstallation (systemweit) prüfen (ohne --unused, Abschnitt 6.3) ---"
flatpak uninstall --system -y --noninteractive org.gnome.Calculator

if flatpak list --app | grep -q "org.gnome.Calculator"; then
    echo "FEHLER: org.gnome.Calculator nach Deinstallation noch vorhanden!"
    exit 1
fi
echo "Erfolgreich deinstalliert."

echo "--- 7. Nachweis: Keine Systemaktualisierung ausgelöst (Abschnitt 4.2 & F3) ---"
PACMAN_AFTER=$(pacman -Q | sha256sum)
if [ "${PACMAN_BEFORE}" != "${PACMAN_AFTER}" ]; then
    echo "FEHLER: pacman -Q hat sich geändert! Ein Systemupdate wurde fälschlich ausgelöst!"
    exit 1
fi
echo "Nachweis erbracht: Systempaket-Bestand unverändert (${PACMAN_BEFORE%% *} == ${PACMAN_AFTER%% *})."

echo "=== Flatpak Container-Abnahme erfolgreich bestanden ==="
EOF

echo "=== [F3 Container-Abnahme] Erfolgreich abgeschlossen ==="

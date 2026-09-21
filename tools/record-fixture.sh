#!/usr/bin/env bash
set -eo pipefail

if [ $# -lt 1 ]; then
    echo "Verwendung: $0 <ausgabe-fixture.jsonl>"
    exit 1
fi

OUTFILE="$1"
echo "Zeichne Transaktions-Events von org.linuxupdatetool.Daemon1 auf: ${OUTFILE}"
echo "Beende die Aufnahme mit Strg+C sobald die Transaktion abgeschlossen ist..."

# Überwache D-Bus Signale 'Event' auf org.linuxupdatetool.Daemon1
busctl --system monitor org.linuxupdatetool.Daemon1 | grep --line-buffered "Event" | while read -r line; do
    # Extrahiere JSON-String aus D-Bus Signal
    json=$(echo "$line" | sed -n 's/.*s "\(.*\)".*/\1/p' | sed 's/\\"/"/g')
    if [ -n "$json" ]; then
        echo "{\"delay_ms\": 25, \"event\": ${json}}" >> "${OUTFILE}"
    fi
done

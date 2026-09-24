#!/usr/bin/env bash
# Prüft die AppStream-Kennungen aus data/store/curated.json gegen einen echten
# Distributionskatalog in einem Wegwerf-Container. Ohne diese Prüfung fällt nicht
# auf, dass eine Sammlung auf dem Zielsystem einfach leer bleibt: nicht
# auflösbare Einträge werden in der Oberfläche stillschweigend ausgeblendet.
#
# Der Katalog verwendet je nach Distribution "org.kde.kate" oder
# "org.kde.kate.desktop". Beide Schreibweisen gelten hier als Treffer, weil
# CatalogService::normalizedAppKey() sie zusammenführt.
set -euo pipefail

LUT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CURATED="${LUT_ROOT}/data/store/curated.json"
IMAGE="${LUT_CURATED_IMAGE:-archlinux:latest}"

if [ ! -f "$CURATED" ]; then
    echo "FEHLER: $CURATED nicht gefunden" >&2
    exit 1
fi

IDS="$(python3 -c '
import json, sys
data = json.load(open(sys.argv[1]))
seen = []
for collection in data["collections"]:
    for app_id in collection["appIds"]:
        if app_id not in seen:
            seen.append(app_id)
print("\n".join(seen))
' "$CURATED")"

if [ -z "$IDS" ]; then
    echo "FEHLER: keine App-Kennungen in $CURATED" >&2
    exit 1
fi

echo "=== Prüfe $(echo "$IDS" | wc -l) kuratierte Kennungen gegen $IMAGE ==="

printf '%s\n' "$IDS" | docker run --rm --network=host -i "$IMAGE" sh -c '
    pacman -Sy --noconfirm archlinux-appstream-data appstream >/dev/null 2>&1 || {
        echo "FEHLER: Katalogpaket konnte nicht installiert werden" >&2
        exit 2
    }
    appstreamcli refresh --force >/dev/null 2>&1

    missing=0
    total=0
    while read -r id; do
        [ -n "$id" ] || continue
        total=$((total + 1))
        if appstreamcli get "$id" >/dev/null 2>&1; then
            echo "  OK       $id"
        elif appstreamcli get "${id}.desktop" >/dev/null 2>&1; then
            echo "  OK       $id (Katalog führt ${id}.desktop)"
        else
            echo "  NICHT AUFLÖSBAR  $id"
            missing=$((missing + 1))
        fi
    done

    echo "--- $((total - missing)) von $total Kennungen auflösbar ---"
    if [ "$missing" -gt 0 ]; then
        echo "FEHLER: $missing Kennung(en) ohne Treffer. Diese Einträge erscheinen auf dem Zielsystem nie." >&2
        exit 1
    fi
'

echo "=== Alle kuratierten Kennungen sind im Distributionskatalog auflösbar ==="

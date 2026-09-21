#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ $# -lt 1 ]; then
    echo "Verwendung: $0 <fixture.jsonl> [--speed <faktor>] [--quiet]"
    echo "Beispiel:   $0 tests/fixtures/kernel-update.jsonl --speed 5.0"
    exit 1
fi

FIXTURE="$1"
shift

# Falls relativer Pfad übergeben wurde
if [ ! -f "${FIXTURE}" ] && [ -f "${ROOT_DIR}/${FIXTURE}" ]; then
    FIXTURE="${ROOT_DIR}/${FIXTURE}"
fi

# Baue Runner falls nicht vorhanden
if [ ! -x "${ROOT_DIR}/build/tools/lut-replay" ]; then
    echo "Baue lut-replay..."
    cmake --build "${ROOT_DIR}/build" --target lut-replay -j"$(nproc)"
fi

exec "${ROOT_DIR}/build/tools/lut-replay" "${FIXTURE}" "$@"

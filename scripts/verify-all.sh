#!/usr/bin/env bash
set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "=== [1/5] Building linux-update-tool ==="
cmake -B "${ROOT_DIR}/build" -S "${ROOT_DIR}"
cmake --build "${ROOT_DIR}/build" -j"$(nproc)"

echo "=== [2/5] Running automated tests ==="
ctest --test-dir "${ROOT_DIR}/build" --output-on-failure

echo "=== [3/5] Testing QML engine runtime smoke load (alle Seiten, Warnungen = Fehler) ==="
QT_QPA_PLATFORM=offscreen "${ROOT_DIR}/build/linux-update-tool/linux-update-tool" --check-qml --replay "${ROOT_DIR}/tests/fixtures/small-update.jsonl"

echo "=== [4/5] Validating desktop entry and AppStream metadata ==="
desktop-file-validate "${ROOT_DIR}/data/org.linuxupdatetool.desktop"
appstreamcli validate --no-net "${ROOT_DIR}/data/org.linuxupdatetool.metainfo.xml"

echo "=== [5/5] Checking QML color policy (no hardcoded hex colors except documented Theme) ==="
# Check that no hex color constants appear in QML components/pages
HARDCODED_COLORS=$(grep -rnE '#[0-9a-fA-F]{3,8}' "${ROOT_DIR}/linux-update-tool/qml/" 2>/dev/null | grep -v "Vorläufig für M0" || true)
if [ -n "${HARDCODED_COLORS}" ]; then
    echo "ERROR: Hardcoded hex colors found in QML files:"
    echo "${HARDCODED_COLORS}"
    exit 1
fi

echo "=== ALL CHECKS PASSED ==="

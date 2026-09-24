#!/usr/bin/env bash
# Baut liblut und tests/integration/pacstall_integration in einem Wegwerf-Container
# mit echtem Pacstall (Version 6.4.2) und offiziellem PPR-Repository.
#
# Abgrenzung:
#   tests/unit/pacstall_backend_test.cpp  -> Backend gegen gemockte CLI und Downloader
#   dieses Skript                         -> Backend gegen echtes Pacstall und PPR-Repo
#
# Paketänderungen bleiben strikt im Container.
set -euo pipefail

LUT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LUT_IMAGE="lut-pacstall-tests"

docker build --network=host -t "$LUT_IMAGE" -f "$LUT_ROOT/tests/integration/containers/pacstall/Containerfile" \
    "$LUT_ROOT/tests/integration/containers/pacstall"

# --privileged: Pacstall nutzt bubblewrap (bwrap) zur Kapselung.
LUT_CONTAINER="$(docker create --network=host --privileged \
    -e LUT_INTEGRATION_ALLOW_MUTATIONS=1 -e QT_QPA_PLATFORM=offscreen -e LANG=C.UTF-8 -e TERM=xterm \
    "$LUT_IMAGE" sh -c '
    set -e
    OLD=https://raw.githubusercontent.com/pacstall/pacstall-programs/967e274f4db13701c10f18feb6827c2fe4af0195/packages/tree-sitter-cli-bin/tree-sitter-cli-bin.pacscript
    cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release
    cmake --build /app/build -j4 --target pacstall_integration
    echo "=== 0. Paketlisten wie auf einem normalen System"
    apt-get update -qq
    echo "=== 1. PPR über den Store-Code einrichten (darf die übrigen Listen nicht löschen)"
    /app/build/tests/pacstall_integration setup
    echo "=== 2. pacstall aus der PPR installieren"
    apt-get install -y --no-install-recommends pacstall
    pacstall -V
    curl -fsSL "$OLD" -o /tmp/tree-sitter-cli-bin.pacscript
    echo "=== 3. Debian-Grenze: ohne spdx-licenses lehnt pacstall die Lizenzangabe ab"
    if apt-cache policy spdx-licenses | grep -q "Candidate: [0-9]"; then echo "spdx-licenses ist in dieser Debian-Fassung vorhanden"; exit 1; fi
    if NO_COLOR=1 DISABLE_PROMPTS=yes pacstall -P -I /tmp/tree-sitter-cli-bin.pacscript > /tmp/nolicense.log 2>&1; then
        echo "Unerwartet: Installation ohne spdx-licenses gelang"; exit 1
    fi
    grep "is not a valid license" /tmp/nolicense.log
    echo "=== 4. spdx-licenses so beschaffen wie Pacstalls eigener Updater (misc/scripts/update.sh)"
    curl -fsSL -o /tmp/spdx-licenses.deb https://ftp.debian.org/debian/pool/main/s/spdx-licenses/spdx-licenses_3.27.0+ds-1_all.deb
    apt-get install -y /tmp/spdx-licenses.deb
    echo "=== 5. Ältere echte Fassung installieren (pacstall-programs 967e274f)"
    NO_COLOR=1 DISABLE_PROMPTS=yes pacstall -P -I /tmp/tree-sitter-cli-bin.pacscript
    rm -f /tmp/tree-sitter-cli-bin.pacscript
    echo "=== 6. Erkennung, Plan, Manipulationsabwehr, Aktualisierung"
    /app/build/tests/pacstall_integration
')"
trap 'docker rm -f "$LUT_CONTAINER" >/dev/null' EXIT
tar -C "$LUT_ROOT" --exclude=.git --exclude=work --exclude=build --exclude=pkg --exclude=src \
    --exclude='*.pkg.tar.zst' -cf - . | docker cp - "$LUT_CONTAINER:/app"
docker start -a "$LUT_CONTAINER"
LUT_EXIT="$(docker inspect --format '{{.State.ExitCode}}' "$LUT_CONTAINER")"
exit "$LUT_EXIT"

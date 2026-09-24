#!/usr/bin/env bash
# Baut das Projekt in einem Wegwerf-Container mit echtem Flatpak und fährt
# tests/integration/flatpak_integration.cpp dagegen.
#
# Abgrenzung zu den beiden anderen Flatpak-Prüfungen:
#   tests/unit/store_flatpak_test.cpp     -> unser Backend gegen einen gefälschten CLI-Aufruf
#   scripts/verify-flatpak-container.sh   -> echte flatpak-CLI, aber ohne unser Backend
#   dieses Skript                         -> unser Backend gegen die echte flatpak-CLI
#
# Nur dieses Skript belegt, dass eine echte Flatpak-Installation tatsächlich von
# unserem Code ausgelöst wird. Paketänderungen bleiben im Container.
set -euo pipefail

LUT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LUT_IMAGE="lut-flatpak-tests"

docker build --network=host -t "$LUT_IMAGE" -f "$LUT_ROOT/tests/integration/containers/flatpak/Containerfile" \
    "$LUT_ROOT/tests/integration/containers/flatpak"

# --privileged: Flatpak braucht bubblewrap und eigene Mount-Namespaces.
LUT_CONTAINER="$(docker create --network=host --privileged \
    -e LUT_INTEGRATION_ALLOW_MUTATIONS=1 -e QT_QPA_PLATFORM=offscreen -e LANG=C.UTF-8 \
    "$LUT_IMAGE" sh -c '
    flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo &&
    cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release &&
    cmake --build /app/build -j4 --target flatpak_integration &&
    /app/build/tests/flatpak_integration
')"
trap 'docker rm -f "$LUT_CONTAINER" >/dev/null' EXIT
tar -C "$LUT_ROOT" --exclude=.git --exclude=work --exclude=build --exclude=pkg --exclude=src \
    --exclude='*.pkg.tar.zst' -cf - . | docker cp - "$LUT_CONTAINER:/app"
docker start -a "$LUT_CONTAINER"
LUT_EXIT="$(docker inspect --format '{{.State.ExitCode}}' "$LUT_CONTAINER")"
exit "$LUT_EXIT"

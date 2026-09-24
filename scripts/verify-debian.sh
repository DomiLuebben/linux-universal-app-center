#!/usr/bin/env bash
set -euo pipefail
LUT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LUT_IMAGE="lut-debian-tests"

docker build --network=host -t "$LUT_IMAGE" -f "$LUT_ROOT/tests/integration/containers/debian/Containerfile" "$LUT_ROOT/tests/integration/containers/debian"

# Copy sources into a disposable container; package mutations never touch the host.
LUT_CONTAINER="$(docker create --network=host -e LUT_INTEGRATION_ALLOW_MUTATIONS=1 -e QT_QPA_PLATFORM=offscreen -e LANG=C.UTF-8 "$LUT_IMAGE" sh -c '
    apt-get update &&
    cmake -S /app -B /app/build && cmake --build /app/build -j4 &&
    ctest --test-dir /app/build --output-on-failure &&
    QT_QPA_PLATFORM=offscreen /app/build/linux-app-store/linux-universal-app-center --check-qml --replay /app/tests/fixtures/small-update.jsonl &&
    /app/build/tests/apt_integration
')"
trap 'docker rm -f "$LUT_CONTAINER" >/dev/null' EXIT
tar -C "$LUT_ROOT" --exclude=.git --exclude=work --exclude=build --exclude=pkg --exclude=src --exclude='*.pkg.tar.zst' -cf - . | docker cp - "$LUT_CONTAINER:/app"
docker start -a "$LUT_CONTAINER"
LUT_EXIT="$(docker inspect --format '{{.State.ExitCode}}' "$LUT_CONTAINER")"
exit "$LUT_EXIT"

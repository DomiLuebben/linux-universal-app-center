#!/usr/bin/env bash
set -euo pipefail
LUT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LUT_IMAGE="lut-fedora44-tests"
docker build --network=host -t "$LUT_IMAGE" -f "$LUT_ROOT/tests/integration/containers/fedora44/Containerfile" "$LUT_ROOT/tests/integration/containers/fedora44"
# Copy sources into a disposable container; package mutations never touch the host.
LUT_CONTAINER="$(docker create --network=host -e LUT_INTEGRATION_ALLOW_MUTATIONS=1 "$LUT_IMAGE" sh -c '
    cmake -S /app -B /app/build && cmake --build /app/build -j4 &&
    ctest --test-dir /app/build --output-on-failure &&
    /app/build/linux-update-tool/linux-update-tool --check-qml --replay /app/tests/fixtures/small-update.jsonl &&
    /app/build/tests/dnf5_integration
')"
trap 'docker rm -f "$LUT_CONTAINER" >/dev/null' EXIT
 tar -C "$LUT_ROOT" --exclude=.git --exclude=build --exclude=pkg --exclude=src --exclude='*.pkg.tar.zst' -cf - . | docker cp - "$LUT_CONTAINER:/app"
docker start -a "$LUT_CONTAINER"
LUT_EXIT="$(docker inspect --format '{{.State.ExitCode}}' "$LUT_CONTAINER")"
exit "$LUT_EXIT"

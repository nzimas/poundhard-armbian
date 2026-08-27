#!/usr/bin/env bash
# Run the conversion harness in a container. Needs --privileged for loop devices.
#   ./tests/run-convert-test.sh [bundle-dir]
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUNDLE="${1:-$HERE/bundle/armbian}"
[ -f "$BUNDLE/rootfs.tar.gz" ] || { echo "no bundle at $BUNDLE — run armbian/build-bundle.sh first" >&2; exit 1; }

docker run --rm --privileged --platform linux/arm64 \
    -v "$HERE/armbian/convert.sh:/work/convert.sh:ro" \
    -v "$HERE/tests/convert-harness.sh:/work/harness.sh:ro" \
    -v "$BUNDLE:/bundle:ro" \
    debian:trixie-slim bash -c '
        set -e
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq >/dev/null
        apt-get install -y -qq --no-install-recommends \
            parted dosfstools e2fsprogs util-linux python3 openssh-client procps \
            tar gzip mount >/dev/null
        exec bash /work/harness.sh
    '

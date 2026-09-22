#!/bin/bash
# (Re)build the PhModal UGen (the MODAL engine: crispinha's modal synthesiser) for the
# Move's aarch64 Linux, plus the offline checks as a static aarch64 binary that can run on
# the device to measure CPU on the real chip.
#
# modal-synth is vendored in-tree (supercollider/plugins/PhModal/vendor, unmodified), so
# only the SuperCollider headers are fetched, at the version the Move runs.
#
# Built in an arm64 Ubuntu 20.04 container with libstdc++/libgcc STATICALLY linked, like
# ByteBeat and PhSoftcut, so the .so needs only glibc and loads whatever the device's C++
# runtime. The image is named by ARCHITECTURE (arm64v8/...) and the container checks it:
# a local tag can point at a 32-bit image, and --platform does not override a local tag.
#
# Output: supercollider/plugins/PhModal/PhModal{,_supernova}.so, tests/modal/out/test_phmodal-aarch64
#
# FASTER, and how the shipped binaries were made: build ON a Move running Armbian, which is
# an aarch64 Linux with a compiler already on it (install.sh puts gcc there for phgain):
#   ./move/build-modal.sh --on-device <host>
# Compiles in a scratch dir under /data, never touches the running stack, and copies the
# .so pair back here. Built against the device's glibc, so it is for Armbian Moves.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/.." && pwd)"
if [ "${1:-}" = "--on-device" ]; then
    HOST="${2:?usage: build-modal.sh --on-device <host>}"
    SRC="$ROOT/supercollider/plugins/PhModal"; CACHE="$ROOT/.cache/sc-3.13-include"
    if [ ! -d "$CACHE/plugin_interface" ]; then
        echo "-> fetching SuperCollider 3.13.0 headers (once)"
        T="$(mktemp -d)"; git clone --branch Version-3.13.0 --depth 1 \
            https://github.com/supercollider/supercollider.git "$T/sc" >/dev/null 2>&1
        mkdir -p "$CACHE"; cp -R "$T/sc/include/plugin_interface" "$T/sc/include/common" "$CACHE/"; rm -rf "$T"
    fi
    B=/data/UserData/.build-modal
    # Stock and Armbian answer on the same USB address with different host keys, so a
    # remembered key is wrong half the time. Same handling as install.sh.
    SSHO="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
    echo "-> staging on $HOST"
    tar --no-xattrs -czf - -C "$ROOT/supercollider/plugins" PhModal/PhModal.cpp PhModal/phmodal_core.cpp \
        PhModal/phmodal_core.hpp PhModal/vendor -C "$ROOT/.cache" sc-3.13-include \
      | ssh $SSHO "root@$HOST" "rm -rf $B && mkdir -p $B && tar -C $B -xzf -"
    echo "-> compiling on $HOST (static libstdc++, runtime symbols hidden)"
    ssh $SSHO "root@$HOST" "set -e; cd $B
      F='-std=c++17 -O3 -DMODAL_NUM_TYPE=float -IPhModal/vendor/modal-synth/include -IPhModal -fPIC -shared \
         -Isc-3.13-include/plugin_interface -Isc-3.13-include/common -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL'
      g++ \$F PhModal/PhModal.cpp PhModal/phmodal_core.cpp -o PhModal.so
      g++ \$F -DSUPERNOVA=1 PhModal/PhModal.cpp PhModal/phmodal_core.cpp -o PhModal_supernova.so
      echo '   needs:' \$(ldd PhModal.so | awk '{print \$1}')"
    scp -q $SSHO "root@$HOST:$B/PhModal.so" "root@$HOST:$B/PhModal_supernova.so" "$SRC/"
    echo "-> $SRC/PhModal{,_supernova}.so"
    exit 0
fi
SRC="$ROOT/supercollider/plugins/PhModal"; TESTS="$ROOT/tests/modal"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
IMAGE="${PH_BUILD_IMAGE:-arm64v8/ubuntu:20.04}"

echo "-> fetching SuperCollider 3.13.0 headers"
git clone --branch Version-3.13.0 --depth 1 https://github.com/supercollider/supercollider.git "$WORK/sc" >/dev/null 2>&1

mkdir -p "$WORK/out" "$TESTS/out"
cat > "$WORK/build.sh" <<'BUILD'
set -e
[ "$(uname -m)" = aarch64 ] || { echo "container is $(uname -m), not aarch64" >&2; exit 1; }
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq && apt-get install -y -qq build-essential cmake >/dev/null 2>&1
cmake -S /src -B /tmp/b -DSC_PATH=/sc -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-O3 -static-libstdc++ -static-libgcc" \
  -DCMAKE_SHARED_LINKER_FLAGS="-static-libstdc++ -static-libgcc" >/dev/null
cmake --build /tmp/b -j"$(nproc)" 2>&1 | grep -E "error|warning: unused" | head -n 20 || true
cp /tmp/b/PhModal.so /tmp/b/PhModal_supernova.so /out/
g++ -std=c++17 -O3 -static -DMODAL_NUM_TYPE=float -I/src/vendor/modal-synth/include -I/src \
    /tests/test_phmodal.cpp /src/phmodal_core.cpp -o /out/test_phmodal-aarch64
echo "--- PhModal.so needs:"; ldd /out/PhModal.so | awk '{print "   " $1}'
file /out/PhModal.so | sed 's/^/   /'
BUILD
echo "-> building for aarch64 in $IMAGE (static libstdc++)"
docker run --rm -v "$SRC":/src:ro -v "$TESTS":/tests:ro -v "$WORK/sc":/sc:ro \
  -v "$WORK/out":/out -v "$WORK":/w "$IMAGE" bash /w/build.sh
cp "$WORK/out/PhModal.so" "$WORK/out/PhModal_supernova.so" "$SRC/"
cp "$WORK/out/test_phmodal-aarch64" "$TESTS/out/"
echo "-> $SRC/PhModal{,_supernova}.so ($(du -h "$SRC/PhModal.so" | cut -f1) each)"

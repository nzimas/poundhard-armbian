#!/usr/bin/env bash
# Build Armbian for the Move from source, on this machine, in Docker.
#
#   ./armbian/build-armbian.sh <move-host> [workdir]
#
# Nothing about the operating system is downloaded pre-built. This clones the
# Move port and Armbian's build framework, extracts the Move firmware from YOUR
# device over ssh, and compiles an image locally. That is deliberate: the image
# ends up containing Ableton's /opt/move, which is yours and is not ours to
# distribute -- building it on your machine from your device is what makes the
# whole thing legitimate as well as automatic.
#
# Follows the port's own BUILD.md rather than reimplementing it: the staging
# list and the compile invocation below are that document's, and the firmware
# extraction runs the port's own script.
#
# Requirements: Docker, ~30GB free disk, bash 5+, and ssh access to the Move.
# Expect an hour or more on the first run; the Armbian cache makes reruns fast.
set -euo pipefail

MOVE_HOST="${1:?usage: build-armbian.sh <move-host> [workdir]}"
WORK="${2:-$HOME/.cache/poundhard-armbian-build}"
PORT_REPO="${PH_PORT_REPO:-https://github.com/djhardrich/move-spi-armbian.git}"
ARMBIAN_REPO="${PH_ARMBIAN_REPO:-https://github.com/armbian/build.git}"
# Arch-explicit on purpose. A local "debian:trixie-slim" tag may well point at an
# armhf image -- docker reuses a local tag without consulting the registry, and
# --platform does NOT override that -- and the debs would come out 32-bit with
# nothing to say so until the Move failed to boot. The arm64v8 namespace cannot
# be anything else, and the containers assert their own architecture besides.
DEB_IMAGE="${PH_DEB_IMAGE:-arm64v8/debian:trixie-slim}"

b()   { printf '\n\033[1m== %s\033[0m\n' "$*"; }
info(){ printf '   %s\n' "$*"; }
die() { printf '\n\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

# ------------------------------------------------------------------ preflight
b "preflight"
command -v docker >/dev/null 2>&1 || die "Docker is required to build Armbian.
   The Armbian build framework runs inside a container, and extracting the Move
   firmware needs a Debian userland this machine does not have.
     macOS:  brew install --cask docker   (then start Docker Desktop)
     Linux:  https://docs.docker.com/engine/install/"
docker info >/dev/null 2>&1 || die "Docker is installed but not running — start it and retry."
info "docker $(docker version --format '{{.Server.Version}}' 2>/dev/null || echo present)"

# Armbian's build scripts use associative arrays and other bash 4/5 features.
# macOS ships bash 3.2 (2007) as /bin/bash, which fails in confusing ways.
if [ "${BASH_VERSINFO[0]:-0}" -lt 5 ]; then
    die "bash ${BASH_VERSION} is too old for the Armbian build framework (needs 5+).
     macOS: brew install bash, then re-run this with the new bash:
       \$(brew --prefix)/bin/bash ./armbian/build-armbian.sh $MOVE_HOST"
fi
info "bash ${BASH_VERSION}"

command -v git >/dev/null 2>&1 || die "git is required"
ssh -o BatchMode=yes -o ConnectTimeout=10 -o StrictHostKeyChecking=no \
    "root@${MOVE_HOST}" true 2>/dev/null \
  || die "cannot ssh to root@${MOVE_HOST}.
   The build takes the Move firmware off your own device, so it needs to reach it.
   Boot the Move (stock is fine) and run: ssh-copy-id root@${MOVE_HOST}"
info "move reachable at root@${MOVE_HOST}"

FREE_GB=$(df -Pk "$(dirname "$WORK")" 2>/dev/null | awk 'NR==2{print int($4/1048576)}')
[ "${FREE_GB:-0}" -ge 30 ] || die "only ${FREE_GB}GB free at $(dirname "$WORK"); the build needs ~30GB"
info "${FREE_GB}GB free"

mkdir -p "$WORK"
PORT="$WORK/move-spi-armbian"
BUILD="$WORK/build"

# --------------------------------------------------------------------- clones
b "sources"
if [ -d "$PORT/.git" ]; then
    info "port checkout present — reusing $PORT"
else
    git clone --depth 1 "$PORT_REPO" "$PORT" || die "could not clone $PORT_REPO"
    info "cloned the Move port"
fi
if [ -d "$BUILD/.git" ]; then
    info "armbian build framework present — reusing $BUILD"
else
    git clone --depth 1 "$ARMBIAN_REPO" "$BUILD" || die "could not clone $ARMBIAN_REPO"
    info "cloned armbian/build"
fi

# ------------------------------------------------ firmware, off the users Move
b "extracting Move firmware from your device"
mkdir -p "$BUILD/userpatches/overlay/extras"
if ls "$BUILD/userpatches/overlay/extras"/move-firmware_*.deb >/dev/null 2>&1; then
    info "firmware deb already built — reusing it"
else
    # The port's extractor reads the vendored Ableton libraries out of /usr/lib.
    # On a Move that has ALREADY been converted they are not there any more --
    # they live in /opt/move/lib, where the firmware deb installed them -- and
    # the extractor aborts on the missing glob rather than reaching its own
    # "not found; skipping" branch (`ls` fails, `pipefail` fails the pipeline,
    # `set -e` ends the script). Check for that here so the failure is legible,
    # 220MB of rsync earlier than it would otherwise appear.
    MISSING=$(ssh -o BatchMode=yes -o StrictHostKeyChecking=no "root@${MOVE_HOST}" \
        'n=0; for p in libc++ libXSDBusCpp libXTCMalloc libubootenv libswupdate; do
             ls /usr/lib/${p}.so* >/dev/null 2>&1 || n=$((n+1)); done; echo $n' 2>/dev/null || echo 9)
    if [ "${MISSING:-9}" != 0 ]; then
        die "root@${MOVE_HOST} is missing Ableton's vendored libraries in /usr/lib.
   That means this Move has already been converted to Armbian: the libraries now
   live in /opt/move/lib, and the port's extractor only looks in /usr/lib.

   The firmware has to come off a Move running STOCK AbletonOS. Either:
     * run this against a Move that is still stock, or
     * boot this one back to stock for a single boot and re-run:
           ssh root@${MOVE_HOST} /usr/local/sbin/boot-stock
     * or drop an already-built move-firmware_*.deb into
           $BUILD/userpatches/overlay/extras/
       and this step will reuse it."
    fi

    # dpkg-deb/fakeroot do not exist on macOS, so the port's script runs in a
    # container. It reads /opt/move off the Move over ssh and packages it.
    # --firmware-only: the user-data deb would carry the whole sample library,
    # and the conversion preserves /data in place anyway.
    docker run --rm \
        -v "$PORT":/repo:ro \
        -v "$BUILD/userpatches/overlay/extras":/out \
        -v "$HOME/.ssh":/root/.ssh:ro \
        "$DEB_IMAGE" bash -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq >/dev/null
            [ "$(dpkg --print-architecture)" = arm64 ] || {
                echo "container is $(dpkg --print-architecture), not arm64" >&2; exit 1; }
            apt-get install -y -qq --no-install-recommends \
                rsync fakeroot dpkg-dev openssh-client ca-certificates >/dev/null
            cp -r /repo /work && cd /work
            ./scripts/extract-move-firmware.sh \
                --host root@'"${MOVE_HOST}"' --firmware-only --output /out
        ' || die "firmware extraction failed"
    info "firmware deb built: $(ls "$BUILD/userpatches/overlay/extras"/move-firmware_*.deb | xargs -n1 basename)"
fi

# ---------------------------------------------------- move-bringup (ablspi &c)
b "building move-bringup (ablspi driver, overlays, data.mount)"
if ls "$BUILD/userpatches/overlay/extras"/move-bringup_*.deb >/dev/null 2>&1; then
    info "move-bringup deb already built — reusing it"
else
    docker run --rm \
        -v "$PORT":/port \
        -v "$BUILD/userpatches/overlay/extras":/out \
        "$DEB_IMAGE" bash -c '
            set -e
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq >/dev/null
            [ "$(dpkg --print-architecture)" = arm64 ] || {
                echo "container is $(dpkg --print-architecture), not arm64" >&2; exit 1; }
            apt-get install -y -qq --no-install-recommends \
                build-essential fakeroot dpkg-dev debhelper device-tree-compiler \
                dh-dkms dkms >/dev/null
            cp -r /port /build && cd /build/move-bringup
            dpkg-buildpackage -us -uc -b
            cp ../move-bringup_*.deb /out/
        ' || die "move-bringup build failed"
    info "move-bringup deb built"
fi

# -------------------------------------------------------------------- staging
b "staging the Move port into the Armbian tree"
# This list is BUILD.md's, verbatim in effect.
mkdir -p "$BUILD/config/boards" "$BUILD/userpatches/extensions" \
         "$BUILD/userpatches/extras" "$BUILD/userpatches/overlay/dts"
cp "$PORT/armbian/config/boards/move.csc"          "$BUILD/config/boards/"
cp "$PORT/armbian/userpatches/customize-image.sh"  "$BUILD/userpatches/"
cp "$PORT"/armbian/userpatches/extensions/*.sh     "$BUILD/userpatches/extensions/"
cp "$PORT/armbian/userpatches/extras/verify.sh"    "$BUILD/userpatches/extras/"
cp "$PORT/kernel-config/move.fragment.config"      "$BUILD/userpatches/"
cp "$PORT"/dts/ablspi-move-cm*.dts                 "$BUILD/userpatches/overlay/dts/"
# customize-image.sh builds move-bringup INSIDE the image chroot from this source
# tree (bind-mounted to /tmp/overlay/move-bringup-src). Without it the build dies
# late, during rootfs customization, with "neither ... nor pre-built core debs".
rm -rf "$BUILD/userpatches/overlay/move-bringup-src"
cp -a "$PORT/move-bringup" "$BUILD/userpatches/overlay/move-bringup-src"
info "board file, customize hook, extension, fragment, dts and move-bringup source staged"

# --------------------------------------------------------------------- compile
b "compiling (this is the long part — an hour or more on a first run)"
info "BOARD=move BRANCH=current RELEASE=trixie BUILD_MINIMAL=yes KERNEL_CONFIGURE=no"
( cd "$BUILD" && ./compile.sh \
        BOARD=move BRANCH=current RELEASE=trixie \
        BUILD_DESKTOP=no BUILD_MINIMAL=yes \
        KERNEL_CONFIGURE=no ) || die "the Armbian build failed — see the output above"

IMG=$(ls -t "$BUILD"/output/images/*.img 2>/dev/null | head -1)
[ -n "$IMG" ] || die "the build finished but produced no image in $BUILD/output/images"
b "built"
info "$IMG"
echo "$IMG" > "$WORK/LAST_IMAGE"

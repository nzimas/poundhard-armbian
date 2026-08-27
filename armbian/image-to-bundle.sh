#!/usr/bin/env bash
# Turn a built Armbian .img into the bundle layout the converter installs.
#
#   ./armbian/image-to-bundle.sh <image.img> [outdir]
#
# The image is what `armbian/build-armbian.sh` produces (or what you get by
# following the port's own BUILD.md). It is laid out for a fresh SD card -- two
# partitions, its own UUIDs -- and we are not flashing a card: we install into
# the Move's existing partitions, in place, over the network. So the pieces are
# taken out of it rather than dd'd onto anything.
#
# Runs the extraction inside a container: loop-mounting and reading an ext4
# rootfs with ownership intact is not something macOS can do.
set -euo pipefail

IMG="${1:?usage: image-to-bundle.sh <image.img> [outdir]}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$HERE/bundle/armbian}"
[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || { echo "docker is required" >&2; exit 1; }
mkdir -p "$OUT"

docker run --rm --privileged \
    -v "$(cd "$(dirname "$IMG")" && pwd)":/img:ro \
    -v "$HERE/armbian/boot":/phboot:ro \
    -v "$OUT":/out \
    debian:trixie-slim bash -c '
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null
apt-get install -y -qq --no-install-recommends util-linux mount tar gzip coreutils device-tree-compiler >/dev/null

IMG=/img/'"$(basename "$IMG")"'
L=$(losetup --show -fP -r "$IMG"); LN=$(basename "$L")
# No udev in here: the kernel makes the partitions but no device nodes appear.
for n in 1 2; do
    d=/sys/block/$LN/${LN}p$n/dev
    [ -f "$d" ] && mknod "${L}p$n" b "$(cut -d: -f1 "$d")" "$(cut -d: -f2 "$d")" || true
done
mkdir -p /m1 /m2
mount -o ro "${L}p1" /m1
mount -o ro "${L}p2" /m2

echo "== boot payload"
mkdir -p /out/boot/overlays
# The kernel. Named armbian-Image on the Move so it cannot be confused with
# Ableton_s own kernel8.img sitting in the same FAT partition.
cp /m2/boot/vmlinuz-*-current-bcm2711 /out/boot/armbian-Image
echo "   kernel  $(stat -c %s /out/boot/armbian-Image) bytes"
# Deliberately NO initrd: the Move boots the kernel directly (root= by PARTUUID,
# ext4 built in), and its boot partition is 68MB with Ableton_s files already on
# it -- 20MB of initramfs we never load would not fit alongside a 29MB kernel.

# The CM4 device tree Armbian built, kept under its own name so it never
# collides with the stock one already on the Move_s boot partition.
cp /m1/bcm2711-rpi-cm4.dtb /out/boot/bcm2711-rpi-cm4-armbian.dtb
echo "   dtb     $(stat -c %s /out/boot/bcm2711-rpi-cm4-armbian.dtb) bytes"

# ablspi is the whole point of the port: it binds /dev/ablspi0.0.
cp /m1/overlays/ablspi-move-cm4.dtbo /out/boot/overlays/
[ -f /m1/overlays/ablspi-move-cm5.dtbo ] && cp /m1/overlays/ablspi-move-cm5.dtbo /out/boot/overlays/ || true
for o in dwc2.dtbo; do
    [ -f "/m1/overlays/$o" ] && cp "/m1/overlays/$o" /out/boot/overlays/ || true
done
# Ours, not the port_s: ablspi needs GPIO3, and the stock spidev@0 node claims it.
dtc -@ -I dts -O dtb -o /out/boot/overlays/move-spidev0-off.dtbo     /phboot/move-spidev0-off.dts 2>/dev/null   || { echo "could not compile move-spidev0-off.dts" >&2; exit 1; }
echo "   overlays: $(ls /out/boot/overlays | tr "\n" " ")"

echo "== rootfs"
# Same exclusions as a device capture, for the same reasons: per-device identity
# must be generated, not shared, and the users own data is not ours to move.
tar -C /m2 -czf /out/rootfs.tar.gz \
    --exclude=./proc/"*" --exclude=./sys/"*" --exclude=./dev/"*" \
    --exclude=./run/"*" --exclude=./tmp/"*" --exclude=./mnt/"*" --exclude=./media/"*" \
    --exclude=./lost+found --exclude=./swapfile \
    --exclude=./etc/ssh/ssh_host_"*" \
    --exclude=./etc/NetworkManager/system-connections \
    --exclude=./etc/NetworkManager/system-connections/"*" \
    --exclude=./etc/machine-id --exclude=./var/lib/dbus/machine-id \
    --exclude=./root/.ssh --exclude=./root/.ssh/"*" \
    --exclude=./var/log/"*" --exclude=./var/tmp/"*" \
    --exclude=./var/cache/apt/archives/"*.deb" --exclude=./var/lib/apt/lists/"*" \
    --one-file-system --numeric-owner .

umount /m1 /m2; losetup -d "$L"
echo "   rootfs.tar.gz $(stat -c %s /out/rootfs.tar.gz) bytes"
'

echo "== verifying"
gzip -t "$OUT/rootfs.tar.gz" || { echo "rootfs archive is corrupt" >&2; exit 1; }
for must in ./etc/fstab ./usr/lib/systemd/system/data.mount ./usr/bin/python3; do
    tar -tzf "$OUT/rootfs.tar.gz" "$must" >/dev/null 2>&1 \
      || { echo "rootfs is missing $must" >&2; exit 1; }
done
echo "   complete: fstab, data.mount, python3"
for leak in './etc/ssh/ssh_host_' './etc/machine-id' './root/.ssh/' './etc/NetworkManager/system-connections/.'; do
    n=$(tar -tzf "$OUT/rootfs.tar.gz" 2>/dev/null | grep -c "^$leak" || true)
    [ "$n" = 0 ] || { echo "per-device identity leaked into the bundle: $leak" >&2; exit 1; }
done
echo "   clean: no shared identity or credentials"

cp "$HERE/armbian/boot/config.txt.armbian"   "$OUT/boot/config.txt.armbian"
cp "$HERE/armbian/boot/armbian-cmdline.txt"  "$OUT/boot/armbian-cmdline.txt"
{
    echo "# PoundHard Armbian bundle"
    echo "redistributable=no"
    echo "kernel=$(basename "$(tar -tzf "$OUT/rootfs.tar.gz" './boot/vmlinuz-*' 2>/dev/null | head -1)" 2>/dev/null)"
    echo "built_from=$(basename "$IMG")"
    echo "source=built-from-source"
} > "$OUT/MANIFEST"
( cd "$OUT" && find . -type f ! -name SHA256SUMS -exec shasum -a 256 {} + | sort -k2 > SHA256SUMS )
echo "== done: $OUT ($(du -sh "$OUT" | cut -f1))"

# The image is built with a firmware deb extracted from YOUR Move, so the rootfs
# contains Ableton's /opt/move. That is exactly right for a bundle built on your
# own machine from your own device -- and exactly why this must never be uploaded
# anywhere. Say so out loud rather than leaving it to be discovered.
if tar -tzf "$OUT/rootfs.tar.gz" 2>/dev/null | grep -q '^\./opt/move/'; then
    printf '\n\033[1m   NOTE:\033[0m this bundle contains Ableton'"'"'s /opt/move, taken from your own\n'
    printf '   device during the build. It is yours to install on your own Move.\n'
    printf '   Do NOT publish or redistribute it.\n'
fi

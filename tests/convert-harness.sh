#!/usr/bin/env bash
# Test armbian/convert.sh against a SYNTHETIC stock Move layout.
#
# The conversion cannot be rehearsed on the real device: once p4 carries the
# Armbian root, that machine is no longer a stock Move and re-running the
# converter against it would relocate the rootfs into move-data. So the layout
# is built from scratch here -- a loopback disk with the Move's partition
# geometry, a stock-shaped p4, a stock config.txt on p1 -- and the real
# converter is run against it, unmodified.
#
# Runs inside an arm64 Linux container (needs loop devices => --privileged).
set -euo pipefail
W=/work
IMG=$W/fake-move.img
BUNDLE=${BUNDLE:-/bundle}
ok(){ printf '\033[32m  PASS\033[0m %s\n' "$*"; }
no(){ printf '\033[31m  FAIL\033[0m %s\n' "$*"; FAILED=1; }
FAILED=0

echo "== building a synthetic stock Move card"
rm -f "$IMG"; truncate -s 8G "$IMG"   # sparse; p4 must fit the ~2.2GB rootfs + the staged tarball
parted -s "$IMG" mklabel msdos \
    mkpart primary fat32 4MiB 72MiB \
    mkpart primary ext4  72MiB 200MiB \
    mkpart primary ext4  200MiB 260MiB \
    mkpart primary ext4  260MiB 100%
LOOP=$(losetup --show -fP "$IMG")
LN=$(basename "$LOOP")
# Docker's /dev is a tmpfs, not devtmpfs, and there is no udev in here -- the
# kernel creates the partitions but no device nodes appear. Make them by hand
# from sysfs so the converter sees an ordinary partitioned card.
partx -a "$LOOP" 2>/dev/null || true
for n in 1 2 3 4; do
    sysdev="/sys/block/$LN/${LN}p$n/dev"
    [ -f "$sysdev" ] || continue
    mknod "${LOOP}p$n" b "$(cut -d: -f1 "$sysdev")" "$(cut -d: -f2 "$sysdev")" 2>/dev/null || true
done
[ -b "${LOOP}p1" ] || { echo "could not create loop partition nodes"; exit 1; }
mkfs.vfat -n boot "${LOOP}p1" >/dev/null
mkfs.ext4 -q -F "${LOOP}p2"
mkfs.ext4 -q -F "${LOOP}p4" -b 1024 -O meta_bg,^resize_inode
echo "   $LOOP  p1=vfat p2=ext4 p4=ext4(meta_bg,1k) — matching the real card"

# The converter addresses the card by its real device names.
for n in 1 2 4; do rm -f /dev/mmcblk0p$n; ln -s "${LOOP}p$n" /dev/mmcblk0p$n; done

mkdir -p /data /mnt/p2
mount "${LOOP}p4" /data
mount "${LOOP}p1" /mnt/p1x 2>/dev/null || { mkdir -p /mnt/p1x; mount "${LOOP}p1" /mnt/p1x; }

echo "== populating it like a stock Move"
# stock p4 == /data: user content at the root of the partition
mkdir -p /data/UserData/rnbo/lib/jack /data/UserData/Sets /data/settings/connman/lib/connman
echo "fake native driver" > /data/UserData/rnbo/lib/jack/jack_move.so
echo "a users song"       > /data/UserData/Sets/song1.abl
dd if=/dev/urandom of=/data/UserData/bigsample.wav bs=1M count=8 status=none
d=/data/settings/connman/lib/connman/wifi_aabbccddeeff_4d5953534944_managed_psk
mkdir -p "$d"
cat > "$d/settings" <<EOF
[wifi_aabbccddeeff_4d5953534944_managed_psk]
Name=MYSSID
SSID=4d5953534944
Favorite=true
AutoConnect=true
Passphrase=sup3rs3cr3t-psk
IPv4.method=dhcp
EOF
# stock root bits the converter has to carry across
mkdir -p /opt/move/Dsp/Vector/Sprites /root/.ssh
echo "proprietary sprite" > /opt/move/Dsp/Vector/Sprites/basic.bin
echo "ssh-ed25519 AAAAC3Nz-fake-key user@host" > /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys
touch /dev/ablspi0.0 2>/dev/null || true
# a stock-looking config.txt on p1
printf '# stock ableton config\narm_64bit=1\nkernel=kernel8.img\n' > /mnt/p1x/config.txt
umount /mnt/p1x

# Measured excluding UserData/poundhard: the conversion deliberately CREATES that
# tree (it is where the native driver is migrated to), so counting it would make
# a correct conversion look like it had altered the user's data.
USER_BYTES_BEFORE=$(du -sb --exclude=poundhard /data/UserData | cut -f1)
echo "   /data/UserData = $USER_BYTES_BEFORE bytes before conversion (excl. poundhard)"

echo "== staging the bundle"
mkdir -p /data/.ph-convert
cp -r "$BUNDLE"/boot /data/.ph-convert/
cp "$BUNDLE"/rootfs.tar.gz /data/.ph-convert/

echo "== RUNNING THE REAL CONVERTER"
# reboot is the last line; neutralise it so the harness survives to assert.
mkdir -p /shim && printf '#!/bin/sh\necho "[harness] reboot suppressed"\n' > /shim/reboot
chmod +x /shim/reboot
PATH=/shim:$PATH bash /work/convert.sh || { echo "converter exited non-zero"; FAILED=1; }

echo
echo "== assertions"
[ -d /data/var/lib/move-data/UserData/Sets ] \
  && ok "user data relocated to /var/lib/move-data" || no "user data NOT relocated"
USER_BYTES_AFTER=$(du -sb --exclude=poundhard /data/var/lib/move-data/UserData 2>/dev/null | cut -f1 || echo 0)
[ "$USER_BYTES_AFTER" = "$USER_BYTES_BEFORE" ] \
  && ok "pre-existing user data intact byte-for-byte ($USER_BYTES_AFTER)" \
  || no "user data changed size: $USER_BYTES_BEFORE -> $USER_BYTES_AFTER"
[ -f /data/var/lib/move-data/UserData/rnbo/lib/jack/jack_move.so ] \
  && ok "the original rnbo tree is left untouched" || no "rnbo tree altered"
# The whole point: the driver is copied into PoundHard's own tree, so the RNBO
# takeover can be deleted afterwards and nothing in the system points into it.
MIG=/data/var/lib/move-data/UserData/poundhard/lib/jack/jack_move.so
if [ -f "$MIG" ]; then
    ok "driver migrated into PoundHard's tree"
    A=$(sha256sum "$MIG" | cut -d" " -f1)
    B=$(sha256sum /data/var/lib/move-data/UserData/rnbo/lib/jack/jack_move.so | cut -d" " -f1)
    [ "$A" = "$B" ] && ok "migrated driver is byte-identical" || no "driver copy differs"
else
    no "driver NOT migrated — the stack would still depend on RNBO"
fi
[ -d /data/usr/bin ] && [ -d /data/etc/systemd ] \
  && ok "Armbian rootfs unpacked onto p4" || no "rootfs not unpacked"
[ -f /data/opt/move/Dsp/Vector/Sprites/basic.bin ] \
  && ok "/opt/move carried across from the device" || no "/opt/move missing"
grep -q 'fake-key' /data/root/.ssh/authorized_keys 2>/dev/null \
  && ok "authorized_keys installed (device stays reachable)" || no "authorized_keys missing"
[ "$(stat -c %a /data/root/.ssh/authorized_keys 2>/dev/null)" = 600 ] \
  && ok "authorized_keys mode 600" || no "authorized_keys wrong mode"

NM=/data/etc/NetworkManager/system-connections/move-wifi.nmconnection
if [ -f "$NM" ]; then
    grep -q 'ssid=MYSSID' "$NM" && ok "wi-fi SSID migrated" || no "SSID wrong"
    grep -q 'psk=sup3rs3cr3t-psk' "$NM" && ok "wi-fi PSK migrated" || no "PSK not migrated"
    [ "$(stat -c %a "$NM")" = 600 ] && ok "wi-fi keyfile mode 600" || no "keyfile mode $(stat -c %a "$NM")"
else no "no NetworkManager connection written"; fi

[ -s /data/etc/machine-id ] && ok "machine-id generated" || no "no machine-id"
[ -f /data/etc/ssh/ssh_host_ed25519_key ] \
  && ok "per-device ssh host keys generated" || no "no ssh host keys"

mkdir -p /mnt/p1y; mount /dev/mmcblk0p1 /mnt/p1y
grep -q 'kernel=armbian-Image' /mnt/p1y/config.txt \
  && ok "config.txt now boots Armbian" || no "config.txt not switched"
grep -q 'kernel8.img' /mnt/p1y/config.txt.stock-original \
  && ok "stock config.txt preserved" || no "stock config.txt lost"
grep -q 'kernel8.img' /mnt/p1y/tryboot.txt \
  && ok "tryboot.txt is the escape to stock" || no "tryboot.txt wrong"
[ -f /mnt/p1y/armbian-Image ] && ok "kernel installed on p1" || no "kernel missing"
[ -f /mnt/p1y/overlays/ablspi-move-cm4.dtbo ] && ok "ablspi overlay installed" || no "overlay missing"
WANT=$(blkid -s PARTUUID -o value /dev/mmcblk0p4)
grep -q "root=PARTUUID=$WANT" /mnt/p1y/armbian-cmdline.txt \
  && ok "cmdline root= points at THIS card ($WANT)" || no "cmdline root= not rewritten"
umount /mnt/p1y

# The PSK passes through a staging file. It must not still be sitting there:
# /data survives the conversion, so anything left behind is left behind for good.
if grep -rqs 'sup3rs3cr3t-psk' /data/.ph-convert 2>/dev/null; then
    no "plaintext wi-fi PSK left behind in the staging area"
    grep -rls 'sup3rs3cr3t-psk' /data/.ph-convert 2>/dev/null | head -3
else
    ok "no plaintext PSK left in the staging area"
fi
# ...and nowhere the conversion CREATED it, except the 0600 keyfile meant to hold
# it. move-data is excluded on purpose: the user's own stock ConnMan config lives
# there and holds this passphrase because it always did. Preserving it is the
# point -- deleting it would be destroying their data, not protecting them.
STRAY=$(grep -rls 'sup3rs3cr3t-psk' /data/etc /data/root 2>/dev/null \
        | grep -v 'system-connections/move-wifi.nmconnection' | head -3 || true)
[ -z "$STRAY" ] && ok "PSK confined to the NetworkManager keyfile" \
                || { no "PSK found outside the keyfile:"; echo "$STRAY"; }
grep -qs 'sup3rs3cr3t-psk' /data/var/lib/move-data/settings/connman/lib/connman/*/settings \
  && ok "user's own stock ConnMan config preserved untouched" \
  || no "the user's stock ConnMan config was altered or lost"

# The unpacked system has to be coherent, not merely present.
[ -f /data/usr/lib/systemd/system/data.mount ] \
  && ok "data.mount present (so /data will be bind-mounted on boot)" || no "data.mount missing"
[ -L /data/etc/systemd/system/local-fs.target.wants/data.mount ] \
  && ok "data.mount is enabled" || no "data.mount not enabled"
grep -q 'mmcblk0p4' /data/etc/fstab 2>/dev/null \
  && ok "fstab roots on p4" || no "fstab wrong"
grep -q 'mmcblk0p1 */boot/firmware' /data/etc/fstab 2>/dev/null \
  && ok "fstab mounts the boot partition" || no "fstab has no /boot/firmware entry"
# A source-built rootfs arrives with the build image's UUIDs. If any survive,
# /boot/firmware would silently never mount on this card.
grep -q '^UUID=' /data/etc/fstab 2>/dev/null \
  && { no "stale UUIDs from the build image left in fstab:"; grep '^UUID=' /data/etc/fstab; } \
  || ok "no stale build-image UUIDs in fstab"

echo
echo "== re-running the converter on the now-converted card (must refuse)"
if PATH=/shim:$PATH bash /work/convert.sh >/tmp/second.log 2>&1; then
    no "converter did NOT refuse a second run — it would bury the rootfs"
else
    grep -q 'ALREADY converted' /tmp/second.log \
      && ok "second run refused: $(grep -o 'already carries an Armbian root' /tmp/second.log | head -1)" \
      || { no "refused, but not for the right reason:"; tail -3 /tmp/second.log; }
fi

echo
if [ "$FAILED" = 0 ]; then printf '\033[32m== ALL ASSERTIONS PASSED\033[0m\n'; else printf '\033[31m== FAILURES ABOVE\033[0m\n'; fi
umount /data 2>/dev/null || true
losetup -d "$LOOP" 2>/dev/null || true
exit $FAILED

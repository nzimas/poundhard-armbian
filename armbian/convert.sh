#!/bin/bash
# Convert a stock Ableton Move to Armbian, in place, over the network.
#
# Runs ON the Move, under stock AbletonOS, as root. install.sh stages the bundle
# and invokes this; it is not normally run by hand.
#
# The card is never removed. That is not a convenience — this instrument is built
# for a user with a severe sight impairment, for whom "pop the SD card out" is not
# a recovery procedure. Everything here therefore has to be reversible from the
# network, which is why the stock boot config is preserved as a one-shot tryboot
# escape before anything is written.
#
# The conversion is NON-DESTRUCTIVE to the user's data. Armbian's root goes onto
# p4 -- the same 54 GB filesystem that stock uses for /data -- and the existing
# contents are RENAMED into /var/lib/move-data (same filesystem, so it is a rename
# and not a copy) and bind-mounted back at /data. Projects, samples and the RNBO
# tree all survive. Stock's own root on p2 is never touched, which is what makes
# the tryboot escape work.
set -euo pipefail

STAGE="${STAGE:-/data/.ph-convert}"
P1=/dev/mmcblk0p1
P4=/dev/mmcblk0p4
MNT1=/mnt/ph-p1
PRESERVE="$STAGE/preserve"

b()   { printf '\033[1m%s\033[0m\n' "$*"; }
info(){ printf '   %s\n' "$*"; }
die() { printf '\033[31mFATAL: %s\033[0m\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- 0. preflight
b "== preflight"
[ "$(id -u)" = 0 ] || die "must run as root"
case "$(uname -r)" in
    *current-bcm2711*) die "this device is ALREADY running Armbian — nothing to convert" ;;
esac
[ -b "$P4" ] || die "no $P4 — this is not a Move SD layout"
[ -b "$P1" ] || die "no $P1"
[ -e /dev/ablspi0.0 ] || die "no /dev/ablspi0.0 — this is not a Move"

# p4 must be the partition mounted at /data, or we are about to unpack a rootfs
# over something we have not identified.
SRC=$(findmnt -no SOURCE /data 2>/dev/null || true)
# Compare resolved paths: /dev/mmcblk0p4 may be reached through a symlink, and
# findmnt always reports the real node.
SRC_R=$(readlink -f "${SRC:-/nonexistent}" 2>/dev/null || echo /nonexistent)
P4_R=$(readlink -f "$P4")
[ "$SRC_R" = "$P4_R" ] || die "/data is not $P4 (it is '${SRC:-nothing}') — refusing to write"

# The uname check above catches "running Armbian", but not the genuinely dangerous
# case: booting a CONVERTED card into stock via tryboot and running this again. The
# kernel would be stock, every other check would pass, and the relocation would
# bury the Armbian rootfs inside move-data. Look at the card, not at the kernel.
if [ -e /data/etc/fstab ] || [ -e /data/usr/lib/systemd/system/data.mount ]; then
    die "partition 4 already carries an Armbian root — this card is ALREADY converted.
   (You are seeing stock because of a one-shot tryboot; a normal power cycle boots
   Armbian.) Refusing: converting twice would bury the rootfs inside move-data."
fi

[ -f "$STAGE/rootfs.tar.gz" ] || die "bundle missing: $STAGE/rootfs.tar.gz"
[ -f "$STAGE/boot/armbian-Image" ] || die "bundle missing: $STAGE/boot/armbian-Image"

# jack_move.so is the native driver the whole instrument is built on, and it is
# NOT ours to redistribute -- it has to already be on this device.
JACKDRV=/data/UserData/rnbo/lib/jack/jack_move.so
[ -f "$JACKDRV" ] || die "missing $JACKDRV
   The native Move JACK driver comes from the RNBO runtime on your device and is
   not shipped in this bundle. Install RNBO on the Move (stock) first."
info "jack_move.so present"

for t in tar gzip python3 ssh-keygen blkid findmnt mountpoint df; do
    command -v "$t" >/dev/null 2>&1 || die "stock system is missing '$t', which the
   conversion needs. Refusing to start: a tool discovered missing halfway through
   would leave this machine half-converted."
done
info "required tools present"

FREE4=$(df -Pk /data | awk 'NR==2{print int($4/1024)}')
[ "$FREE4" -ge 4096 ] || die "only ${FREE4}MB free on /data; need >= 4096MB"
info "p4 free: ${FREE4}MB"

mkdir -p "$MNT1"
mountpoint -q "$MNT1" || mount "$P1" "$MNT1" || die "cannot mount $P1"
FREE1=$(df -Pk "$MNT1" | awk 'NR==2{print int($4/1024)}')
NEED1=$(( ( $(stat -c %s "$STAGE/boot/armbian-Image") / 1048576 ) + 4 ))
[ "$FREE1" -ge "$NEED1" ] || die "p1 has ${FREE1}MB free, kernel needs ${NEED1}MB"
info "p1 free: ${FREE1}MB (kernel needs ${NEED1}MB)"
[ -f "$MNT1/config.txt" ] || die "no config.txt on p1"

# ------------------------------------------------- 1. preserve what we can't ship
b "== preserving device-owned content"
rm -rf "$PRESERVE"; mkdir -p "$PRESERVE"

# Ableton's own software: proprietary, never redistributed, but WTABLE reads its
# factory sprites and the shutdown path calls MoveXmosPower. It comes across from
# the user's own stock root.
if [ -d /opt/move ]; then
    info "copying /opt/move ($(du -sh /opt/move 2>/dev/null | cut -f1)) off stock root..."
    cp -a /opt/move "$PRESERVE/opt-move"
    info "/opt/move preserved"
else
    info "no /opt/move on this device (WTABLE sprites will be unavailable)"
fi

# Ableton's dbus activation files belong with /opt/move: also theirs, also taken
# from this device rather than shipped.
mkdir -p "$PRESERVE/dbus/system-services" "$PRESERVE/dbus/system.d"
for f in /usr/share/dbus-1/system-services/com.ableton.*; do
    [ -e "$f" ] && cp -a "$f" "$PRESERVE/dbus/system-services/" || true
done
[ -e /etc/dbus-1/system.d/move.conf ] && cp -a /etc/dbus-1/system.d/move.conf "$PRESERVE/dbus/system.d/" || true
info "ableton dbus service files preserved"

# SSH access. Losing this loses the machine: there is no Ethernet on a Move.
mkdir -p "$PRESERVE/ssh"
for f in /root/.ssh/authorized_keys /home/ableton/.ssh/authorized_keys; do
    [ -f "$f" ] && cat "$f" >> "$PRESERVE/ssh/authorized_keys" || true
done
[ -s "$PRESERVE/ssh/authorized_keys" ] || die "no authorized_keys found on stock —
   converting now would leave the device unreachable. ssh-copy-id to it first."
info "authorized_keys preserved ($(wc -l < "$PRESERVE/ssh/authorized_keys") key(s))"

# Wi-Fi is the only lifeline. Stock keeps it in ConnMan; Armbian uses
# NetworkManager, so the credential is translated, never printed.
python3 - "$PRESERVE/wifi.env" <<'PY'
import glob, sys, os
out = sys.argv[1]
ssid = psk = None
for f in glob.glob('/data/settings/connman/lib/connman/*_managed_psk/settings'):
    d = {}
    for line in open(f, errors='replace'):
        if '=' in line:
            k, _, v = line.partition('=')
            d[k.strip()] = v.strip()
    if d.get('Name') and d.get('Passphrase'):
        ssid, psk = d['Name'], d['Passphrase']
        if d.get('Favorite') == 'true':
            break
if not ssid:
    sys.exit("no saved Wi-Fi network found in ConnMan")
fd = os.open(out, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, 'w') as fh:
    fh.write('SSID=%s\nPSK=%s\n' % (ssid, psk))
print('   wi-fi network preserved: %s' % ssid)
PY

# ------------------------------------------------------ 2. the point of no return
b "== relocating stock data into /var/lib/move-data"
DEST=/data/var/lib/move-data
# Relocate any pre-existing /data/var FIRST: the loop below has to skip 'var' (it is
# where move-data lives), so anything already there would be silently buried under
# the rootfs's own /var.
if [ -e /data/var ]; then
    mkdir -p /data/.ph-oldvar
    mv /data/var /data/.ph-oldvar/var
    info "pre-existing /data/var set aside"
fi
mkdir -p "$DEST"
if [ -d /data/.ph-oldvar/var ]; then
    mv /data/.ph-oldvar/var "$DEST/var"
    rmdir /data/.ph-oldvar 2>/dev/null || true
fi
shopt -s dotglob nullglob
for p in /data/*; do
    case "$(basename "$p")" in
        var|.ph-convert) continue ;;
    esac
    mv "$p" "$DEST/" 2>/dev/null || info "SKIP $(basename "$p")"
done
shopt -u dotglob nullglob
sync
info "moved: $(ls -A "$DEST" | wc -l) entries (rename, not copy — instant)"

b "== unpacking Armbian rootfs onto p4"
tar -C /data -xzf "$STAGE/rootfs.tar.gz" --numeric-owner
sync
info "rootfs unpacked"

# ------------------------------------------------------------- 3. reassemble
b "== restoring device-owned content into the new rootfs"
if [ -d "$PRESERVE/opt-move" ]; then
    mkdir -p /data/opt
    rm -rf /data/opt/move
    mv "$PRESERVE/opt-move" /data/opt/move
    info "/opt/move restored"
fi

if [ -n "$(ls -A "$PRESERVE/dbus/system-services" 2>/dev/null)" ]; then
    mkdir -p /data/usr/share/dbus-1/system-services
    cp -a "$PRESERVE/dbus/system-services/." /data/usr/share/dbus-1/system-services/
fi
if [ -n "$(ls -A "$PRESERVE/dbus/system.d" 2>/dev/null)" ]; then
    mkdir -p /data/etc/dbus-1/system.d
    cp -a "$PRESERVE/dbus/system.d/." /data/etc/dbus-1/system.d/
fi
info "ableton dbus service files restored"

install -d -m 700 /data/root/.ssh
install -m 600 "$PRESERVE/ssh/authorized_keys" /data/root/.ssh/authorized_keys
info "authorized_keys installed"

# A shared machine-id across installs collides DHCP leases; a shared ssh host key
# is worse. Both are generated here, per device.
python3 -c "import uuid;open('/data/etc/machine-id','w').write(uuid.uuid4().hex+'\n')"
mkdir -p /data/var/lib/dbus
cp /data/etc/machine-id /data/var/lib/dbus/machine-id
for t in rsa ecdsa ed25519; do
    f=/data/etc/ssh/ssh_host_${t}_key
    rm -f "$f" "$f.pub"
    ssh-keygen -q -t "$t" -N '' -f "$f" -C "move-armbian" </dev/null
done
info "machine-id + ssh host keys generated for this device"

# Wi-Fi, translated ConnMan -> NetworkManager. Written 0600, never echoed.
python3 - "$PRESERVE/wifi.env" <<'PY'
import os, sys, uuid
env = dict(l.rstrip('\n').split('=', 1) for l in open(sys.argv[1]) if '=' in l)
d = '/data/etc/NetworkManager/system-connections'
os.makedirs(d, exist_ok=True)
p = os.path.join(d, 'move-wifi.nmconnection')
fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
with os.fdopen(fd, 'w') as fh:
    fh.write("""[connection]
id=move-wifi
uuid=%s
type=wifi
autoconnect=true
autoconnect-priority=100

[wifi]
mode=infrastructure
ssid=%s
hidden=false

[wifi-security]
key-mgmt=wpa-psk
psk=%s

[ipv4]
method=auto

[ipv6]
method=auto
""" % (uuid.uuid4(), env['SSID'], env['PSK']))
print('   wi-fi connection written for %s' % env['SSID'])
PY
rm -f "$PRESERVE/wifi.env"

# ------------------------------------------------------------- 4. boot partition
b "== boot partition"
# Save the stock config BEFORE overwriting it, and put a copy in tryboot.txt.
# That file is the way back: the RPi firmware honours it for exactly one boot, so
# `boot-stock` returns to AbletonOS without touching the card.
if [ ! -f "$MNT1/config.txt.stock-original" ]; then
    cp "$MNT1/config.txt" "$MNT1/config.txt.stock-original"
    info "stock config.txt saved as config.txt.stock-original"
fi
{
    echo "# ONE-SHOT ESCAPE TO STOCK AbletonOS."
    echo "# Reached only via a tryboot reboot (see /usr/local/sbin/boot-stock)."
    echo "# A normal power cycle uses config.txt, which boots Armbian."
    cat "$MNT1/config.txt.stock-original"
} > "$MNT1/tryboot.txt"
info "tryboot.txt written (escape to stock)"

cp "$STAGE/boot/armbian-Image"                 "$MNT1/armbian-Image"
cp "$STAGE/boot/bcm2711-rpi-cm4-armbian.dtb"   "$MNT1/bcm2711-rpi-cm4-armbian.dtb"
cp "$STAGE/boot/armbian-cmdline.txt"           "$MNT1/armbian-cmdline.txt"
mkdir -p "$MNT1/overlays"
cp "$STAGE"/boot/overlays/*.dtbo               "$MNT1/overlays/"
info "kernel, dtb, overlays and cmdline installed"

# The root= in cmdline must match THIS card, not the one the bundle was built on.
PARTUUID=$(blkid -s PARTUUID -o value "$P4")
[ -n "$PARTUUID" ] || die "cannot read PARTUUID of $P4"
python3 - "$MNT1/armbian-cmdline.txt" "$PARTUUID" <<'PY'
import re, sys
p, uid = sys.argv[1], sys.argv[2]
s = open(p).read()
s2 = re.sub(r'root=\S+', 'root=PARTUUID=' + uid, s)
open(p, 'w').write(s2)
print('   cmdline root= set to PARTUUID=%s' % uid)
PY

cp "$STAGE/boot/config.txt.armbian" "$MNT1/config.txt"
info "config.txt now boots Armbian"

sync
umount "$MNT1" || true

b "== converted"
echo "   Rebooting into Armbian. The device will disappear for a minute or two."
echo "   If it does not come back, power-cycle it: config.txt boots Armbian, and"
echo "   the stock system is still intact on p2 behind tryboot.txt."
sync
sleep 2
reboot

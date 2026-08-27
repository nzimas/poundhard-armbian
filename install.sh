#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# PoundHard — install the whole instrument onto an Ableton Move, in one command.
#
#   ./install.sh [host]           default host: move.local
#   ./install.sh move.local --yes non-interactive (skips the conversion prompt)
#
# This is the complete bundle. Run it against a STOCK Move and you get, at the
# end, a machine running Armbian with PoundHard on it. Run it against a Move that
# is already converted and it redeploys PoundHard alone, leaving the OS in place.
#
# It installs, in dependency order:
#   * Armbian (kernel, dtb, overlays, rootfs) — only if the Move is still stock
#   * PoundHard controller + SuperCollider engine + launch scripts
#   * the appliance UI (ui.js) and phhost, the native host that runs it
#   * phgain    — realtime master-volume stage (compiled ON the device)
#   * launcher  — the appliance menu you see at boot
#   * systemd units, realtime tuning, and the JACK watchdog
#
# The SD card is never removed and the stock system is never erased: Armbian's
# root shares the 54 GB partition with your existing projects, and stock stays
# intact on its own partition behind a one-shot `boot-stock` escape.
# ---------------------------------------------------------------------------
set -euo pipefail

die_early() { printf '\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

HOST=""
ASSUME_YES=0
for a in "$@"; do
    case "$a" in
        --yes|-y) ASSUME_YES=1 ;;
        -*)       die_early "unknown option: $a" ;;
        *)        if [ -z "$HOST" ]; then HOST="$a"; fi ;;
    esac
done
if [ -z "$HOST" ]; then HOST="move.local"; fi

HERE="$(cd "$(dirname "$0")" && pwd)"
SSHOPT="-o ConnectTimeout=10 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
SSH="ssh $SSHOPT root@${HOST}"
SCP="scp $SSHOPT -q"
PH=/data/UserData/poundhard
MOD=/data/UserData/schwung/modules/overtake/poundhard

# Where the Armbian bundle comes from when it is not sitting in the repo.
BUNDLE_DIR="${PH_BUNDLE:-$HERE/bundle/armbian}"
BUNDLE_TAG="${PH_BUNDLE_TAG:-armbian-bundle-v1}"
BUNDLE_URL="https://github.com/nzimas/poundhard-armbian/releases/download/${BUNDLE_TAG}"

# macOS tags files with com.apple.provenance; GNU tar on the device warns about every
# one of them. Strip xattrs if the local tar supports it (bsdtar and GNU tar both do).
if tar --no-xattrs -cf /dev/null -T /dev/null 2>/dev/null; then TARFLAGS="--no-xattrs"; else TARFLAGS=""; fi

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
step() { printf '   -> %s\n' "$*"; }
die()  { printf '\n\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

# --- 0. what are we talking to? --------------------------------------------
say "target: root@${HOST}"
$SSH true 2>/dev/null || die "cannot reach root@${HOST} over ssh.
   The installer needs key-based root access. From this machine:
       ssh-copy-id root@${HOST}"

KVER=$($SSH 'uname -r' 2>/dev/null || true)
case "$KVER" in
    *current-bcm2711*) STATE=armbian ;;
    "")                die "could not read the kernel version over ssh" ;;
    *)                 STATE=stock ;;
esac
step "kernel $KVER  →  ${STATE}"

# ===========================================================================
# STAGE A — convert the machine to Armbian, if it is still stock
# ===========================================================================
if [ "$STATE" = stock ]; then
    say "this Move is running stock AbletonOS"

    # The bundle: use a local one if present, otherwise fetch the release asset.
    if [ -f "$BUNDLE_DIR/rootfs.tar.gz" ] && [ -f "$BUNDLE_DIR/boot/armbian-Image" ]; then
        step "using local bundle: $BUNDLE_DIR"
    else
        step "no local bundle — downloading ${BUNDLE_TAG}"
        mkdir -p "$BUNDLE_DIR/boot/overlays"
        for f in MANIFEST SHA256SUMS rootfs.tar.gz \
                 boot/armbian-Image boot/bcm2711-rpi-cm4-armbian.dtb \
                 boot/armbian-cmdline.txt boot/config.txt.armbian \
                 boot/overlays/ablspi-move-cm4.dtbo boot/overlays/move-spidev0-off.dtbo \
                 boot/overlays/ablspi.dtbo; do
            curl -fL# -o "$BUNDLE_DIR/$f" "$BUNDLE_URL/$(echo "$f" | tr '/' '_')" \
              || die "could not download $f from $BUNDLE_URL
   Build it yourself from a working Move with:  ./armbian/build-bundle.sh <host>"
        done
    fi

    if [ -f "$BUNDLE_DIR/SHA256SUMS" ]; then
        step "verifying bundle checksums"
        ( cd "$BUNDLE_DIR" && shasum -a 256 -c SHA256SUMS --quiet ) \
          || die "bundle checksums do not match — refusing to install it"
    fi
    BSIZE=$(du -h "$BUNDLE_DIR/rootfs.tar.gz" | cut -f1)

    printf '\n  \033[1mAbout to convert this Move to Armbian.\033[0m\n\n'
    cat <<EOF
    What happens:
      * Armbian's root is unpacked onto partition 4, alongside your data.
      * Your projects, samples and settings are MOVED (not copied, not deleted)
        to /var/lib/move-data and bind-mounted back at /data. They survive.
      * Stock AbletonOS stays intact on partition 2 and is never written to.
      * The stock boot config is saved first, so \`boot-stock\` returns to
        AbletonOS for one boot at any time. The SD card is never removed.
      * Your Wi-Fi credentials are carried across so the Move stays reachable.

    What you should know:
      * Ableton's software will no longer run at boot. The Move becomes a
        PoundHard machine with an appliance menu.
      * This is not something the installer can undo for you. Recovery is
        \`boot-stock\`, or writing the stock image back to the card.
      * Bundle: ${BSIZE} rootfs + a 29MB kernel, over your network.

EOF
    if [ "$ASSUME_YES" != 1 ]; then
        printf '  Type \033[1mCONVERT\033[0m to proceed, anything else to abort: '
        read -r reply
        [ "$reply" = "CONVERT" ] || { echo "  Aborted. Nothing was written."; exit 1; }
    fi

    say "staging the bundle on the Move"
    # /data is the only place with room: the stock root partition is 463MB and
    # ~99% full, so anything written to /tmp there fails in a confusing way.
    $SSH "rm -rf /data/.ph-convert && mkdir -p /data/.ph-convert/boot/overlays" \
      || die "cannot write to /data on the Move"
    $SCP "$BUNDLE_DIR/boot"/*.dtb "$BUNDLE_DIR/boot"/*.txt "$BUNDLE_DIR/boot/armbian-Image" \
         "root@${HOST}:/data/.ph-convert/boot/"
    $SCP "$BUNDLE_DIR/boot/overlays"/*.dtbo "root@${HOST}:/data/.ph-convert/boot/overlays/"
    step "boot payload staged"
    step "rootfs — this is the slow part (${BSIZE} over the network)"
    $SCP "$BUNDLE_DIR/rootfs.tar.gz" "root@${HOST}:/data/.ph-convert/rootfs.tar.gz"
    $SCP "$HERE/armbian/convert.sh" "root@${HOST}:/data/.ph-convert/convert.sh"
    $SSH 'chmod +x /data/.ph-convert/convert.sh'
    step "staged"

    say "converting (the Move will reboot itself at the end)"
    # The converter reboots, which drops the connection, so ssh coming back
    # non-zero is expected. But a converter that REFUSED in preflight also exits
    # non-zero, and swallowing that would send us into a ten-minute wait for a
    # machine that was never going to reboot. 255 is the dropped connection;
    # anything else is the converter itself, and it has already said why.
    CONV_RC=0
    $SSH 'bash /data/.ph-convert/convert.sh' || CONV_RC=$?
    if [ "$CONV_RC" != 0 ] && [ "$CONV_RC" != 255 ]; then
        die "the converter stopped before changing anything (exit $CONV_RC).
   The reason is printed above. Nothing on the Move was modified."
    fi

    say "waiting for the Move to come back on Armbian"
    # Its ssh host key was regenerated during the conversion (a bundle-wide
    # shared host key would be a real vulnerability), so the old one must go.
    ssh-keygen -R "${HOST}" >/dev/null 2>&1 || true
    ok=0
    for i in $(seq 1 60); do
        sleep 10
        k=$($SSH 'uname -r' 2>/dev/null || true)
        case "$k" in
            *current-bcm2711*) ok=1; break ;;
        esac
        printf '   ...%ds\n' $((i*10))
    done
    [ "$ok" = 1 ] || die "the Move did not come back within 10 minutes.
   Power-cycle it: config.txt now boots Armbian. If it still does not appear,
   it is reachable on the stock system after a tryboot power cycle, and stock
   was never erased."
    step "Armbian is up: $($SSH 'uname -r')"
    # The staging dir was written to p4's root while p4 was mounted at /data. Now
    # that p4 IS /, it lives at /.ph-convert -- roughly a gigabyte of bundle that
    # would otherwise sit there for good.
    $SSH 'rm -rf /.ph-convert /data/.ph-convert' || true
    step "staging area removed"

    say "confirming your data survived"
    $SSH 'n=$(ls -A /data/UserData 2>/dev/null | wc -l)
          printf "   %-22s %s entries\n" "/data/UserData" "$n"
          [ "$n" -gt 0 ]' || die "/data/UserData is empty after conversion — stop and investigate"
fi

# ===========================================================================
# STAGE B — PoundHard itself
# ===========================================================================
$SSH 'test -e /dev/ablspi0.0' 2>/dev/null \
  || die "/dev/ablspi0.0 is missing — ablspi did not bind. Check config.txt overlays."
step "ablspi present"

say "device packages"
$SSH 'export DEBIAN_FRONTEND=noninteractive
      need=""
      command -v node    >/dev/null 2>&1 || need="$need nodejs"
      command -v gcc     >/dev/null 2>&1 || need="$need gcc libc6-dev libjack-jackd2-dev"
      python3 -c "import jack" 2>/dev/null || need="$need python3-jack-client"
      command -v jack_lsp >/dev/null 2>&1 || need="$need jack-example-tools"
      command -v dnsmasq  >/dev/null 2>&1 || need="$need dnsmasq-base"
      if [ -n "$need" ]; then
          echo "   installing:$need"
          apt-get update -qq >/dev/null 2>&1
          apt-get install -y --no-install-recommends --no-upgrade $need >/dev/null 2>&1
      else
          echo "   all present"
      fi' || die "package install failed"

say "native JACK driver"
# jack_move.so owns /dev/ablspi0.0 -- audio, the screen, the pads, every LED. It
# is the one component PoundHard cannot ship: built from Ableton's own move-spi
# source, distributed inside the RNBO runtime, carrying no licence marking. It
# lives in PoundHard's tree so that RNBO -- a 153MB third-party takeover that has
# nothing to do with this instrument -- can be deleted and never reinstalled.
$SSH "mkdir -p $PH/lib/jack
      if [ -f $PH/lib/jack/jack_move.so ]; then
          echo '   already in PoundHard'\''s tree'
      elif [ -f /data/UserData/rnbo/lib/jack/jack_move.so ]; then
          cp -a /data/UserData/rnbo/lib/jack/jack_move.so $PH/lib/jack/jack_move.so
          echo '   migrated out of the RNBO tree — RNBO can now be removed'
      else
          echo '   MISSING' >&2; exit 1
      fi" || die "jack_move.so is nowhere on this device.
   It is the native Move JACK driver and ships inside the RNBO runtime. Install
   RNBO on the Move once to obtain it; it can be deleted immediately afterwards."

say "PoundHard controller + engine"
$SSH "mkdir -p $PH/controller $PH/controller/vendor $PH/sc $PH/logs $PH/ipc"
tar $TARFLAGS -C "$HERE/controller" -czf - poundhard | $SSH "tar -C $PH/controller -xzf -"
step "controller"
tar $TARFLAGS -C "$HERE/controller/vendor" -czf - pythonosc | $SSH "tar -C $PH/controller/vendor -xzf -"
step "vendored python-osc"
tar $TARFLAGS -C "$HERE/supercollider" -czf - boot.scd engine.scd synthdefs.scd | $SSH "tar -C $PH/sc -xzf -"
tar $TARFLAGS -C "$HERE/move/sc" -czf - ph-boot.scd | $SSH "tar -C $PH/sc -xzf -"
step "SuperCollider engine"
for f in run-stack.sh run-engine.sh run-csound.sh run-controller.sh stop-stack.sh; do
    if [ -f "$HERE/move/$f" ]; then $SCP "$HERE/move/$f" "root@${HOST}:$PH/$f"; fi
done
$SSH "chmod +x $PH/*.sh"
step "launch scripts"

say "appliance UI"
$SSH "mkdir -p $MOD"
$SCP "$HERE/move/schwung-module/poundhard/ui.js" "root@${HOST}:$MOD/ui.js"
if [ -f "$HERE/move/schwung-module/poundhard/module.json" ]; then
    $SCP "$HERE/move/schwung-module/poundhard/module.json" "root@${HOST}:$MOD/module.json"
fi
step "ui.js + module.json"

say "native stack"
$SSH 'mkdir -p /opt/phhost /opt/move-launcher'
$SCP "$HERE/armbian/phhost/phhost.mjs" "$HERE/armbian/phhost/fonts.mjs" "root@${HOST}:/opt/phhost/"
step "phhost (Node host for ui.js)"
$SCP "$HERE/armbian/phgain/phgain.c" "root@${HOST}:/opt/phhost/phgain.c"
$SSH 'cd /opt/phhost && gcc -O2 -Wall -o phgain phgain.c -ljack -lpthread 2>&1 | head -5; test -x /opt/phhost/phgain' \
  || die "phgain did not compile on the device"
step "phgain compiled"
$SCP "$HERE/armbian/launcher/launcher.py" "$HERE/armbian/launcher/movedisp.py" "root@${HOST}:/opt/move-launcher/"
step "appliance launcher"
$SCP "$HERE"/armbian/sbin/* "root@${HOST}:/usr/local/sbin/"
$SSH 'chmod +x /usr/local/sbin/move-rt-tune.sh /usr/local/sbin/move-jack-watchdog.sh \
              /usr/local/sbin/move-shutdown.sh /usr/local/sbin/boot-stock'
step "helper scripts"

say "services"
$SCP "$HERE"/armbian/systemd/* "root@${HOST}:/etc/systemd/system/"
$SSH 'systemctl daemon-reload
      # Ableton userspace must not claim the hardware.
      systemctl disable --now move-launcher.service move-web.service >/dev/null 2>&1 || true
      systemctl enable move-rt-tune.service jackd-move.service phgain.service \
                       move-launcher-menu.service >/dev/null 2>&1
      systemctl enable move-jack-watchdog.timer >/dev/null 2>&1'
step "enabled: rt-tune, jackd-move, phgain, launcher, watchdog"

say "starting"
$SSH 'systemctl restart move-rt-tune.service || true
      systemctl restart jackd-move.service; sleep 5
      systemctl restart phgain.service;     sleep 2
      systemctl restart move-launcher-menu.service; sleep 4
      systemctl start   move-jack-watchdog.timer || true'

say "verify"
$SSH 'fail=0
      for u in jackd-move phgain move-launcher-menu; do
          s=$(systemctl is-active $u 2>/dev/null)
          printf "   %-22s %s\n" "$u" "$s"
          [ "$s" = active ] || fail=1
      done
      p=$(env -u LD_LIBRARY_PATH timeout 15 jack_lsp 2>/dev/null | wc -l)
      printf "   %-22s %s\n" "jack ports" "$p"
      [ "$p" -gt 0 ] || fail=1
      env -u LD_LIBRARY_PATH timeout 10 jack_lsp 2>/dev/null | grep -q "^system:display" \
        && printf "   %-22s yes\n" "display port" || { printf "   %-22s NO\n" "display port"; fail=1; }
      exit $fail' \
  || die "post-install checks did not pass — see the lines above"

cat <<EOF

  Done. The appliance menu should be on the Move's screen.

    jogwheel      scroll        push = select
    SHUT DOWN     last entry, push twice to confirm
    master knob   volume (host-side, works in every appliance)

  Back inside PoundHard exits to the menu and tears the stack down.
  To return to stock AbletonOS for ONE boot:  ssh root@${HOST} /usr/local/sbin/boot-stock
EOF

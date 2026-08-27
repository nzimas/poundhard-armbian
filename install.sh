#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# PoundHard / Armbian — one-shot deploy of the whole stack.
#
#   ./install.sh [host]          default host: move.local
#
# Installs, in dependency order:
#   * PoundHard controller + SuperCollider engine + launch scripts
#   * the appliance UI (ui.js) the native host runs
#   * phgain    — realtime master-volume stage (compiled ON the device)
#   * phhost    — Node host that runs ui.js against the Move hardware
#   * launcher  — the appliance menu you see at boot
#   * systemd units, realtime tuning, and the JACK watchdog
#
# ASSUMES the Move is ALREADY running the Armbian image (see armbian/README.md).
# It does not flash anything and never touches the boot configuration.
# ---------------------------------------------------------------------------
set -euo pipefail

HOST="${1:-move.local}"
SSH="ssh -o ConnectTimeout=10 root@${HOST}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# macOS tags files with com.apple.provenance; GNU tar on the device warns about every
# one of them. Strip xattrs if the local tar supports it (bsdtar and GNU tar both do).
if tar --no-xattrs -cf /dev/null -T /dev/null 2>/dev/null; then TARFLAGS="--no-xattrs"; else TARFLAGS=""; fi
PH=/data/UserData/poundhard
MOD=/data/UserData/schwung/modules/overtake/poundhard

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
step() { printf '   -> %s\n' "$*"; }
die()  { printf '\n\033[31mFAILED: %s\033[0m\n' "$*" >&2; exit 1; }

say "target: root@${HOST}"
$SSH true 2>/dev/null || die "cannot reach root@${HOST} over ssh"
$SSH 'test -e /dev/ablspi0.0' 2>/dev/null \
  || die "/dev/ablspi0.0 is missing — the Move is not running the Armbian image, or ablspi did not bind"
step "reachable, ablspi present"

# --- 1. dependencies the device needs -------------------------------------
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

# --- 2. PoundHard itself ---------------------------------------------------
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
    [ -f "$HERE/move/$f" ] && scp -q "$HERE/move/$f" "root@${HOST}:$PH/$f"
done
$SSH "chmod +x $PH/*.sh"
step "launch scripts"

# --- 3. the appliance UI ---------------------------------------------------
say "appliance UI"
$SSH "mkdir -p $MOD"
scp -q "$HERE/move/schwung-module/poundhard/ui.js" "root@${HOST}:$MOD/ui.js"
[ -f "$HERE/move/schwung-module/poundhard/module.json" ] && \
  scp -q "$HERE/move/schwung-module/poundhard/module.json" "root@${HOST}:$MOD/module.json"
step "ui.js + module.json"

# --- 4. native host, gain stage, launcher ----------------------------------
say "native stack"
$SSH 'mkdir -p /opt/phhost /opt/move-launcher'
scp -q "$HERE/armbian/phhost/phhost.mjs" "$HERE/armbian/phhost/fonts.mjs" "root@${HOST}:/opt/phhost/"
step "phhost (Node host for ui.js)"
scp -q "$HERE/armbian/phgain/phgain.c" "root@${HOST}:/opt/phhost/phgain.c"
$SSH 'cd /opt/phhost && gcc -O2 -Wall -o phgain phgain.c -ljack -lpthread 2>&1 | head -5; test -x /opt/phhost/phgain' \
  || die "phgain did not compile on the device"
step "phgain compiled"
scp -q "$HERE/armbian/launcher/launcher.py" "$HERE/armbian/launcher/movedisp.py" "root@${HOST}:/opt/move-launcher/"
step "appliance launcher"
scp -q "$HERE"/armbian/sbin/* "root@${HOST}:/usr/local/sbin/"
$SSH 'chmod +x /usr/local/sbin/move-rt-tune.sh /usr/local/sbin/move-jack-watchdog.sh \
              /usr/local/sbin/move-shutdown.sh /usr/local/sbin/boot-stock'
step "helper scripts"

# --- 5. systemd ------------------------------------------------------------
say "services"
scp -q "$HERE"/armbian/systemd/* "root@${HOST}:/etc/systemd/system/"
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

# --- 6. verify -------------------------------------------------------------
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

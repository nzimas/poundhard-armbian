#!/bin/sh
# Recover a WEDGED jack server - one that keeps running, holds /dev/ablspi0.0 and
# satisfies Restart=always while its graph is dead (jack_lsp returns nothing).
#
# SAFETY FIRST. An earlier version of this restarted JACK during stack startup, because
# a BUSY server (supernova loading its 500-sample bank) cannot answer jack_lsp inside a
# short timeout and looks identical to a wedged one. That killed supernova mid-boot and
# broke every single PoundHard restart. So:
#   * generous timeout - a busy server gets time to answer
#   * 3 consecutive strikes, 30s apart - a wedge lasts, a load spike does not
#   * NEVER act while the stack is starting up (young engine processes) or under load
set -u
STATE=/run/move-jack-watchdog.strikes
TAG=move-jack-watchdog
[ -f "$STATE" ] || echo 0 > "$STATE"

clear_and_exit() { echo 0 > "$STATE"; exit 0; }

# jackd absent -> systemd's Restart=always owns that.
pgrep -x jackd >/dev/null 2>&1 || clear_and_exit

# NEVER touch a starting stack. Anything younger than 120s means a launch is in flight;
# that is exactly when a busy server is misread as a wedged one.
for proc in supernova scsynth sclang csound; do
    for pid in $(pgrep -x "$proc" 2>/dev/null); do
        et=$(ps -o etimes= -p "$pid" 2>/dev/null | tr -d " ")
        [ -n "${et:-}" ] || continue
        [ "$et" -lt 120 ] && clear_and_exit
    done
done

# High load means busy, not broken.
L=$(cut -d" " -f1 /proc/loadavg | cut -d. -f1)
[ "${L:-0}" -ge 3 ] && clear_and_exit

# Generous timeout: a healthy-but-loaded server still answers within this.
if env -u LD_LIBRARY_PATH timeout 20 jack_lsp 2>/dev/null | grep -q "^system:"; then
    clear_and_exit
fi

STRIKES=$(cat "$STATE" 2>/dev/null || echo 0)
STRIKES=$((STRIKES + 1))
echo "$STRIKES" > "$STATE"
logger -t "$TAG" "jackd running but no ports (strike $STRIKES/3)"

if [ "$STRIKES" -ge 3 ]; then
    logger -t "$TAG" "wedged for 3 checks with an idle stack - restarting the audio chain"
    echo 0 > "$STATE"
    systemctl restart jackd-move.service
fi
exit 0

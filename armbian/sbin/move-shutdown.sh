#!/bin/sh
# Graceful Move power-off, run DETACHED from the launcher.
#
# This cannot live inside the launcher: move-launcher-menu.service declares
# Requires=jackd-move.service, so stopping jackd stops the launcher as well -
# killing the process mid-sequence before it can power anything off.
#
# Ordering: MoveXmosPower opens /dev/ablspi0.0, which jackd holds exclusively.
# Free the device first, and bound every call so a wedge can never strand the
# unit with networking already down.
set -u
LOG=/var/log/move-shutdown.log
exec >>"$LOG" 2>&1
echo "=== $(date) move-shutdown starting ==="

systemctl stop move-launcher-menu.service phgain.service jackd-move.service
echo "services stopped; ablspi owner: $(fuser /dev/ablspi0.0 2>&1 || echo free)"

if [ -x /opt/move/MoveXmosPower ]; then
    echo "asking XMOS for the shutdown animation"
    timeout 15 /opt/move/MoveXmosPower --command shutdown
    echo "  MoveXmosPower rc=$?"
fi

echo "calling systemctl poweroff"
systemctl poweroff
echo "  poweroff returned $? (should not be reached)"

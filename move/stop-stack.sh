#!/bin/sh
# Tear the PoundHard stack DOWN completely. Run by the appliance UI on Back, so
# nothing survives into the next session.
#
# Clients are asked to leave BEFORE they are made to. supernova and csound are
# JACK clients: SIGKILL gives them no chance to unregister, and the server then
# refuses new client connections while it reaps the corpses — jackd alive and
# holding /dev/ablspi0.0, jack_lsp returning nothing, screen and pads dead. That
# is what "the system froze after I exited cleanly" was, every time. The JACK
# watchdog eventually recovers it by restarting jackd, but that is a ~90 second
# freeze the user should never have been shown.
#
# jackd itself is NEVER killed here. Under Armbian it runs the native `move`
# driver and owns the display, the pads and the jogwheel; the launcher draws
# through it. Killing it strands the device.

alive() {
    # -x: exact process name, so this never matches our own shell.
    for p in sclang scsynth supernova csound; do
        pgrep -x "$p" >/dev/null 2>&1 && return 0
    done
    return 1
}

# 0. Stop the LAUNCHERS before the things they launch, or they simply start them
#    again. run-stack.sh backgrounds a subshell that waits up to 120s for the
#    engine to report ready and then runs run-csound.sh, which retries four
#    times: exiting inside that window killed csound and got a fresh one a
#    moment later, still registered with JACK and holding 34 ports. Bracketed
#    patterns so these never match this script or its own shell.
#    A bracketed pattern stops the match hitting this file, but NOT a caller
#    whose own command line happens to mention the script — over ssh that means
#    killing the session you are working in. Skip our own pid and our parent.
kill_script() {
    for pid in $(pgrep -f "$1" 2>/dev/null); do
        [ "$pid" = "$$" ] && continue
        [ "$pid" = "$PPID" ] && continue
        kill -9 "$pid" 2>/dev/null
    done
}
kill_script "run-stac[k].sh"
kill_script "run-csoun[d].sh"
kill_script "run-engin[e].sh"

# 1. Ask. The controller first, so it stops talking to an engine that is leaving.
pkill -TERM -f poundhard.headless 2>/dev/null
# COMPASS's Lua interpreter is a CHILD of the controller and exits when its pipe
# closes — but only if the controller got to close it. Killed by name too.
pkill -TERM -f compass_host.lua 2>/dev/null
killall -TERM sclang scsynth supernova csound 2>/dev/null

# 2. Wait for them to actually go — this is the part that keeps JACK healthy.
#    Up to 5s; a clean supernova exit is well inside that.
i=0
while alive && [ "$i" -lt 50 ]; do
    sleep 0.1
    i=$((i + 1))
done

# 3. Insist, but only on what is left. A surviving supernova makes the next boot
#    attach to an orphan server (ready, zero nodes, no audio); a surviving csound
#    lingers dead-but-present and makes run-csound.sh skip starting a live one,
#    so the CSOUND engine comes back silent.
if alive; then
    killall -9 sclang scsynth supernova csound 2>/dev/null
    sleep 0.3
fi
pkill -9 -f poundhard.headless 2>/dev/null
pkill -9 -f compass_host.lua 2>/dev/null

rm -f /dev/shm/SuperColliderServer_* 2>/dev/null
rm -f /data/UserData/schwung/jack_running 2>/dev/null
rm -f /data/UserData/poundhard/ipc/*.json /data/UserData/poundhard/ipc/ui_hb.txt /dev/shm/poundhard/* 2>/dev/null
exit 0

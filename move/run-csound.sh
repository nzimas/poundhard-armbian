#!/bin/sh
# Bring up the PoundHard CSOUND engine (engine 20) as a JACK client.
#
# Csound is a SEPARATE PROCESS from the SC server, but it is not a separate instrument:
# it writes one stereo pair per track into supernova's input ports, and an SC voice reads
# that pair onto the track bus. So a Csound track goes through the per-track filter, the
# 8-slot FX chain, the living-FX sends, the mixer and the master exactly like every other
# engine — which is the whole reason for wiring it this way instead of letting Csound talk
# to the hardware.
#
# CHANNEL MAP. supernova boots with 36 inputs: 1-2 are the microphone (untouched), 3-34
# are the 16 Csound track pairs, and 35-36 are the AUDITION pair ("track 16") the palette
# pad plays through. Csound's own channels 1-2 are dead — its JACK module
# auto-connects the first two to the hardware playback ports and there is no flag to stop
# it, so they are left silent and the tracks start at channel 3.
#
#   csound output_3+2t / output_4+2t   ->   supernova input_3+2t / input_4+2t   (track t)
#
# The port PREFIX is set explicitly (-+jack_outportname): Csound's default prefix is
# `outport`, so the ports would be poundhard_cs:outport1.. and every connection by name
# would fail — which is exactly what happened before it was pinned.
#
# The connections are made explicitly with ph-jackconnect rather than left to Csound's
# auto-connect, which wires by port enumeration order and would silently mis-route all 32
# channels the day another client joins the graph.
set -e
PH=/data/UserData/poundhard
CS=$PH/csound
LOGS=$PH/logs; mkdir -p "$LOGS"
CSLOG=$LOGS/csound.log

# HOME MUST be set. systemd services get no HOME, and the launcher starts the stack as
# a service - so under the launcher this was empty. Csound's JACK client init uses $HOME
# (its Berkeley DB registry lives there) and SEGFAULTS without it, on every attempt. That
# is why engine 20 died on every launcher start yet always worked when started by hand
# from a login shell, which has HOME. run-engine.sh already does this; this did not.
export HOME="${HOME:-/data/UserData}"
export OPCODE6DIR64=$CS/plugins
export LD_LIBRARY_PATH=$CS/lib:$PH/lib
export PATH=$CS/bin:$PH/bin:$PATH

# already running? (idempotent — the stack launcher may be re-run)
# NEVER trust an existing instance. Csound loses its JACK client when jackd restarts but
# the process can linger, and a lingering one is worthless: it holds no ports and makes no
# sound. Treating it as "already running" is exactly how the engine came back silent after
# a restart, so any instance is replaced rather than adopted.
if pgrep -x csound >/dev/null 2>&1; then
    echo "[csound] replacing an existing instance"
    killall -9 csound 2>/dev/null
    # SIGKILL gives csound no chance to unregister its JACK client, so the name
    # 'poundhard_cs' lingers in the graph. Re-registering it before the server has
    # reaped the corpse makes the new process SEGFAULT on startup - which is exactly
    # how engine 20 came up silent. Wait for BOTH the process and the JACK client
    # name to actually disappear.
    i=0
    while [ $i -lt 40 ]; do
        still_proc=0; still_jack=0
        pgrep -x csound >/dev/null 2>&1 && still_proc=1
        timeout 2 jack_lsp 2>/dev/null | grep -q "^poundhard_cs:" && still_jack=1
        [ "$still_proc" = 0 ] && [ "$still_jack" = 0 ] && break
        i=$((i+1)); sleep 0.25
    done
    [ $i -ge 40 ] && echo "[csound] WARNING: 'poundhard_cs' still registered after 10s"
    sleep 1
fi

[ -f "$CS/orc/ph-engine.orc" ] || { echo "[csound] no orchestra at $CS/orc — not installed"; exit 1; }

# The orchestra is driven entirely over UDP ($-prefixed score events from sclang), so the
# score holds nothing but a 100-year-long dummy note to keep the performance alive.
cat > "$CS/orc/ph-run.sco" <<'SCO'
i999 0 3153600000
e
SCO

# Csound's JACK client registration is FRAGILE while the graph is busy: starting it
# while supernova is still loading its sample bank segfaults it outright, with nothing in
# the log but "Segmentation fault". It is not deterministic, so RETRY rather than leaving
# engine 20 silent for the whole session (which is exactly what used to happen).
attempt=1
CSPID=""
while [ $attempt -le 4 ]; do
    echo "[csound] starting (jack client 'poundhard_cs', UDP 11000) attempt $attempt"
    csound \
      -+rtaudio=jack -odac -+jack_client=poundhard_cs \
      -+jack_outportname=output_ \
      -b128 -B1024 --sample-rate=44100 --nchnls=36 \
      --port=11000 --nodisplays -d -m0 \
      "$CS/orc/ph-engine.orc" "$CS/orc/ph-run.sco" </dev/null > "$CSLOG" 2>&1 &
    CSPID=$!
    ok=0; i=0
    while [ $i -lt 40 ]; do
        if grep -q "UDP server started" "$CSLOG" 2>/dev/null; then ok=1; break; fi
        kill -0 "$CSPID" 2>/dev/null || break
        i=$((i+1)); sleep 0.25
    done
    # NOTE: this script runs under `set -e`. A bare `[ ... ] && break` returns
    # non-zero when the test fails and ABORTS the whole script - which silently
    # killed the retry loop after attempt 1. Use an explicit if.
    if [ "$ok" = 1 ]; then break; fi
    echo "[csound] attempt $attempt failed:"; tail -n 3 "$CSLOG"
    kill -9 "$CSPID" 2>/dev/null || true
    CSPID=""
    j=0
    while [ $j -lt 20 ]; do
        gone=1
        if pgrep -x csound >/dev/null 2>&1; then gone=0; fi
        if timeout 2 jack_lsp 2>/dev/null | grep -q "^poundhard_cs:"; then gone=0; fi
        if [ "$gone" = 1 ]; then break; fi
        j=$((j+1)); sleep 0.25
    done
    sleep 2
    attempt=$((attempt + 1))
done
if [ -z "$CSPID" ]; then
    echo "[csound] FAILED after 4 attempts - engine 20 silent"
    exit 1
fi
echo "[csound] pid=$CSPID (log: $CSLOG)"
# 32 channels: csound output_3..34 -> supernova input_3..34
if "$CS/bin/ph-jackconnect" poundhard_cs 3 supernova 3 34; then
    echo "[csound] track pairs + audition wired into supernova inputs 3-36"
else
    # A partial wiring is worse than none: some tracks sound and others are mysteriously
    # silent. Retry once — the usual cause is connecting before supernova's ports settle.
    echo "[csound] connect failed, retrying once"
    sleep 2
    "$CS/bin/ph-jackconnect" poundhard_cs 3 supernova 3 34 \
        && echo "[csound] track pairs wired on retry" \
        || echo "[csound] WARNING: engine 20 will be silent — connections failed"
fi

# ---- realtime placement -----------------------------------------------------------
# Csound and supernova are in the SAME JACK cycle, and Csound feeds supernova: its audio
# has to be written before supernova's DSP threads read it. It came up at priority 65 —
# exactly the DSP threads' priority — and unpinned across all four cores, so it competed
# with the very threads waiting on it and migrated between them mid-callback. That is
# where the XRuns came from.
#
# Priority 68 puts it above the DSP threads (65) and below jackd (70), which is the order
# the graph actually needs. Affinity goes on the RT THREAD, not the process: setting it on
# the pid before the callback thread exists does not stick (the same gotcha run-engine.sh
# documents for supernova). Core 3 is where it already ran and is the least loaded — the
# DSP threads own cores 0-2, one each.
# chrt and taskset both refuse to touch a SCHED_FIFO thread without CAP_SYS_NICE, and the
# stack runs as `ableton`. ph-rtsched carries that capability (see build-csound.sh).
sleep 1
"$CS/bin/ph-rtsched" "$CSPID" 68 3 || echo "[csound] WARNING: could not place RT threads"
echo "[csound] up"

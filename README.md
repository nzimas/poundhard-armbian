<p align="center">
  <img src="web/poundhard-logo.svg" alt="PoundHard" width="560">
</p>

# PoundHard

**A 16-track groovebox for the Ableton Move** — built for edgy IDM,
rhythmic noise and percussion-centric experimental electronica.

This is the **Armbian edition**. PoundHard no longer runs *on top of* Ableton's
software as a Schwung takeover — it runs on **Armbian**, on the bare machine, with
Ableton's stack gone entirely. The Move boots into a mainline PREEMPT_RT kernel, a
native JACK server owns the hardware directly, and an **appliance menu** on the screen
launches PoundHard from a jogwheel click. Nothing of Ableton's remains in the boot path.

> The previous, Schwung-based edition lives on unchanged at
> [nzimas/poundhard](https://github.com/nzimas/poundhard) and is no longer updated.

A SuperCollider engine carries the DSP, a Python controller holds the authoritative
musical state, and `ui.js` — **the same file, unmodified** — drives the Move's pads, step
buttons, encoders and screen. What changed underneath it is the host: where Schwung used
to provide the drawing and MIDI primitives, PoundHard now ships its own.

```
 Move pads / buttons / knobs / screen
        │  ▲
        ▼  │  jackd -d move   ← native driver, owns /dev/ablspi0.0
        │  │  (display frames + MIDI, no Ableton software anywhere)
        ▼  │
   phhost  (node — implements the ui.js host API: text, rects, LEDs, file I/O)
        │  ▲
        ▼  │  (ui.js — unchanged from the Schwung edition)
   ipc/control.json   ▲ ipc/status.json
        │             │
        ▼   (file bridge, polled)
   controller  (python — poundhard.headless, authoritative Project state)
        │  ▲
        ▼  │   OSC  /ph/…  →  ← /ph/step /ph/cpu /ph/cycle
   engine  (sclang — 20 engines × 16 tracks + TempoClock step sequencer + FX chains)
           running on SUPERNOVA (multicore SC server; ParGroups spread tracks over cores)
        │
        ▼
   supernova ─┐
   csound   ──┼→ jackd (native) → phgain (master volume) → Move speaker / output
              ┘
```

---

## 📖 [Read the User Guide →](docs/USER-GUIDE.md)

**The [User Guide](docs/USER-GUIDE.md) is both a tutorial and the manual.** It opens with
*Your first pattern* — twenty minutes from an empty machine to a track that plays itself —
and then documents every view, control, engine and modifier in full.

This README covers what PoundHard *is*, how to get it onto the device, and how it works
inside. For how to **play** it, go to the guide.

---

## Contents

- [What it is](#what-it-is)
- [The engines](#the-engines)
- [Install](#install)
- [The system underneath](#the-system-underneath)
- [Develop off-device](#develop-off-device)
- [Architecture & internals](#architecture--internals)
- [Wire protocols](#wire-protocols)
- [Repository layout](#repository-layout)
- [Gotchas](#gotchas)
- [License & disclaimer](#license--disclaimer)

---

## What it is

- **16 tracks**, one per step button. Tracks start **empty**; you build your rig by
  assigning engines from the **engine palette**. Any engine can go on any track, and the
  assignment is **per pattern** — two patterns can carry completely different rigs.
- **20 assignable engines**, from digital drums and 6-operator FM through Mutable-style
  resonators and macro-oscillators to chaotic maps, bytebeat, wavetables, a sampler that
  mangles its captures through Csound, a realtime **Csound** synth with 26 architectures, and
  **JOLT**, a procedural breakbeat engine that rebuilds a real break every bar and never drifts.
- **A 16-step sequencer per track**, each with its own length and clock rate
  (**polymeter**), plus a per-step **cycle frequency** so a step can fire once every 2–8
  repetitions.
- **Per-step locks** on pitch, velocity, pan, a voice macro, the FX chain and — on SAMPLE
  tracks — the slice of the buffer a step plays.
- **A multimode filter on every track** that keeps its bass and its level as resonance rises.
- **Pattern generation with eighteen compositional recipes** — `SPARSE`, `WALL`,
  `POLYMETER`, `GLITCH`, `PROCESSION`, `INTERLOCK` and more. Each is a brief covering roles,
  density, register, accent shape, pan, pitch relationships and variation over time, and the
  generator **scores its own output** and repairs the weakest track before handing it over.
- **Eight mastering chains** on one continuum from restrained to destroyed — EQ, broadband
  and multiband compression, gain-compensated saturation, soft and hard clipping and a
  limiter, with the active chain's own parameters on the eight knobs. Measured +9.5 dB of
  loudness and 7.5 dB of crest-factor reduction across the eight, and switching is a 120 ms
  glide along one chain rather than a graph swap, so it cannot click.
- **32 auto-assigned, tempo-synced LFOs** in the modulation view — sample-and-hold and sine,
  each on a unique parameter, never on engine pitch, and completely non-destructive.
- **Seven non-destructive performance modifiers** — **HEAT**, **SHUFFLE**, **QUAKE**,
  **CHURN**, **BREAK**, **STROBE** and **WHIM**. None of them edits a pattern: every one is an overlay
  the engine plays instead, so a single sequence can evolve all night and switching them off
  gives you back exactly what you programmed.
- **Living steps** — mark steps and they **transform themselves** as you play: ratchets,
  timbre lurches, pitch leaps, pan throws and per-step delay/reverb.
- **Patterns are self-contained** — engines, every parameter, FX, mutes and sequences — and
  organised as a hierarchy: **16 pattern seeds**, each with its own row of **16 expansions**
  for developing variations on one idea without risking the original. Projects save to disk
  with an autosave recovery file.

The step buttons for tracks that contain events **pulse at the pace of their sequence**;
assigned-but-empty tracks glow steady-dim in their engine hue, and unassigned tracks are
dark — so you can read the whole rig at a glance.

> Full detail on all of the above, plus how to actually drive it, is in the
> **[User Guide](docs/USER-GUIDE.md)**.

---

## The engines

| Pad | Engine | Colour | Character |
|--------|--------|--------|-----------|
| 1 | **DRUM** | 🟡 yellow | digital drum — kick/snare/hat/metal/clap/tom/noise |
| 2 | **FM7** | 🟢 green | real 6-operator FM — bells / e-pianos / clangs / FM bass / stabs |
| 3 | **BUCHLOID** | 🟣 magenta | Buchla complex osc — drone / noise texture |
| 4 | **MOLLY** | 🔵 blue | gritty Moog-ladder subtractive lead/pad |
| 5 | **RINGS** | 🩵 cyan | Mutable Rings modal / sympathetic resonator |
| 6 | **BEN** | 🟠 orange | Benjolin — chaotic generative machine |
| 7 | **NOIZEOP** | 🩷 pink | 4-sine / 6-algorithm glitch-noise machine |
| 8 | **ICARUS** | 🟪 violet | dreamcrusher drone / pad (VarSaw + FB delay) |
| 9 | **PLAITS** | 🟩 lime | Mutable Plaits — 16-model macro-oscillator |
| 10 | **SHAKER** | 🟨 amber | STK Shakers — 23 shaker/scraper models |
| 11 | **MEMBRANE** | 🟥 warm red | struck 2D-waveguide membrane — tunable drums / frame drums / gongs |
| 12 | **MALLET** | 🟡 gold | STK ModalBar — marimba / vibraphone / agogo / wood / bells |
| 13 | **BOWED** | 🟦 teal | STK BandedWG — bowed/struck metal bars, glass harmonica, Tibetan bowl |
| 14 | **PLUCK** | 🟩 spring | Waveguide voice — `mode` picks **pluck** (DWG stiff string: koto / clav / harp / muted) or **tube** (TwoTube: hollow formant / reedy) |
| 15 | **CHAOS** | 🟥 red | chaotic-map oscillator — FBSine / Latoocarfian / Henon / Standard / Cusp |
| 16 | **WTABLE** | 🟪 violet | Ableton Wavetable rebuild over the Move's own factory sprites |
| 17 | **BYTEBEAT** | 🟢 green | ByteBeat UGen — 8-bit algorithmic expressions evaluated at audio rate |
| 18 | **SAMPLE** | 🌹 rose | capture engine — records another engine, mangles it through a **Csound** opcode graph, plays it back |
| 19 | **CSOUND** | 🩵 turquoise | realtime **Csound** macro-synth — 26 architectures (chained generator cores and shapers) |
| 20 | **JOLT** | 🟥 rust | procedural breakbeat — slices real breaks and rearranges them, eight levels from straight to ruptured, with continuous mutation and automation that leaves a chosen base level for exactly one bar and returns |

PLUCK and TUBE were **merged into one engine** (pad 14): they were two ends of the same
waveguide, so `mode` now picks between them and the palette is 20 pads instead of 21.
Projects saved with the old separate TUBE engine still load — the type is remapped onto
PLUCK with `mode = tube`.

Each engine's parameters, character and per-model detail are documented in the
[User Guide → Sound engines](docs/USER-GUIDE.md#sound-engines).

---

## Recording & the web UI

The [recorder view](docs/USER-GUIDE.md#recorder-view) captures the master output (post-limiter, what
you hear) to **stereo 16-bit WAV** via a `DiskOut` synth in the engine, capped at
**7 minutes** per take, into `/data/UserData/poundhard/recordings/`.

Finishing a take enters a **tail** phase: the engine keeps writing while it reports
the master level to the controller (`/ph/amp`, ~10 Hz), and the file is only closed
once the signal has stayed below the silence threshold for a beat — so reverb and
delay tails are preserved. Tune it with `PH_REC_SILENCE` (default `0.004`; music
typically sits around `0.1–0.4`).

The controller runs a small **web UI** at **`http://move.local:7177`** where every
recording has a **▶ Play** button (audition in the browser) and a **Download**
button. The address is deliberately a general
PoundHard endpoint — more functions will live there over time. The port is
configurable via the `PH_WEB_PORT` environment variable.

---

---

## Install

**Prerequisite: the Move must already be running Armbian** with the native `move` JACK
driver present (`/dev/ablspi0.0` and `jack_move.so`). Getting there is a one-time
operation described in [The system underneath](#the-system-underneath); it replaces
Ableton's software on the device and is not reversible without the stock SD image.

Once the machine is on Armbian, the entire stack — engine runtime, Csound, CDP,
controller, UI host, master-volume client, appliance menu and all systemd units —
installs with **one command from your workstation**:

```bash
./install.sh [move-host]     # default host: move.local
```

That is the whole deployment. It is idempotent — run it again after any change and it
redeploys and restarts what it needs. It takes a couple of minutes on first run (it
compiles the volume client on the device) and prints a verification block at the end:

```
== verify
   jackd-move             active
   phgain                 active
   move-launcher-menu     active
   jack ports             16
   display port           yes
```

If any line reads anything other than `active` / `16` / `yes`, the install did not
finish and the script says which stage failed rather than leaving you to find out from
a blank screen.

**What it does, in order:**

1. **Checks the target** — SSH reachability and the presence of `/dev/ablspi0.0`. It
   refuses to run against a device that isn't the Move on Armbian, rather than half-installing.
2. **Installs device packages** — `nodejs`, `gcc`/`libjack-dev` (to build the volume
   client), `python3-jack-client`, `jack-example-tools`, `dnsmasq-base`.
3. **Deploys PoundHard** — the self-contained SC runtime bundle (supernova, scsynth,
   sclang, every UGen plugin, the class library), the Csound runtime, CDP, the Python
   controller with vendored `python-osc`, the `.scd` engine files and the `run-*.sh`
   launch scripts. RT file capabilities are re-asserted afterwards, because `chown`
   clears them.
4. **Deploys the UI** — `ui.js` and `module.json` unchanged, plus **phhost**, the Node
   host that implements the API `ui.js` expects.
5. **Compiles `phgain` on the device** — the JACK client that puts the Move's master
   volume knob in front of the speaker.
6. **Installs the launcher and systemd units** — the appliance menu, the shutdown
   helper, the RT tuner and the JACK watchdog.
7. **Disables Ableton's leftovers** (`move-launcher.service`, `move-web.service`) and
   enables the new chain.
8. **Verifies** — services active, JACK ports present, display port live.

> After a controller change, exit PoundHard and re-enter it. The launcher reuses a
> controller process that is already running, so an old one from a prior session
> otherwise survives the redeploy.

---

## The system underneath

This edition replaces Ableton's software with a mainline Linux system. The parts below
are what the Schwung edition used to get for free, and what PoundHard now provides itself.

### Boot

Armbian **trixie**, kernel **6.18.46-current-bcm2711 PREEMPT_RT**, arm64. The Move is a
Compute Module 4 **Lite** — no eMMC — so everything lives on the SD card, and the
firmware loads the Armbian kernel **directly** from `config.txt`. There is no
bootloader stage in between.

> **u-boot was tried and abandoned.** Partition 4 (`/data`, 54 GB) is formatted with
> ext4 **`meta_bg`**, and u-boot's ext4 driver cannot read past the first meta block
> group. Loading the kernel through u-boot therefore worked only from the small root
> partition, and every route around it cost more than it bought. The firmware reads
> the kernel itself now.

**The escape hatch is `tryboot`.** `reboot '0 tryboot'` boots `tryboot.txt` — the stock
Ableton configuration — exactly once, without touching the normal boot path. That is
the way back to stock if something goes wrong, and it needs no card removal, which
matters: this instrument is built for a user with a severe sight impairment, and
"just pop the SD card out" is not a recovery procedure.

### Audio and control: `jackd -d move`

The native driver `jack_move.so` (GPL, from the RNBO tree) owns `/dev/ablspi0.0` and is
the single point of contact with the hardware. Started as `jackd -R -P 70 -d move`, it
exposes not just audio but **the whole control surface** as JACK ports:

| Port | Carries |
|------|---------|
| `system:playback_1/2` | speaker / line out |
| `system:display` | the 128×64 screen, one frame per MIDI event |
| `system:midi_capture` | pads, buttons, encoders, jogwheel |
| `system:midi_playback` | pad / button / step LEDs |

There is no ALSA path and no second audio server. Measured **0 xruns** in normal use.

**MIDI to the device must be on channel 16.** `JackMoveDriver::provideMidi` accepts note
and CC messages on channel 16 and silently drops everything else. Schwung sent on
channel 1, so every LED write from `ui.js` had to be re-nibbled by the host — which is
why the pads were dark for an afternoon before the driver's disassembly explained it.

**The display is a 1024-byte SSD1306-paged framebuffer** delivered as one JACK MIDI
event prefixed with the magic `MOVEDISP`: 128×64 at 1 bpp, `byte = (y >> 3) * 128 + x`,
`bit = 1 << (y & 7)`, double-buffered.

### The UI host: `phhost`

`ui.js` is unchanged from the Schwung edition, which means something has to supply the
API it calls. `phhost` (Node) implements it: `clear_screen`, `fill_rect`, `draw_rect`,
`print`, `text_width`, `host_read_file`, `host_write_file`, `host_system_cmd`,
`host_set_refresh_rate`, `host_exit_module` and `move_midi_internal_send`, plus the
`init` / `tick` / `onMidiMessage*` entry points.

Two fonts ship with it, and they are **different fonts, not one scaled**: a 5×7 at
scale 1 (advance 6) and **Tamzen 8×16** at scale 2 (advance 8). `print` is a genuine
text primitive — an early version of the host implemented it as *logging*, which sent
the entire user interface to stderr and drew a blank screen.

Control mappings come from the Move's own `constants.mjs` rather than from observation:
`MoveMainKnob = 14` (relative, 1–63 up / 64–127 down), `MoveMainButton = 3`,
`MoveMaster = 79`, `MoveShift = 49`, `MoveBack = 51`, `MovePlay = 85`, `MoveRec = 86`.

### Master volume: `phgain`

The Move's master knob is a MIDI control, not a mixer, so with Ableton gone nothing was
listening to it. `phgain` is a small JACK client that sits between the engines and
`system:playback_*` and applies the knob's value as a gain — written in C and compiled
**on the device** at install time, so it links against the same `libjack` the running
server speaks.

### The appliance menu

`move-launcher-menu.service` draws a scrollable list on the screen at boot. Scroll with
the jogwheel, **push to launch**. PoundHard is the first entry; **SHUT DOWN** is the
last.

Shutdown is a menu entry rather than a power-button gesture because the Move's power
button is **not wired to a GPIO** the kernel can see — there is nothing to bind a
short-press to. The confirmation-then-jogwheel flow of the stock system is reproduced
in the menu instead. It runs detached (`/usr/local/sbin/move-shutdown.sh`): the launcher
`Requires=jackd-move`, so a shutdown that stopped JACK first would kill the very process
running the shutdown, halfway through.

### Staying reachable

There is no Ethernet — `eth0` exists but has never had a carrier — so **Wi-Fi is the
lifeline** and is brought up before anything else, with a USB-C network fallback behind
it. `move-jack-watchdog` restarts JACK if it wedges, but only under guards learned the
hard way: it skips if any engine process is under 120 s old, skips if load average is
≥ 3, times out at 20 s, and needs three consecutive strikes 30 s apart before acting.
The first version of that watchdog had none of those guards and killed supernova on
every stack start for an hour.

### `armbian/` in this repo

Everything above is version-controlled under `armbian/` — the launcher and its display
library, the UI host and its fonts, the volume client's source, all systemd units, the
sbin helpers (RT tuning, watchdog, shutdown, `boot-stock`) and the boot-time files
(`config.txt`, cmdline, the spidev overlay). `install.sh` deploys from there.

---

## Develop off-device

The controller runs headless with no engine (OSC sends become no-ops), so kit
generation, pattern/project logic and the control/status protocol can be
exercised on any machine:

```bash
cd controller
PYTHONPATH="$PWD:$PWD/vendor" python3 -m poundhard.headless
# writes status.json, polls control.json (paths from $PH_SHARE)
```

---

---

## Architecture & internals

**The controller is authoritative** for musical state (a `Project`: 16 tracks ×
{engine type, note, velocity, parameters, pattern + per-step locks — pitch, velocity,
pan, voice macro, ratchet, living flag/period, FX mask, **cycle divider**, the **per-step
sample window**, the **per-step FX amount** and the **per-step filter** — mute, length, rate, **filter**}, plus FX assignment/bypass/macros, tempo, and the pattern bank — 16 seeds, each with a
lazily-allocated row of 16 expansions). A track
is at most **16 steps**; the per-step arrays are 32 wide for headroom and for projects
saved before the cap. It reads `control.json`, writes `status.json`, generates kits,
and pushes state to the engine over OSC.

**Modulation runs in the controller, not the engine.** `/ph/param` writes the engine's
per-track parameter store *and* sets the value on any ringing voice, but never touches the
controller's `Project` — so the 32-LFO bank can drive parameters continuously while nothing
is stored, and switching an LFO off simply re-sends the programmed value. Phase comes from
the bar position (`bars × cycles_per_bar`), resynced on the engine's own `/ph/cycle`, so
nothing free-runs and a tempo change carries the whole bank. Measured on the device, all 32
LFOs active cost **+0.6% DSP** — they send parameter messages and spawn no synths.

**Startup is a handshake, not a race.** The controller pings until the engine answers
`/ph/ready`, and until then it dispatches nothing — including whatever was left in
`control.json` by the previous session, whose high-water mark it adopts on its first
read rather than replaying. Pushing a machine's worth of state at a half-built graph
floods the server with messages for nodes that do not exist yet and can leave it
running-but-silent, which is a failure mode worth designing out rather than debugging
twice.

**The engine owns the step clock and the DSP.** The clock is a `TempoClock`
routine in `engine.scd`: it advances a per-track accumulator (so each track runs
at its own rate and length — polymeter), counts each track's repetitions so a step
carrying a **cycle divider** only fires on every Nth pass, spawns each active/unmuted
step's voice, streams the playhead back as `/ph/step`, and fires `/ph/cycle` on each
16-step bar boundary for queued pattern switching. Python stays at a relaxed rate for
UI/status only.

### Voice model

Voices are **spawned per hit, not persistent.** Each active/unmuted step spawns a
fresh one-shot synth from the track's stored params; it plays its envelope and
frees itself (`Line.kr … doneAction:2`). Persistent always-on voices were the
first design and **froze the Move** — 16 always-on synths overloaded the ARM even
at idle. Two guards keep it robust under dense IDM/noise patterns:

- **Per-track polyphony cap** (`~maxPoly = 3`, steal oldest) — without it dense
  patterns spawn faster than voices free, growing nodes unbounded until a freeze.
- **Per-mode DRUM defs** (`phDrumKick … phDrumNoise`, picked by the track's
  `mode`) — a hit runs only its mode's DSP, several times cheaper than an
  all-modes-then-`Select` voice.

**BYTEBEAT and CSOUND are the two exceptions.** BYTEBEAT's is forced by the UGen rather
than chosen: it
parses its expression *per instance* and starts on an `Undefined` expression that
evaluates to 0, so a freshly spawned instance is silent until its asynchronous `/eval`
lands. Spawning one per hit raced the parse against the note — long notes won it, short
ones came out inaudible, and the same sound was not reproducible twice. That track keeps
**one live voice**, parsed once and re-triggered per step (`t_trig`, `doneAction: 0`),
with a free-running counter — which is what bytebeat is anyway.

**CSOUND's** is structural: its audio is generated by a separate process and arrives over
JACK, so there is nothing to spawn per hit. Its SC voice is a permanent carrier reading
that track's input pair onto the track bus, and a hit is a score event sent to Csound. The
carrier is deliberately left alone once running — setting gain on it, or moving it between
buses for a per-step send, is heard as a step in the middle of whatever is still ringing,
including a reverb tail fed from it.

Each track has a **private stereo bus**; its voices write there, its FX chain
processes in place (each FX `ReplaceOut`s the bus in canonical order), and a send
sums it to the master. Node order: `gClear → gVoices → gFilt → gFx → gSend → gMaster`
(`gFilt` is the per-track multimode filter, one always-on insert per track).
Under supernova `gVoices` is a **ParGroup** with a serial subgroup per track, so tracks
render in parallel while each track's own chain stays ordered.

### The Move UI (ui.js) and file I/O

ui.js can't open sockets, so everything crosses the `ipc/{control,status}.json`
file bridge. The host's file I/O is **synchronous and can stall the frame**, so
the UI reads/writes as little as possible (change-detected status writes, reads
~5 Hz, coalesced control writes) and redraws only on visible change. Big values
use a **custom block-glyph renderer** (`drawBig` + `FONT`) because the host
`print` maxes at size 2 — the instrument is built for a user with a severe sight
impairment, so param / rate / macro / tempo readouts are drawn large and stay up
while a knob is touched.

---

---

## Wire protocols

### control.json (ui.js → controller)

A `cmds` queue de-duped by `seq` (a single-slot mailbox lost commands when the UI wrote
twice between polls). The queue left behind by a previous session is **never replayed** —
on its first read the controller takes the high-water mark and runs nothing — and no
command is dispatched until the engine reports ready.

| Group | Commands |
|---|---|
| engine palette | `audition`, `palettegen`, `assign`, `randtrack`, `genkit`, `drumaudition` / `drummode` (DRUM type picker), `smparm` (arm the SAMPLE capture) |
| tracks | `mute`, `solo`, `trackset` (pitch/amp/pan/rate), `voicemacro`, `voiceparam` (one named voice param — SAMPLE's window knobs), `trackfilter` (cutoff/res/type), `note`, `setlen`, `clearpat` |
| steps | `stepset` / `steptoggle`, `steplock`, `stepmacro`, `stepfx` (per-step FX mask), `stepfxamt` (per-step FX wet), `stepcycle` (fire every Nth repetition), `stepwindow` (per-step sample slice), `stepfilter` (per-step filter lock), `marklive` / `liveperiod` (living steps — the period is in PLAYS of the step, 1-8) |
| clipboard | `stepcopy` / `steppaste`, `rowcopy` / `rowpaste`, `trackcopy` (the Copy-button gestures) |
| generation | `stepgen` (a new sequence for one track, scale-aware), `joltpad` / `joltbreak` / `joltinit` / `joltauto` / `joltrate` / `joltmut` (JOLT's break variations, automation and continuous mutation) |
| performance | `heat`, `shuffle`, `quake`, `churn`, `break` + `breakint`, `strobe`, `whim` (the seven temporary overlays) |
| mastering | `mastprofile` (pick one of eight chains, or bypass), `mastknob` (one parameter of the active chain) |
| randomizers | `steprand` (toggle one per-step parameter's randomizer), `randdebug` |
| transpose | `transpose` (one track's sequence), `transposeall` (project-wide, the cursor keys) |
| FX | `fxassign`, `fxbypass`, `fxmacro`, `fxwet` |
| modulation | `lfoenter` (assign/refresh the bank), `lfopad` (toggle one LFO), `lfogen` (re-roll all 32) |
| macros | `heat` / `heatpct`, `shuffle`, `quake`, `churn`, `break`, `chaos` / `chaosreset` |
| patterns & projects | `savepat` / `loadpat`, `patdel`, `patcopy` / `patpaste` / `patclipclear`, `expenter` / `expfirst` (open a seed's expansions), `genvar`, `randpat`, `saveproj` / `loadproj`, `loadauto` |
| transport & system | `run`, `editenter` / `editexit`, `recpad`, `undo`, `panic` |

`tempo` is a continuous field applied on change, not a queued command.

### status.json (controller → ui.js)

Carries `ready / engine / cpu / nodes / running / tempo / step / editTrack / solo / kit /
webPort`, per-track `muted / active / note / vel / pan / amp / rate / length` plus
`start / end` (SAMPLE's playable window) and `fcut / fres / ftype` (the track filter), the engine `types` / role `names` and
`drumTracks / drumMode`, the FX view state (`fxTop / fxBypass / fxOn / fxMacro / fxWet /
fxNames`), and the open track's `edit` block: `steps`, the effective per-step
`stepNote / stepVel / stepPan / stepMacro`, `living / period / ratchet / active`,
`fx` (per-step FX masks), `cycle` (per-step dividers), `stepStart / stepEnd` (the effective per-step sample window)
and `stepFcut / stepFres / stepFtype` (the effective per-step filter), plus the project's
`scale` (`{root, name}`, or null until something pitched establishes it).

Also the pattern bank, which is a hierarchy rather than a flat 32: `patFilled` is the
**16 seeds**, `expFilled` the open seed's **16 expansions**, `expSeed` which seed's row is
open (-1 = none) and `expCur` which expansion is live (-1 = the seed itself), alongside
`patCur / patPending / projFilled`. Then the `autoSave` flag, `projCur` (which project is
loaded) and `canUndo / canRedo`, the seven performance modifiers (`heat / heatPct / shuffle /
quake / churn / brk / brkEvery / brkNow / strobe / whim`), the modulation bank (`lfo`, 32
per-pad states of 0 none / 1 assigned / 2 active, and `lfoOn`), the mastering chain
(`mast` / `mastName` / `mastKnobs` / `mastPos`) and the chaos macro (`chaos`), the SAMPLE capture state (`smpState / smpSrc / smpChain`), the recorder
(`recState / recSlot / recSlots / recElapsed / recAmp`), and `clipStep / clipRow` — whether
the Copy-gesture clipboard is holding a step or a row. The edit block also carries `rand`,
the list of per-step randomizers live on the open track.

### OSC (controller → engine, sclang langPort 57120)

`/ph/tempo` · `/ph/run` · `/ph/steps` · `/ph/track t typeIdx` (**-1=empty** 0=DRUM
1=FM7 2=BUCHLOID 3=MOLLY 4=RINGS 5=BEN 6=NOIZEOP 7=ICARUS 8=PLAITS 9=SHAKER 10=MEMBRANE 11=MALLET 12=BOWED 13=PLUCK 15=CHAOS 16=WTABLE 17=BYTEBEAT 18=SAMPLE 19=CSOUND; 14=TUBE retired into PLUCK) ·
`/ph/param t "name" val` (WTABLE's `wt1`/`wt2` are sprite selectors — the engine (re)loads that oscillator's wavetable buffer instead of setting a synth arg; BYTEBEAT's `expr` is a bank index — the engine re-parses its **persistent** voice with the plugin's `/eval` unit command, sent a few control blocks after the node is created, never in the same instant; CSOUND's `arch` picks one of the ten architectures and its `m1`..`m8` are that architecture's macros — both travel to Csound in the score event, not to a synth) ·
`/ph/preview typeIdx note vel mode [name val …]` (audition one voice → master) ·
`/ph/pattern` · `/ph/stepset` · `/ph/steplock` · `/ph/stepmacro` · `/ph/clearlocks` ·
`/ph/clearcell t cell` (empty ONE step slot — what deleting a step sends, so the slot
carries nothing into the next step drawn there) ·
`/ph/stepratchet t cell k` · `/ph/stepsend t cell on` · `/ph/stepfx t cell mask`
(per-step FX: a bitmask over the 8 insert slots, **-1 = no lock**) ·
`/ph/stepcycle t cell n` (fire on every **n**-th repetition of the pattern, 1-8) ·
`/ph/stepfxcycle t cell n` (apply the step's FX mask on every **n**-th PLAY of the step,
1-8 — multiplies with `/ph/stepcycle` the way a living step's period does) ·
`/ph/stepsmp t cell start end` (per-step SAMPLE window, **-1 = inherit the track's**) ·
`/ph/filter t cutoff res type` (per-track multimode filter, type 0=LP 1=HP) ·
`/ph/stepfilt t cell cutoff res type` (per-step filter lock, **cutoff < 0 = follow the track**) ·
`/ph/livingfx dTime dFb dMix vMix vRoom vDamp`
(living-step ratchet / per-step FX-send routing / send-bus params) ·
`/ph/smparm t thresh` (arm the threshold capture) · `/ph/smpwrite \"path\"` · `/ph/smpload \"path\"` ·
`/ph/smpassign t \"path\"` (give the track its OWN buffer, release the pad) ·
`/ph/smpcopy src dst` (duplicate a track's sample buffer — a COPY, so the two tracks can
diverge; what Copy-track sends) ·
`/ph/churncap "path" dur` (record `dur` seconds of the master POST-limiter and write a
finalised WAV — the file appearing is the completion signal) ·
`/ph/churnload "path" slot` · `/ph/churnplay slot amp pan rate hp` · `/ph/churnclear` ·
back: `/ph/smprec`
`/ph/smpdone` `/ph/smpwritten` `/ph/smpready` ·
`/ph/mute` · `/ph/note` · `/ph/vel` · `/ph/length` · `/ph/rate` · `/ph/edittrack` ·
`/ph/fxassign` · `/ph/fxbypass` · `/ph/fxset` · `/ph/fxclear` · `/ph/recstart "path"` ·
`/ph/recstop` · `/ph/mastergain` · `/ph/masterfilter` · `/ph/panic` · `/ph/ping`.

### Telemetry (engine → controller, port 57140)

`/ph/ready` (once) · `/ph/step n` (per step, −1 = stopped) · `/ph/cycle` (each
16-step bar boundary) · `/ph/cpu avg peak nodes`.

---

---

## Repository layout

```
controller/poundhard/   catalog.py    parameter specs for every engine (ranges, musical bands, modulatable)
                        kits.py       sound generation — roles, engine palettes, voice rolls
                        recipes.py    the 18 pattern-generation briefs + candidate scoring
                        variations.py whole-pattern generation and per-pattern variations
                        stepgen.py    the six rhythm algorithms
                        lfo.py        the 32-LFO modulation bank (targets, sync, non-destructive output)
                        mastering.py  the eight output chains and their knob maps
                        strobe.py  churn.py  phrase.py  compass.py   performance modifiers
                        tracks.py     Project / Track — the authoritative musical state
                        engine_bridge.py  OSC to the engine   headless.py  the controller loop
                        csoundfx.py   the SAMPLE engine's offline Csound mangler
                        webserver.py  params.py
controller/vendor/      pythonosc (vendored — no pip on the device)
supercollider/          boot.scd  engine.scd  synthdefs.scd
supercollider/plugins/  ByteBeat, PhSoftcut, PhMicIn — native UGens built for the CM4
csound/                 build-orc.py  ph-engine.orc  trims.txt   (engine 20's orchestra)
docs/                   USER-GUIDE.md — the tutorial and manual
move/                   run-*.sh  stop-stack.sh  deploy*.sh  sc/ph-boot.scd
move/schwung-module/poundhard/   module.json  ui.js  exit-hook.sh  dsp/
                        (the directory name is historical — ui.js is now hosted by phhost)
armbian/launcher/       launcher.py  movedisp.py    the appliance menu + display driver
armbian/phhost/         phhost.mjs  fonts.mjs       the ui.js host API (Node)
armbian/phgain/         phgain.c                    master-volume JACK client
armbian/systemd/        jackd-move, phgain, move-launcher-menu, move-jack-watchdog, move-rt-tune
armbian/sbin/           move-rt-tune.sh  move-jack-watchdog.sh  move-shutdown.sh  boot-stock
armbian/boot/           config.txt.armbian  armbian-cmdline.txt  move-spidev0-off.dts
install.sh              one-command deploy of the whole stack to a Move running Armbian
web/                    poundhard-logo.svg   (brand mark — also served by the web UI)
```

The wordmark uses **[Chakra Petch](https://fonts.google.com/specimen/Chakra+Petch)** —
an angular, industrial typeface that suits the hard, percussion-centric aesthetic.

---

---

## Gotchas

- **ui.js has no sockets** → everything goes through the `ipc/*.json` files, and
  the host's synchronous file I/O can stall the UI, so I/O is kept minimal.
- **LED calls differ:** pads/steps use `setLED` (Note On); the Play and track-row
  buttons use `setButtonLED` (CC). The knob CCs (71–78) and Play CC (85) fall in
  the same numeric range as the pad notes — handlers must match on message type,
  not just number.
- **The server is supernova, not scsynth.** `PH_THREADS` (run-engine.sh, default **3**)
  picks it: >0 = supernova with N DSP threads, 0 = scsynth. Supernova loads **only**
  `*_supernova.so` plugins (both sets ship in the bundle) and needs
  `cap_ipc_lock,cap_sys_nice,cap_sys_resource` on its binary or its parallel DSP threads
  can't go realtime — `chown` clears those caps, so `deploy-controller.sh` re-applies them.
  It also needs its lib path baked in as `DT_RPATH` (a capped binary ignores
  `LD_LIBRARY_PATH`). **GREY is server-conditional**: `GreyholeRaw` won't register on
  supernova, so under it GREY is rebuilt from core UGens (same knobs).
- **Parallelism comes from ParGroups, not from supernova alone.** `~gVoices` is a ParGroup
  of per-track groups and `~gFx` a ParGroup of per-track chains — safe because each track
  owns a private bus. Anything writing a SHARED bus stays serial: voices within one track,
  living-FX hits and palette auditions (`~gSharedVoices`), and the sends/master.
- **A systemd service has no `HOME`, and Csound segfaults without one.** This was the
  entire reason engine 20 was silent under Armbian — not JACK, not the orchestra, not
  priorities. Csound's JACK initialisation dereferences `$HOME` and dies before printing
  anything useful. Proven deterministically: `WITH HOME → starts` / `env -u HOME →
  segfaults, every attempt`. `run-csound.sh` now exports
  `HOME="${HOME:-/data/UserData}"`. The same applies to engine boot generally — a launch
  from the menu inherits no HOME either. scsynth & jackd additionally need RT file-caps,
  re-applied on every deploy.
- **Killing a JACK client doesn't free its name immediately.** `run-csound.sh` used to
  `killall -9 csound` and start a new one straight away, which failed on a client-name
  collision with the corpse. It waits for the name to clear, and retries up to four times.
- **Never restart JACK to "fix" a startup problem.** A watchdog that restarts a wedged
  JACK will, if it is not guarded, fire *during* stack startup — when load is high and
  supernova is still building its graph — and kill the engine it is supposed to protect.
  It ran that way for an hour before the guards (age, load, timeout, three strikes) went in.
  Test both halves of a watchdog, the "should act" and the "should not act", before enabling it.
- **sclang OSC string args arrive as Symbols** — the engine uses
  `.asSymbol` / `.asInteger`.
- **No fallbacks:** a required dependency (a UGen, plugin, file) is called
  unconditionally and fails loudly if absent — features work or they don't.
- **Forwards compatibility:** older projects load into the current stack. A `FMTONE`
  track is remapped to **FM7** at load (the old 2-op params don't map onto 6-op, so it
  comes back as a default FM7 to re-roll), and an FX macro reads its direction with
  `.get(arg, 1)` so a project saved before a param was added won't `KeyError` mid-load —
  which used to crash the load and freeze the instrument. **FX are saved by SLOT INDEX**, so
  the chain can't be reordered silently: snapshots carry an `fx_layout` version, and a
  pre-VERB (v1) project is remapped on load — the flanger is dropped and CLDS/RESO/GREY
  slide down one slot, each carrying its own macro / wet / direction. Without that, a
  track's CLDS would have come back as RING.
- **A unit command sent with the node is lost.** `/u_cmd` delivered in the same instant as
  the `/s_new` that creates its node hits a node the server has not instantiated yet and is
  dropped — silently. That is how BYTEBEAT ended up mute: the ByteBeat UGen starts on an
  `Undefined` expression (silent) and only speaks once its `/eval` lands, so every voice was
  a coin flip. Defer the unit command a few control blocks after creating the node.
- **`Synth:onFree` only fires for a REGISTERED node.** Without `.register` the callback never
  runs, so a reference to a freed synth (panic frees everything under `~gVoices`) lives on and
  every later `.set` goes to a dead node — a track that is silent *forever* with nothing in the
  log but `node not found`.
- **`control.json` outlives the session.** The queue is on disk, so a restarting controller
  would replay the previous session's commands at an engine that is still booting. That wedges
  the graph: `ready` is true, `nodes` is 0, and nothing sounds. The controller now adopts the
  queue's high-water mark on its first read and holds every command until `/ph/ready`.
- **`ready: true` with `nodes: 0` means the graph is gone, not that your feature is broken.**
  Usually an orphaned supernova that survived a kill: the new sclang attaches to it, never runs
  `initTree`, and the default group (node 1) does not exist. Restart properly — and note that
  `pgrep -f "<pattern>"` inside an ssh command matches the ssh command line itself, so the
  remote shell kills its own session and the stack survives. Bracket the pattern
  (`bin/sclan[g]`) and verify with `ps` before starting again.
- **The engine recorder taps hardware bus 0**, so a capture includes anything else the Move is
  playing. If MoveOriginal has audio running, absolute levels are meaningless — verify DSP
  offline instead (`scsynth -N` with `-U plugins`), where the render is isolated and repeatable.
- **A spawned voice's args can be SHADOWED by stale `~pstore` entries.** `~pstore[t]` is
  never cleared when a track changes engine, so appending an arg *after* `merged.getPairs`
  in `~spawn` can lose to an older entry of the same name. This made SAMPLE tracks play the
  1024-frame silent buffer while auditioning worked perfectly (the preview path puts `buf`
  *before* the params). Set such values **into `merged`** — it's a dictionary, so a key can
  only hold one value. Symptom to watch for: correct-looking spawn logs but silence.
- **Only one appliance runs at a time**, and the ports (57110 scsynth/supernova · 57120
  sclang · 57140 controller telemetry) are shared with the other appliances on the menu. A
  clean exit tears the stack down, but an **unclean** exit leaves a sibling's engine
  running — which both holds those ports and (before the fix) matched PoundHard's
  `pgrep -f "bin/sclang"` start-guard, so the engine silently never started and you got
  a half-stack (controller up, no sound). `run-stack.sh` now matches its **own** sclang
  by full path and clears any **foreign** SC engine/controller first. **It must never kill
  `jackd`** — under Armbian that is not a shared audio server it can restart at will, it is
  the process driving the screen, the pads and the LEDs. `stop-stack.sh` kills `sclang`,
  `scsynth`, `supernova` and `csound`, and deliberately leaves `jackd` alone.
- **A process the teardown forgets is worse than one that dies.** `stop-stack.sh` used to
  kill `sclang`, `scsynth` and `jackd` but neither `supernova` nor `csound`. A surviving
  supernova makes the next boot attach to an **orphan server** — `ready` true, zero nodes,
  no audio. A surviving Csound loses its JACK client with `jackd` and then lingers
  *dead-but-present*, which was enough to make `run-csound.sh` skip starting a live one, so
  the CSOUND engine came back silent. Both are killed now, and `run-csound.sh` **replaces**
  any existing instance rather than adopting it.
- **`set -e` will skip the tail of a launch script.** The Csound launch used to sit at the
  end of `run-engine.sh`; any non-zero command above it ended the script first, so engine 20
  never started and nothing in the log said why. It is launched from `run-stack.sh` now, in
  its own subshell that waits for the server's ports and cannot be skipped by an unrelated
  failure upstream. A launcher that can silently not-launch is worth restructuring.
- **Realtime placement matters as much as realtime priority.** Csound came up at priority
  **65 — exactly supernova's DSP threads** — and unpinned across all four cores, so it
  competed with the very threads that consume its audio in the same JACK cycle and migrated
  between them mid-callback. That was the XRuns. It runs at **68**, above the DSP threads
  and below `jackd`, pinned to core 3. Measured: 0 XRuns over 25 s with four Csound tracks
  and reverbs at 43% CPU, where the same rig XRan before.
- **A limiter has to be after the sum, not on each voice.** Every Csound voice was
  individually under the ceiling and four overlapping hits on one track summed straight
  past it. Voices accumulate onto a per-track bus and one limiter runs after the sum. The
  same trap in miniature: that limiter is a UDO called once per track **by name**, because
  its detector and held gain are state and a runtime loop would reuse one instance for all
  17 pairs — a single loud track would have ducked every other one.
- **Deleting something must delete its state, not just its visible part.** Turning a step
  off removed the hit and left its pitch, velocity, pan, macro, FX mask, cycle divider,
  filter, window, ratchet, send and living mark in the slot, which the next step drawn
  there inherited. The fix derives the field list from the `Track` dataclass rather than
  writing it out, so a per-step parameter added later is cleared automatically — the
  hand-written list in `clear_pattern` had already drifted and was missing five fields.
- **Cell indices into the engine palette must be derived, never hardcoded.** `ui.js` had
  `SAMPLE_CELL = 18` and `MIC_CELL = 20` written out as literals. Merging PLUCK and TUBE
  shifted SAMPLE to 17, so 18 then pointed at CSOUND — which broke the hold-a-pad-to-sample
  gesture *and* the CSOUND pad audition, in two places that look unrelated to the merge.
  They are computed now (`ENGINE_TYPES.indexOf('SAMPLE')`, `N_ENGINES`).
- **A merge is not finished until every parallel table is merged too.** Removing TUBE meant
  touching `TYPE_INDEX`, `PALETTE_ENGINES`, the role pools, the weights, the ui.js engine
  list and the engine's own dispatch. `variations.py` still did `pool.update(kits.TUBE_ROLES)`
  — identical keys, so it silently overwrote the merged PLUCK flavours and resurrected the
  dead type 14 in generated patterns.
- **`pkill -f <pattern>` matches the shell that runs it.** Over SSH that means the remote
  shell kills its own session and the thing you were aiming at survives — repeatedly, and it
  looks like the command did nothing. Bracket the pattern (`bin/sclan[g]`) and check with
  `ps` first.
- **`set -e` and `&&`/`||` do not mix in POSIX sh.** `cmd && x` at statement level returns
  non-zero when the condition is false and aborts the whole script under `set -e`; and
  `||`/`&&` are equal precedence and left-associative, so `a || b && c` is not what it looks
  like. A wait loop written that way exited immediately, and a retry loop gave up after
  attempt 1. Write them as explicit `if` blocks and append `|| true` where a failure is fine.
- **Don't write scratch to `/tmp` on the device.** The root filesystem is **463 MB at ~99%
  full** (the partition is 2 GB; the filesystem inside it is not). `/data` has 54 GB.
- **Never `saveproj` into a saved slot during a device test.** It overwrites the user's
  project, and it has already done so once.
- **Do not disable the Move's update services** (`swupdate` / `UpdateDBusService`) to block
  auto-updates — `MoveControlModeHandler` hangs forever when they are absent and the device
  won't finish booting. This applies to the stock system; under Armbian they are gone anyway.

---

---

## License & disclaimer

> **Plain-language summary:** PoundHard is a free, unofficial, hobbyist project. It is
> **not** an Ableton product, it comes with **no warranty of any kind**, and running it
> **modifies your Move at your own risk**. It builds on other people's free software,
> whose licenses you must also honour. Nothing here is legal advice — where this section
> and an upstream license disagree, the upstream license governs.

### PoundHard's own code

The original PoundHard material in this repository — the SuperCollider synthdefs
(`supercollider/*.scd`), the Python controller (`controller/poundhard/`), the UI module
(`move/schwung-module/`), the Armbian system layer (`armbian/` — launcher, UI host,
volume client, units) and the deploy/build scripts (`install.sh`, `move/*.sh`) —
is released by its author(s) under the **MIT License** (© the PoundHard contributors).
You may use, copy, modify and redistribute *that* material under MIT terms.

**However, PoundHard does not run in isolation.** It links against, bundles, and is
distributed together with third-party software under **copyleft (GPL) licenses** (below).
When PoundHard is conveyed as a working system — or when any GPL component is
redistributed with it — the terms of those licenses (including source-availability and
copyleft obligations) apply to the combined/aggregate work. In practice, treat a
redistributed PoundHard bundle as **governed by the GPL (v3)**, and keep this notice and
the upstream license texts intact.

### Third-party components

PoundHard depends on, embeds, or ships the following. Copyrights belong to their
respective authors; consult each project for authoritative and current license terms.
To the author's best knowledge:

| Component | Role in PoundHard | License (see upstream) |
|---|---|---|
| **SuperCollider** (scsynth / sclang) | the audio engine + language | GPL-3.0-or-later |
| **sc3-plugins** (incl. FM7, Greyhole, JPverb, Streson, DiodeRingMod, chaos & glitch UGens, DWG, TwoTube…) | many of the synthesis/FX UGens | GPL-2.0-or-later / GPL-3.0 (mixed) |
| **mi-UGens** — SuperCollider ports of **Mutable Instruments** *Plaits, Rings, Clouds* | the PLAITS / RINGS / CLOUDS engines | Mutable Instruments DSP © Émilie Gillet (**MIT**); SC UGen wrapper **GPL-3.0** |
| **STK — the Synthesis ToolKit** (Perry R. Cook & Gary P. Scavone) | SHAKER / MEMBRANE / MALLET / BOWED voices (+ bundled `rawwaves/`) | STK permissive free license |
| **ByteBeat** (github.com/midouest/bytebeat) | the BYTEBEAT engine (prebuilt `.so` shipped) | **GPL-3.0** (see `supercollider/plugins/ByteBeat/LICENSE`) |
| **JACK2** (`jackd`, `libjackserver`, `libjack`) | the audio server the engine runs on — **shipped in the runtime bundle** so no other project has to provide it | server **GPL-2.0-or-later**, client library **LGPL-2.1-or-later** |
| **python-osc** (vendored under `controller/vendor/`) | OSC transport in the controller | Unlicense / public domain |
| **Csound** | the **CSOUND engine (20)** and the SAMPLE engine's offline mangler — **shipped in the runtime bundle** (`move/bundle/poundhard-csound.tar.gz`, 20 opcode plugins incl. `librtjack`) | LGPL-2.1-or-later |
| **Composers Desktop Project (CDP8)** | the transform engine behind the **CHURN** modifier — **shipped**, built from source (`move/bundle/poundhard-cdp.tar.gz`, ~220 aarch64 programs) | see `CDP8/LICENSE.txt` (LGPL-2.1 for the library, per-program notices) |
| **softcut-lib** (github.com/monome/softcut-lib) | the tape engine under **COMPASS**, built as the `PhSoftcut` UGen (prebuilt `.so` shipped) | **GPL-3.0** (monome) |
| **Compass** (github.com/oliviercreurer/compass) by Olivier Creurer, w/ contributions from @justmat + @gonecaving | `controller/compass/compass.lua` is vendored **verbatim** and executed, not reimplemented. The COMPASS modifier built on it was **retired** — it never reproduced its input convincingly on this hardware — and STROBE now occupies that pad. The script and its softcut infrastructure remain in the tree, unused, and the attribution stands. | no licence file upstream; © its author, vendored unmodified with attribution |
| **Lua** (5.4) | the interpreter COMPASS runs that script under — **shipped in the runtime bundle** (`move/bundle/poundhard-lua.tar.gz`) | MIT |
| **`jack_move.so`** — the Move JACK driver, from the RNBO tree | the native backend that owns `/dev/ablspi0.0` and carries audio, the display and all MIDI. **Not part of this repo** — it is taken from the device. | **GPL** |
| **Armbian** (trixie, `6.18.x-current-bcm2711` PREEMPT_RT) | the operating system the whole instrument runs on — **not part of this repo** | GPL-2.0 (kernel) + per-package licences |
| **Node.js** | the runtime `phhost` executes `ui.js` under | MIT |
| **Tamzen font** (8×16) | the large screen typeface in `armbian/phhost/fonts.mjs` | free / permissive (see upstream) |
| **Schwung** / move-anything (and its `wildrider` SC bundle) | the framework the **previous edition** ran inside. The Armbian edition does not use it — the plumbing it once supplied is now `armbian/`. **Not part of this repo.** | © its author; separate project & terms |

The prebuilt `ByteBeat.so` is an aarch64 binary of GPL-3.0 source; its corresponding
source is upstream at github.com/midouest/bytebeat, and `move/build-bytebeat.sh`
reproduces the build.

The runtime bundle likewise ships prebuilt aarch64 binaries of GPL software —
SuperCollider (`scsynth`, `supernova`, `sclang`) and JACK2 (`jackd`, `libjackserver`,
`libjack`), the latter taken from the Move's own JACK build. Their corresponding sources
are the upstream projects named above; the binaries carry only a patched RPATH (the
library search path), no code changes.

### Ableton — no affiliation, trademarks, and device content

PoundHard is an **independent, unofficial** project. It is **not** created, sponsored,
endorsed by, or affiliated with **Ableton AG** in any way. *"Ableton"*, *"Move"*,
*"Live"*, *"Wavetable"*, and related names and logos are trademarks of Ableton AG, used
here **only nominatively** to describe interoperability. No trademark or other rights in
them are claimed.

PoundHard runs on Ableton Move **hardware**. In this edition it **replaces** Ableton's
software on the device with an independent Linux system rather than running alongside it.
It does **not** contain, copy, or redistribute Ableton's proprietary firmware, application
binaries, or content — replacing that software on your own device is not the same as
redistributing it, and this repository ships none of it. Installing PoundHard is very
likely to void any warranty, and restoring the device to stock is your responsibility. Where it uses on-device Ableton resources — most
notably the **WTABLE** engine reading the Move's factory **Wavetable sprites** from
`/opt/move/Dsp/Vector/Sprites/` — it does so **only at runtime, on the end user's own
device**, reading files that already ship on the hardware you bought. (Under Armbian those
files still live on the device's own storage; PoundHard reads them in place and copies
nothing.) Nothing proprietary
to Ableton is included in, or distributed by, this repository. Use PoundHard only on a
Move you own, and only with software you are licensed to run.

### No warranty · use entirely at your own risk

PoundHard is provided **"AS IS", without warranty of any kind**, express or implied,
including but not limited to the warranties of merchantability, fitness for a particular
purpose, and non-infringement. **In no event shall the authors or copyright holders be
liable for any claim, damages, or other liability** arising from, out of, or in
connection with PoundHard or its use.

Be specifically aware that PoundHard:

- **modifies the runtime behaviour of a commercial device** and rides on top of a
  reverse-engineered takeover of its software;
- involves **root access and changes to the device filesystem**, which can render the
  device unbootable — this project has, in development, temporarily **bricked the boot**
  (recoverable over SSH; see the git history and the warning against disabling the Move's
  update services);
- **may void your warranty**, may be affected or removed by official firmware updates,
  and may stop working on future device revisions;
- is an **experimental hobbyist instrument**, not a supported product.

If any of that is not acceptable to you, **do not install or run PoundHard.** By using
it, you accept full responsibility for what happens to your device and your data.

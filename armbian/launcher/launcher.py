#!/usr/bin/env python3
"""Move appliance launcher.

A JACK client. Reads the jogwheel from system:midi_capture and draws the
menu to system:display. No Ableton, no Schwung.

  jogwheel turn  -> CC 14 ch 15   (value 1 = +1 detent, 127 = -1)
  jogwheel push  -> Note 9 ch 15  (vel 127 press, 0 release)
"""
import base64, json, os, socket, subprocess, sys, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import jack
import movedisp
from movedisp import Framebuffer, WIDTH

APPLIANCE_DIR = "/data/UserData/schwung/modules/overtake"
FIRST = "poundhard"                     # always listed first
SHUTDOWN_ID = "__shutdown__"            # synthetic entry, always last

# Stock's flow: MoveXmosPower "shutdown" starts the XMOS shutdown animation and
# arms a 30s power-off; the systemd shutdown hook then calls "power-off" to cut
# power immediately. We reproduce both halves.
XMOS_POWER = "/opt/move/MoveXmosPower"

# From Schwung's shared/constants.mjs - do NOT infer these from MIDI captures:
CC_JOG    = 14   # MoveMainKnob   turn: 1..63 = +, 64..127 = -
CC_PUSH   = 3    # MoveMainButton the click (no LED)
NOTE_TOUCH = 9   # MoveMainTouch  capacitive touch - deliberately ignored
CC_BACK   = 51   # MoveBack
CC_MASTER = 79   # MoveMaster - master volume knob, RELATIVE encoder, no LED

GAIN_ADDR = ("127.0.0.1", 7666)          # phgain control socket
MASTER_CLIENT = "supernova"              # only this client's out is master audio
VOL_STATE = "/var/lib/move-launcher/volume"
VOL_STEP  = 0.02                          # ~50 detents across the full range
CHANNEL   = 15   # status low nibble

# How to start each appliance. Anything absent from this map has no host yet:
# the ui.js appliances still expect Schwung's API, which no longer exists.
LAUNCH = {
    "poundhard": ["/bin/sh", "/data/UserData/poundhard/run-stack.sh"],
}

# Appliances whose ui.js we can host natively. Selecting one starts its engine
# (LAUNCH) and then runs its ui.js under phhost, which owns the screen and pads
# until it exits (Back button -> host_exit_module -> "X").
UI_HOST = "/opt/phhost/phhost.mjs"
UI = {
    "poundhard": "/data/UserData/schwung/modules/overtake/poundhard/ui.js",
}

# Every launchable appliance MUST have a stop route, otherwise there is no way
# back to this menu without SSH. Teardown must never kill jackd: it is the
# display + jogwheel host this launcher draws through.
STOP = {
    "poundhard": ["/bin/sh", "/data/UserData/poundhard/stop-stack.sh"],
}


def discover():
    items = []
    try:
        names = sorted(os.listdir(APPLIANCE_DIR))
    except OSError:
        names = []
    for n in names:
        mj = os.path.join(APPLIANCE_DIR, n, "module.json")
        try:
            with open(mj) as f:
                meta = json.load(f)
        except Exception:
            continue
        items.append({
            "id": meta.get("id", n),
            "name": (meta.get("name") or n).upper(),
            "abbrev": (meta.get("abbrev") or n[:3]).upper(),
            "runnable": meta.get("id", n) in LAUNCH,
        })
    items.sort(key=lambda a: (a["id"] != FIRST, a["id"]))
    items.append({"id": SHUTDOWN_ID, "name": "SHUT DOWN",
                  "abbrev": "PWR", "runnable": True})
    return items


def fit_scale(s):
    for sc in (3, 2, 1):
        if Framebuffer.text_width(s, sc) <= WIDTH - 4:
            return sc
    return 1


class Launcher:
    def __init__(self, items):
        self.items = items or [{"id": "-", "name": "NO APPLIANCES",
                                "abbrev": "---", "runnable": False}]
        self.sel = 0
        self.running = None          # appliance currently started, if any
        self.ui = None               # ApplianceUI while its ui.js owns the screen
        self.vol = self._load_vol()  # 0..1 knob position (relative encoder: no readback)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.client = None           # set by main(); used to rewire audio
        self.confirm = False         # shutdown confirmation showing
        self.led_queue = []          # raw 3-byte midi awaiting the process callback
        self.led_lock = threading.Lock()
        self.push_vol()
        self.msg = None
        self.msg_until = 0.0
        self.fb = Framebuffer()
        self.payload = b""
        self.lock = threading.Lock()
        self.render()

    def clear_all_leds(self):
        """Turn every pad and button LED off.

        LED state lives in the hardware: once lit it stays lit until something
        says otherwise. When an appliance exits, nothing does - so the grid
        keeps the appliance's colours while the menu is on screen. Mirrors
        Schwung's clearAllLEDs(): note 0..127 vel 0, CC 0..127 value 0,
        both on channel 16 (the only channel the driver accepts).
        """
        msgs = []
        for i in range(128):
            msgs.append(bytes([0x9F, i, 0]))     # pad / note LEDs
            msgs.append(bytes([0xBF, i, 0]))     # button LEDs
        with self.led_lock:
            self.led_queue.extend(msgs)

    def take_leds(self, limit=48):
        with self.led_lock:
            if not self.led_queue:
                return ()
            out = self.led_queue[:limit]
            del self.led_queue[:limit]
            return out

    @staticmethod
    def _load_vol():
        try:
            with open(VOL_STATE) as f:
                return max(0.0, min(1.0, float(f.read().strip())))
        except Exception:
            return 0.8

    def _save_vol(self):
        try:
            os.makedirs(os.path.dirname(VOL_STATE), exist_ok=True)
            with open(VOL_STATE, "w") as f:
                f.write(f"{self.vol:.4f}")
        except Exception:
            pass

    def push_vol(self):
        """Send the gain to phgain. Squared taper: linear gain feels top-heavy."""
        try:
            self.sock.sendto(f"{self.vol ** 2:.5f}".encode(), GAIN_ADDR)
        except Exception:
            pass

    def volume(self, delta):
        self.vol = max(0.0, min(1.0, self.vol + delta * VOL_STEP))
        self.push_vol()
        self._save_vol()
        if not self.ui:                       # appliance owns the screen when running
            self.flash(f"VOL {int(round(self.vol * 100))}", 1.0)

    def move(self, delta):
        if self.confirm:            # any turn cancels the shutdown prompt
            self.confirm = False
            self.flash("CANCELLED", 1.0)
            return
        if self.running:            # jogwheel is inert while an appliance runs
            return
        self.sel = (self.sel + delta) % len(self.items)
        self.render()

    def select(self):
        if self.confirm:            # second push confirms
            self.confirm = False
            self.do_shutdown()
            return
        if self.running:
            self.stop()
            return
        it = self.items[self.sel]
        if it["id"] == SHUTDOWN_ID:
            self.confirm = True
            self.render()
            return
        if not it["runnable"]:
            self.flash("NO HOST YET")
            return
        if it["id"] not in STOP:
            self.flash("NO STOP ROUTE")   # refuse: would be a one-way trip
            return
        self.running = it
        self.flash("STARTING", 1.2)
        threading.Thread(target=self._spawn, args=(it,), daemon=True).start()

    def do_shutdown(self):
        """Hand off to the detached helper and get out of the way.

        We cannot run the sequence ourselves: our unit Requires=jackd-move, so
        stopping jackd stops US, killing the process mid-shutdown. The helper is
        started in its own session so it survives our teardown.
        """
        self.msg = "SHUTTING DOWN"
        self.msg_until = time.time() + 3600
        self.render()

        def run():
            time.sleep(0.6)                     # let that frame reach the screen
            if self.running and self.running["id"] in STOP:
                try:
                    subprocess.run(STOP[self.running["id"]], timeout=30,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    pass
            self.clear_all_leds()
            time.sleep(0.4)
            print("[launcher] handing off to move-shutdown.sh", flush=True)
            try:
                subprocess.Popen(["/bin/sh", "/usr/local/sbin/move-shutdown.sh"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 start_new_session=True)
            except Exception as e:
                print("[launcher] shutdown handoff FAILED:", repr(e), flush=True)
        threading.Thread(target=run, daemon=True).start()

    def stop(self):
        it = self.running
        if self.ui:
            # ui.stop() triggers _ui_exited, which performs the teardown.
            self.ui.stop()
            return
        self.flash("STOPPING", 1.5)
        def run():
            try:
                subprocess.run(STOP[it["id"]], timeout=30,
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            except Exception:
                pass
            self.running = None
            self.render()
        threading.Thread(target=run, daemon=True).start()

    def _spawn(self, it):
        # Always tear down first. KillMode=process means a launcher restart or
        # crash ORPHANS the appliance stack; re-entering would then reuse the old
        # controller and inherit the previous session\'s state instead of opening
        # clean. Stop is idempotent and cheap when nothing is running.
        if it["id"] in STOP:
            try:
                subprocess.run(STOP[it["id"]], timeout=30,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass
        try:
            subprocess.Popen(LAUNCH[it["id"]],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL,
                             start_new_session=True)
        except Exception:
            self.flash("FAILED")
            self.running = None
            self.render()
            return
        # Hand the screen and pads to the appliance's own ui.js, if we can host it.
        path = UI.get(it["id"])
        if path and os.path.exists(path) and os.path.exists(UI_HOST):
            time.sleep(2.0)                     # let the engine settle first
            try:
                print(f"[launcher] starting ui host: {path}", flush=True)
                self.ui = ApplianceUI(path, self._ui_exited)
                print("[launcher] ui host pid", self.ui.proc.pid, flush=True)
            except Exception as e:
                print("[launcher] ui host FAILED:", repr(e), flush=True)
                self.ui = None
                self.flash("UI FAILED")
        else:
            print(f"[launcher] no ui host for {it['id']} "
                  f"(ui={path} exists={os.path.exists(path) if path else None} "
                  f"host_exists={os.path.exists(UI_HOST)})", flush=True)

    def rewire_audio(self):
        """Route the appliance's output through phgain so the master knob works.

        Engines auto-connect straight to system:playback_{1,2}; we insert the
        gain stage in between. Idempotent and best-effort - a missing phgain
        must never stop an appliance from making sound.
        """
        c = self.client
        if c is None:
            return
        moved = []
        try:
            if not c.get_ports("phgain:in_", is_input=True):
                print("[launcher] phgain absent; leaving audio direct", flush=True)
                return
            for n in (1, 2):
                pb = f"system:playback_{n}"
                gin = f"phgain:in_{n}"
                for src_port in c.get_all_connections(pb):
                    name = src_port.name
                    if name.startswith("phgain:"):
                        continue
                    # ONLY the engine's master pair belongs behind the gain stage.
                    # Csound (poundhard_cs) auto-connects to system:playback but its
                    # outputs are TRACK RETURNS feeding supernova:input_3..34 - moving
                    # those into phgain silently breaks engine 20.
                    if not name.startswith(MASTER_CLIENT + ":"):
                        continue
                    try:
                        c.disconnect(name, pb)
                        c.connect(name, gin)
                        moved.append(name)
                    except Exception:
                        pass
                try: c.connect(f"phgain:out_{n}", pb)
                except Exception: pass
            if moved:
                print(f"[launcher] routed {moved} through phgain", flush=True)
        except Exception as e:
            print("[launcher] rewire failed:", repr(e), flush=True)

    def _ui_exited(self):
        """ui.js called host_exit_module (Back), or node died.

        Exiting the appliance must tear the WHOLE stack down, not just reclaim
        the screen: otherwise the engine keeps running, holding audio and CPU,
        while the menu implies nothing is loaded.
        """
        it = self.running
        self.ui = None
        self.msg = None
        if it is not None and it["id"] in STOP:
            self.flash("STOPPING", 2.0)
            def teardown():
                try:
                    subprocess.run(STOP[it["id"]], timeout=30,
                                   stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
                except Exception:
                    pass
                self.running = None
                self.msg = None
                self.clear_all_leds()
                self.render()
                print(f"[launcher] {it['id']} torn down; leds cleared", flush=True)
            threading.Thread(target=teardown, daemon=True).start()
        else:
            self.running = None
            self.clear_all_leds()
            self.render()

    def flash(self, text, secs=1.5):
        self.msg = text
        self.msg_until = time.time() + secs
        self.render()

    def render(self):
        fb = self.fb
        fb.clear()
        n = len(self.items)
        cur = self.items[self.sel]

        if self.confirm:
            fb.text_centered("SHUT DOWN?", 6, scale=2)
            fb.rect_fill(0, 28, WIDTH, 20, True)
            fb.text_centered("PUSH = YES", 32, on=False, scale=2)
            fb.text_centered("TURN = CANCEL", 52, scale=1)
        elif self.msg:
            fb.text_centered(self.msg, 24, scale=fit_scale(self.msg))
        elif self.running:
            r = self.running["name"]
            fb.text_centered("RUNNING", 2, scale=1)
            sc = fit_scale(r)
            fb.rect_fill(0, 14, WIDTH, 7 * sc + 6, True)
            fb.text_centered(r, 17, on=False, scale=sc)
            fb.text_centered("PUSH = STOP", 44, scale=2)
        else:
            # context above / below, selection large and inverted
            if n > 1:
                prev = self.items[(self.sel - 1) % n]["name"]
                fb.text_centered(prev[:20], 2, scale=1)
            sc = fit_scale(cur["name"])
            bar_h = 7 * sc + 6
            fb.rect_fill(0, 16, WIDTH, bar_h, True)
            fb.text_centered(cur["name"], 19, on=False, scale=sc)
            if n > 1:
                nxt = self.items[(self.sel + 1) % n]["name"]
                fb.text_centered(nxt[:20], 16 + bar_h + 4, scale=1)
            tag = f"{self.sel + 1}/{n}" + ("" if cur["runnable"] else "  (no host)")
            fb.text_centered(tag, 56, scale=1)

        with self.lock:
            self.payload = fb.payload()

    def tick(self):
        if self.msg and time.time() > self.msg_until:
            self.msg = None
            self.render()

    def current_payload(self):
        with self.lock:
            return self.payload


class ApplianceUI:
    """Runs an appliance's ui.js under node and bridges it to JACK.

    node stdout: F:<b64 1024-byte frame>  L:<b64 raw midi>  X
    node stdin : M:<b64 raw midi>
    """

    def __init__(self, ui_path, on_exit):
        self.on_exit = on_exit
        # node MUST NOT inherit our LD_LIBRARY_PATH: it points at PoundHard's
        # bundled libstdc++, which is older than libnode needs (GLIBCXX_3.4.32),
        # and node dies at startup. The engine scripts set their own path.
        env = dict(os.environ)
        env.pop("LD_LIBRARY_PATH", None)
        self.proc = subprocess.Popen(
            ["/usr/bin/node", UI_HOST, ui_path],
            cwd=os.path.dirname(UI_HOST), env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=open("/tmp/phhost.err", "ab", buffering=0), bufsize=0)
        self.frame = None
        self.frames = 0
        self.wrote = 0
        self.leds = []
        self.lock = threading.Lock()
        self.alive = True
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        try:
            while True:
                raw = self.proc.stdout.readline()
                if not raw:
                    break
                line = raw.decode("ascii", "ignore").strip()
                if not line:
                    continue
                if line[0] == "F" and line[1:2] == ":":
                    data = base64.b64decode(line[2:])
                    if len(data) == movedisp.BUFLEN:
                        with self.lock:
                            self.frame = movedisp.MAGIC + data
                            self.frames += 1
                elif line[0] == "L" and line[1:2] == ":":
                    with self.lock:
                        self.leds.append(base64.b64decode(line[2:]))
                elif line == "X":
                    break
        except Exception:
            pass
        self.alive = False
        rc = self.proc.poll()
        print(f"[launcher] ui host ended (rc={rc})", flush=True)
        try:
            self.proc.terminate()
        except Exception:
            pass
        self.on_exit()

    def feed_midi(self, data):
        if not self.alive:
            return
        try:
            self.proc.stdin.write(b"M:" + base64.b64encode(bytes(data)) + b"\n")
        except Exception:
            self.alive = False

    def take(self):
        with self.lock:
            f, l = self.frame, self.leds
            self.leds = []
            return f, l

    def do_shutdown(self):
        """Graceful power-off.

        Ordering matters: MoveXmosPower opens /dev/ablspi0.0, which jackd holds
        EXCLUSIVELY. Calling it while jackd runs blocks forever and wedges the
        whole shutdown with networking already down. So: tell the user, free the
        device, then talk to the XMOS. Every external call is timeout-bounded -
        nothing here may hang, because a hang leaves an unreachable brick.
        """
        self.msg = "SHUTTING DOWN"
        self.msg_until = time.time() + 3600     # hold it; we are on the way out
        self.render()

        def run():
            time.sleep(0.6)                     # let that frame reach the screen
            if self.running and self.running["id"] in STOP:
                try:
                    subprocess.run(STOP[self.running["id"]], timeout=30,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    pass
            self.clear_all_leds()
            time.sleep(0.4)                     # let the LED clear reach the hardware

            # Free /dev/ablspi0.0 before the XMOS call. After this the display is
            # dead and the XMOS animation takes over.
            for unit in ("move-launcher-menu.service",):
                pass                            # we ARE that unit; do not stop ourselves
            try:
                subprocess.run(["/usr/bin/systemctl", "stop", "phgain.service",
                                "jackd-move.service"], timeout=25,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass

            if os.path.exists(XMOS_POWER):      # stock shutdown animation
                try:
                    subprocess.run([XMOS_POWER, "--command", "shutdown"], timeout=15,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                except Exception:
                    pass
            print("[launcher] powering off", flush=True)
            try:
                subprocess.run(["/usr/bin/systemctl", "poweroff"], timeout=30)
            except Exception:
                os.system("poweroff")
        threading.Thread(target=run, daemon=True).start()

    def stop(self):
        self.alive = False
        try:
            self.proc.terminate()
            self.proc.wait(timeout=5)
        except Exception:
            try: self.proc.kill()
            except Exception: pass


def main():
    app = Launcher(discover())
    client = jack.Client("movelauncher", no_start_server=True)
    app.client = client
    midi_in = client.midi_inports.register("in")
    disp_out = client.midi_outports.register("display")
    led_out = client.midi_outports.register("leds")
    events = []
    ev_lock = threading.Lock()
    dead = threading.Event()
    last_report = [0.0]
    last_rewire = [0.0]

    @client.set_shutdown_callback
    def shutdown(status, reason):
        # jackd went away -> we have no display and no input. Exit so the
        # service manager restarts us once JACK is back.
        dead.set()

    @client.set_process_callback
    def process(frames):
        for _, data in midi_in.incoming_midi_events():
            with ev_lock:
                events.append(bytes(data))
        disp_out.clear_buffer()
        led_out.clear_buffer()
        for m in app.take_leds():
            try: led_out.write_midi_event(0, m)
            except Exception: break
        ui = app.ui
        if ui is not None and ui.alive:
            frame, leds = ui.take()
            if frame:
                try:
                    disp_out.write_midi_event(0, frame)
                    ui.wrote += 1
                except Exception: pass
            for m in leds:
                try: led_out.write_midi_event(0, m)
                except Exception: pass
        else:
            p = app.current_payload()
            if p:
                try: disp_out.write_midi_event(0, p)
                except Exception: pass

    with client:
        for src in ("system:midi_capture", "system:midi_capture_ext"):
            try:
                client.connect(src, midi_in)
            except Exception:
                pass
        try:
            client.connect(led_out, "system:midi_playback")
        except Exception:
            pass
        try:
            client.connect(disp_out, "system:display")
        except Exception as e:
            print("cannot reach system:display:", e)
            return 1
        app.clear_all_leds()
        for _aid, _cmd in STOP.items():
            try:
                subprocess.run(_cmd, timeout=30,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass
        print(f"launcher up: {len(app.items)} appliances (orphans swept)", flush=True)

        while not dead.is_set():
            with ev_lock:
                batch, events[:] = list(events), []
            ui = app.ui
            if ui is not None and ui.alive:
                for d in batch:
                    # master volume is the host's job in every mode; the
                    # appliance never sees it (ui.js does not import MoveMaster)
                    if len(d) >= 3 and (d[0] & 0xF0) == 0xB0 and d[1] == CC_MASTER:
                        v = d[2]
                        app.volume(v if 1 <= v <= 63 else -(128 - v) if v >= 64 else 0)
                        continue
                    ui.feed_midi(d)
                now = time.time()
                if now - last_report[0] > 3.0:
                    last_report[0] = now
                    print(f"[launcher] ui frames_rx={ui.frames} frames_tx={ui.wrote}",
                          flush=True)
                app.tick()
                now2 = time.time()
                if now2 - last_rewire[0] > 3.0:
                    last_rewire[0] = now2
                    app.rewire_audio()
                time.sleep(0.005)
                continue
            for d in batch:
                if len(d) < 3:
                    continue
                status, d1, d2 = d[0], d[1], d[2]
                if (status & 0x0F) != CHANNEL:
                    continue
                kind = status & 0xF0
                if kind == 0xB0 and d1 == CC_JOG:
                    # relative encoder: 1..63 clockwise, 64..127 counter-clockwise
                    if 1 <= d2 <= 63:
                        app.move(1)
                    elif 64 <= d2 <= 127:
                        app.move(-1)
                elif kind == 0xB0 and d1 == CC_MASTER:
                    app.volume(d2 if 1 <= d2 <= 63 else -(128 - d2) if d2 >= 64 else 0)
                elif kind == 0xB0 and d1 == CC_PUSH and d2 > 0:
                    app.select()
            app.tick()
            now = time.time()
            if now - last_rewire[0] > 3.0:
                last_rewire[0] = now
                app.rewire_audio()
            time.sleep(0.02)

    print("jack shutdown - exiting for restart")
    return 1


if __name__ == "__main__":
    sys.exit(main() or 0)

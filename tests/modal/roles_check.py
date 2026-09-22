"""Roll every MODAL material N times through the real kit generator, render each roll with
the PhModal core (render_params), and report loudness and envelope shape per material.
   python3 tests/modal/roles_check.py [rolls]"""
import os, random, statistics as st, subprocess, sys
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
sys.path[:0] = [os.path.join(ROOT, "controller"), os.path.join(ROOT, "controller", "vendor")]
from poundhard import kits, catalog
ORDER = ["amp2","amp3","pos2","pos3","fold","foldpt","exciter","exrate","attack","release",
         "modes","detune","expo","falloff","decay","fx","fy","throat","fmix"]
N = int(sys.argv[1]) if len(sys.argv) > 1 else 30
rng = random.Random(2026); lines = []
for name, role in kits.MODAL_ROLES.items():
    for _ in range(N):
        v = kits.gen_voice(role, rng); p = v["params"]
        vals = []
        for k in ORDER:
            x = p[f"modal.{k}"]
            if k == "exrate": x = catalog.MODAL_DIVIDERS[int(round(x))]
            vals.append(x)
        note = v.get("note", 48)
        lines.append(" ".join(map(str, [name.replace(" ", "_"), note, v.get("vel", 1.0), p["modal.amp"],
                     p["modal.hold"], p["modal.damp"], p["modal.release"], *vals])))
exe = os.path.join(os.path.dirname(__file__), "out", "render_params")
out = subprocess.run([exe], input="\n".join(lines), capture_output=True, text=True).stdout.split("\n")
rows = {}
for l in out:
    if not l.strip(): continue
    lab, pk, r, tp, ln = l.split(); rows.setdefault(lab.replace("_", " "), []).append((float(pk), float(r), float(tp), float(ln)))
print(f"{'material':12s} {'family':7s} {'peak med':>9s} {'loud med':>11s} {'loud spread':>11s} {'to peak':>8s} {'length':>8s}")
allr = []
for name in kits.MODAL_ROLES:
    R = rows[name]; pk = [x[0] for x in R]; rm = [x[1] for x in R]; tp = [x[2] for x in R]; ln = [x[3] for x in R]
    allr += rm
    print(f"{name:12s} {kits.MODAL_FAMILY[name]:7s} {st.median(pk):8.1f}dB {st.median(rm):10.1f}dB "
          f"{max(rm)-min(rm):10.1f}dB {st.median(tp):6.0f}ms {st.median(ln):6.0f}ms")
q = sorted(allr); print(f"\nall rolls loudness (loudest 250 ms): p10 {q[len(q)//10]:.1f}  median {st.median(q):.1f}  p90 {q[9*len(q)//10]:.1f} dB")
print("peaks at/over -0.5 dBFS:", sum(1 for R in rows.values() for x in R if x[0] > -0.5), "of", len(lines))

"""Roll CHAOS characters through the real generator and write hits.txt for render.scd:
one line per hit — `label defname note vel name val name val ...`. Usage: roll.py N [seed]"""
import pathlib, random, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "controller"))
from poundhard import catalog, kits  # noqa: E402

DEFS = ["phChaosScream", "phChaosLorenz", "phChaosCrunch", "phChaosStutter",
        "phChaosFeedback", "phChaosSnap", "phChaosCircuit", "phChaosSwarm"]
n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 7)
out = []
for m in kits._CHAOS_SPEC:
    for _ in range(n):
        v = kits.gen_voice(kits.CHAOS_ROLES[m["name"]], rng)
        p = v["params"]
        args = [f"{catalog.engine_arg(k)} {x}" for k, x in p.items() if k not in ("chaos.mode", "chaos.pan")]
        out.append(f'{m["name"].replace(" ", "_")} {DEFS[int(p["chaos.mode"])]} {v["note"]} {v.get("vel", 1.0)} ' + " ".join(args))
pathlib.Path(__file__).with_name("out").mkdir(exist_ok=True)
pathlib.Path(__file__).with_name("out").joinpath("hits.txt").write_text("\n".join(out) + "\n")
print(len(out), "hits")

"""Per-hit level and envelope of out/chaos.wav (float stereo), grouped by character.
Loudest 250 ms RMS is the level metric (same as tests/modal)."""
import pathlib, struct, sys
import numpy as np
d = pathlib.Path(__file__).with_name("out")
raw = (d / "chaos.wav").read_bytes()
i, fmt, data = 12, None, None
while i < len(raw):
    cid, sz = raw[i:i+4], struct.unpack("<I", raw[i+4:i+8])[0]
    if cid == b"fmt ": fmt = struct.unpack("<HHIIHH", raw[i+8:i+24])
    if cid == b"data": data = raw[i+8:i+8+sz]
    i += 8 + sz + (sz & 1)
ch, sr = fmt[1], fmt[2]
x = np.frombuffer(data, np.float32).reshape(-1, ch).mean(1).astype(float) * (2 ** 0.5)  # centre pan
labels = [l.split()[0] for l in (d / "hits.txt").read_text().split("\n") if l.strip()]
gap, t0, rows = 6.0, 0.5, {}
w = int(0.25 * sr)
for k, lab in enumerate(labels):
    seg = x[int((t0 + k * gap) * sr): int((t0 + (k + 1) * gap - 0.05) * sr)]
    pk = np.abs(seg).max() + 1e-12
    c = np.concatenate([[0.0], np.cumsum(seg ** 2)])
    rms = np.sqrt(max((c[w:] - c[:-w]).max() / w, 0.0)) + 1e-12
    e = np.abs(seg) > pk * 0.01
    on = int(np.argmax(e)); b = seg[on:on + int(0.5 * sr)]
    S = np.abs(np.fft.rfft(b * np.hanning(len(b)))) + 1e-12
    flat = np.exp(np.log(S).mean()) / S.mean()                 # 1 = white noise
    ac = np.correlate(b[:8192], b[:8192], "full")[8191:]; ac /= ac[0] + 1e-12
    rows.setdefault(lab, []).append((20*np.log10(pk), 20*np.log10(rms), np.argmax(np.abs(seg))/sr,
                                     (len(e) - np.argmax(e[::-1])) / sr, flat, ac[40:2000].max(),
                                     np.isfinite(seg).all()))
allr = []
print(f"{'character':12s} {'peak':>7s} {'loud med':>9s} {'min':>6s} {'max':>6s} {'to peak':>8s} {'length':>7s} {'flat':>5s} {'pitch':>5s}")
for lab, r in rows.items():
    a = np.array([q[:6] for q in r]); allr += list(a[:, 1])
    print(f"{lab:12s} {np.median(a[:,0]):7.1f} {np.median(a[:,1]):9.1f} {a[:,1].min():6.1f} {a[:,1].max():6.1f}"
          f" {np.median(a[:,2])*1000:6.0f}ms {np.median(a[:,3])*1000:5.0f}ms {np.median(a[:,4]):5.2f} {np.median(a[:,5]):5.2f}"
          + ("" if all(q[6] for q in r) else "  NaN!"))
allr = np.array(allr)
print(f"all: p10 {np.percentile(allr,10):.1f}  median {np.median(allr):.1f}  p90 {np.percentile(allr,90):.1f} dB")

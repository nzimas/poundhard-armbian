# Per-hit peak and RMS(250 ms) from an engine recording of spaced auditions.
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); n = w.getnframes(); raw = w.readframes(n); w.close()
s = struct.unpack("<%dh" % (n*2), raw); x = [max(abs(s[2*i]), abs(s[2*i+1]))/32768 for i in range(n)]
names = sys.argv[2].split(",")
th = 10**(-45/20); onsets = []; quiet = 0
for i, v in enumerate(x):
    if v < th: quiet += 1
    else:
        if quiet > 0.3*sr or not onsets: onsets.append(i)
        quiet = 0
db = lambda v: 20*math.log10(v) if v > 1e-9 else -180
for k, o in enumerate(onsets[:len(names)]):
    seg = x[o:o+int(0.25*sr)]; pk = max(x[o:o+int(3.3*sr)]); r = math.sqrt(sum(v*v for v in seg)/len(seg))
    print(f"{names[k]:11s} peak {db(pk):6.1f} dB   rms250 {db(r):6.1f} dB")
print(f"({len(onsets)} hits found)")

// Render MODAL voices described on stdin and report loudness and envelope shape.
// One line per voice:  label note vel amp hold damp release  p0 .. p18 (phmodal::Param order)
// Mirrors the phModal synthdef: level 2.0, gate held for `hold`, choke envelope when
// damp = 1 (flat until hold, then to zero over release, curve -4), accents over vel 1.
// Output per line: label peak_dB loud_dB t_peak_ms length_ms (above -50 dBFS)
// loud = the loudest 250 ms anywhere in the sound (RMS): fair to a strike AND a swell.
#include "phmodal_core.hpp"   // -I supercollider/plugins/PhModal
#include <cstdio>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include <vector>
using namespace phmodal;
int main() {
    warmUp();
    const float SR = 48000; std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line); std::string label; float note, vel, amp, hold, damp, rel, p[NPARAM];
        if (!(in >> label >> note >> vel >> amp >> hold >> damp >> rel)) continue;
        for (float &x : p) in >> x;
        alignas(Voice) static unsigned char mem[sizeof(Voice)];
        Voice *v = new (mem) Voice(); v->init(SR);
        for (int i = 0; i < NPARAM; ++i) v->set(i, p[i]);
        const float f = 440.f * std::pow(2.f, (note - 69.f) / 12.f);
        v->setHold(hold);
        v->noteOn(f, std::min(1.f, vel));
        const long total = (long)(5.f * SR), offAt = (long)(hold * SR);
        float buf[64], peak = 0; long tPeak = 0, last = 0; bool off = false;
        std::vector<double> sq; sq.reserve(total);
        for (long t = 0; t < total; t += 64) {
            if (!off && t >= offAt) { v->noteOff(); off = true; }
            v->render(buf, 64, 2.0f);
            for (int i = 0; i < 64; ++i) {
                const long n = t + i; const float secs = (float)n / SR;
                float g = 1.f;
                if (damp >= 0.5f && secs > hold) {   // Env([1,1,0],[hold,release],[0,-4])
                    const float x = std::min(1.f, (secs - hold) / std::max(0.005f, rel));
                    g = 1.f - (1.f - std::exp(-4.f * x)) / (1.f - std::exp(-4.f));
                }
                const float s = std::fabs(buf[i] * g * std::max(1.f, vel) * amp);
                if (s > peak) { peak = s; tPeak = n; }
                sq.push_back((double)s * s);
                if (s > 0.00316f) last = n;               // -50 dBFS
            }
        }
        auto db = [](double x) { return x > 1e-9 ? 20 * std::log10(x) : -180.0; };
        const long W = (long)(0.25f * SR); double run = 0, best = 0;
        for (long i = 0; i < (long)sq.size(); ++i) { run += sq[i]; if (i >= W) run -= sq[i - W]; best = std::max(best, run); }
        std::printf("%s %.1f %.1f %.0f %.0f\n", label.c_str(), db(peak), db(std::sqrt(best / W)),
                    1000.0 * tPeak / SR, 1000.0 * last / SR);
        v->~Voice();
    }
}

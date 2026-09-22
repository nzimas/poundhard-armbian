// How much does upstream's vowel (formant) filter change the level, fully mixed in?
// RMS of a held hit with formantMix 1 vs 0, through the reference spectrum, over random
// vowels, per exciter and note.
#include "phmodal_core.hpp"
#include <cstdio>
#include <new>
#include <random>
#include <vector>
#include <algorithm>
using namespace phmodal;
static double rms(int e, float f, float fx, float fy, float th, float mix) {
    alignas(Voice) static unsigned char mem[sizeof(Voice)];
    Voice *v = new (mem) Voice(); v->init(48000);
    for (int i = 0; i < NPARAM; ++i) v->set(i, kRange[i].def);
    v->set(MODES, 24); v->set(FALLOFF, 1); v->set(DECAY, 1); v->set(EXCITER, (float)e); v->set(EXRATE, 1);
    v->set(ATK, 0.005f); v->set(FX, fx); v->set(FY, fy); v->set(THROAT, th); v->set(FMIX, mix); v->noteOn(f, 1);
    double a = 0; long n = 0; float b[64];
    for (long t = 0; t < 12000; t += 64) { v->render(b, 64, 0.001f); for (float x : b) { a += (double)x*x; n++; } }
    v->~Voice(); return std::sqrt(a / n);
}
int main() {
    warmUp(); std::mt19937 r(9); std::uniform_real_distribution<float> U(0, 1);
    const char *ex[] = {"strike", "noise", "pulses"};
    for (int e : {0, 1, 2}) for (float f : {110.f, 220.f, 440.f, 880.f}) {
        std::vector<double> d;
        for (int k = 0; k < 40; ++k) { float fx = U(r), fy = U(r), th = U(r);
            d.push_back(20 * std::log10(rms(e, f, fx, fy, th, 1) / rms(e, f, fx, fy, th, 0))); }
        std::sort(d.begin(), d.end());
        std::printf("%-7s %4.0f Hz  formant full-mix change: min %6.1f  median %6.1f  max %6.1f dB\n", ex[e], f, d.front(), d[20], d.back());
    }
}

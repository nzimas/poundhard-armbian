// Measure each exciter's loudness through the reference spectrum the levelling assumes
// (24 modes falling as 2/k, ringing 1 s), below the limiter (level 0.001: all linear).
#include "phmodal_core.hpp"   // -I supercollider/plugins/PhModal
#include <cstdio>
#include <new>
using namespace phmodal;
int main() {
    warmUp();
    const float SR = 48000; const char *exn[] = {"impulse","noise","pulses","square","chirp"};
    const float notes[] = {65.4f, 130.8f, 261.6f, 523.3f, 1046.5f};
    for (int e = 0; e < 5; ++e) {
        double sumdb = 0; std::printf("%-8s", exn[e]);
        for (float f : notes) {
            alignas(Voice) static unsigned char mem[sizeof(Voice)];
            Voice *v = new (mem) Voice(); v->init(SR);
            for (int i = 0; i < NPARAM; ++i) v->set(i, kRange[i].def);
            v->set(MODES, 24); v->set(FALLOFF, 1); v->set(DECAY, 1); v->set(DETUNE, 0); v->set(EXPO, 1);
            v->set(FOLD, 0); v->set(FMIX, 0); v->set(EXRATE, 4); v->set(ATK, 0.005f); v->set(REL, 0.3f);
            v->set(EXCITER, (float)e); v->noteOn(f, 1.f);
            // loudness of the HIT: RMS over the first 250 ms, the exciter held throughout
            double acc = 0; long n = 0; float buf[64];
            for (long t = 0; t < (long)(0.25f * SR); t += 64) { v->render(buf, 64, 0.001f);
                for (float x : buf) { acc += (double)x * x; n++; } }
            float rdb = 20 * std::log10(std::sqrt(acc / n) / 0.001f);
            sumdb += rdb; std::printf(" %7.1f", rdb); v->~Voice();
        }
        std::printf("   mean %6.1f dB (level 1.0, pre-limiter)\n", sumdb / 5);
    }
}

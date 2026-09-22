#include "phmodal_core.hpp"   // -I supercollider/plugins/PhModal
#include <cstdio>
#include <new>
using namespace phmodal;
static float hitDb(int e, float f, float exrate) {
    alignas(Voice) static unsigned char mem[sizeof(Voice)];
    Voice *v = new (mem) Voice(); v->init(48000);
    for (int i = 0; i < NPARAM; ++i) v->set(i, kRange[i].def);
    v->set(MODES, 24); v->set(FALLOFF, 1); v->set(DECAY, 1); v->set(FOLD, 0); v->set(FMIX, 0);
    v->set(EXRATE, exrate); v->set(ATK, 0.005f); v->set(REL, 0.3f); v->set(EXCITER, (float)e); v->noteOn(f, 1.f);
    double acc = 0; long n = 0; float buf[64];
    for (long t = 0; t < 12000; t += 64) { v->render(buf, 64, 0.001f); for (float x : buf) { acc += (double)x*x; n++; } }
    v->~Voice(); return 20 * std::log10(std::sqrt(acc / n) / 0.001f);
}
int main() {
    warmUp();
    const float notes[] = {65.4f, 130.8f, 261.6f, 523.3f, 1046.5f};
    for (int e : {2, 3}) { std::printf("%s\n", e == 2 ? "pulses" : "square");
        for (float d : {1.f, 2.f, 3.f, 4.f, 6.f, 8.f}) { std::printf("  div %-4g", d);
            for (float f : notes) std::printf(" %7.1f", hitDb(e, f, d)); std::printf("\n"); } }
}

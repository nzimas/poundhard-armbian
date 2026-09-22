// Offline checks for the PhModal core, the same code the UGen runs.
//   1. every exciter sounds, on a handful of materials, low and high notes
//   2. levelling: 300 random materials land in a narrow loudness band
//   3. nothing non-finite, nothing reaches full scale (the limiter bends, never clips)
//   4. CPU: realtime factor per voice
// Writes a few WAVs to $OUT (default: current dir) for listening.
#include "phmodal_core.hpp"   // -I supercollider/plugins/PhModal
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace phmodal;
static const float SR = 48000.f;
static const int BLOCK = 64;

struct Stats { float peak = 0, rmsFirst = 0; long nonfinite = 0, atFull = 0; };

static Stats render(const float p[NPARAM], float freq, float vel, float hold, float secs,
                    std::vector<float> *keep = nullptr) {
    alignas(Voice) static unsigned char mem[sizeof(Voice)];
    Voice *v = new (mem) Voice();
    v->init(SR);
    for (int i = 0; i < NPARAM; ++i) v->set(i, p[i]);
    v->noteOn(freq, vel);
    Stats s; double acc = 0; long accN = 0;
    const long total = (long)(secs * SR), offAt = (long)(hold * SR);
    float buf[BLOCK]; bool off = false;
    for (long t = 0; t < total; t += BLOCK) {
        if (!off && t >= offAt) { v->noteOff(); off = true; }
        v->render(buf, BLOCK, 1.0f);
        for (int i = 0; i < BLOCK; ++i) {
            float x = buf[i];
            if (!std::isfinite(x)) { s.nonfinite++; continue; }
            s.peak = std::max(s.peak, std::fabs(x));
            if (std::fabs(x) >= 0.9999f) s.atFull++;
            if (t + i < (long)SR) { acc += (double)x * x; accN++; }
            if (keep) keep->push_back(x);
        }
    }
    s.rmsFirst = accN ? (float)std::sqrt(acc / accN) : 0;
    v->~Voice();
    return s;
}

static float db(float x) { return x > 1e-9f ? 20.f * std::log10(x) : -180.f; }

static void wav(const std::string &path, const std::vector<float> &x) {
    FILE *f = std::fopen(path.c_str(), "wb"); if (!f) return;
    uint32_t n = (uint32_t)x.size(), sr = (uint32_t)SR, br = sr * 2, sz = 36 + n * 2;
    uint16_t pcm = 1, ch = 1, ba = 2, bps = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&sz, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fl = 16; std::fwrite(&fl, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    std::fwrite(&sr, 4, 1, f); std::fwrite(&br, 4, 1, f); std::fwrite(&ba, 2, 1, f); std::fwrite(&bps, 2, 1, f);
    std::fwrite("data", 1, 4, f); uint32_t ds = n * 2; std::fwrite(&ds, 4, 1, f);
    for (float v : x) { int16_t s = (int16_t)std::max(-32767.f, std::min(32767.f, v * 32767.f)); std::fwrite(&s, 2, 1, f); }
    std::fclose(f);
}

int main() {
    warmUp();
    const char *outdir = std::getenv("OUT") ? std::getenv("OUT") : ".";
    long totalNonfinite = 0, totalFull = 0; int silent = 0;

    // ---- 1. materials x exciters ------------------------------------------------------
    struct Mat { const char *name; float modes, detune, expo, falloff, decay, fold, foldpt; };
    const Mat mats[] = {
        {"default", 40, 0.0f, 1.0f, 1.0f, 1.0f, 0, 1600},
        {"wood",    10, 0.05f, 1.0f, 1.6f, 0.3f, 0, 1600},
        {"metal",   40, 0.35f, 1.15f, 0.5f, 2.5f, 0, 1600},
        {"glass",   16, 0.0f, 1.6f, 0.9f, 1.8f, 0, 1600},
        {"under",   24, 0.1f, 1.0f, 1.0f, 1.2f, 1, 1600},
        {"folded",  30, 0.2f, 1.2f, 0.7f, 1.5f, 2, 1200},
    };
    const char *exn[] = {"impulse", "noise", "pulses", "square", "chirp"};
    std::printf("== 1. every exciter x material (C3 130.8 Hz and A5 880 Hz; hold 0.5 s)\n");
    std::printf("%-8s %-8s %9s %9s %9s %9s\n", "material", "exciter", "peakC3", "rmsC3", "peakA5", "rmsA5");
    for (const Mat &m : mats) for (int e = 0; e < 5; ++e) {
        float p[NPARAM]; for (int i = 0; i < NPARAM; ++i) p[i] = kRange[i].def;
        p[MODES] = m.modes; p[DETUNE] = m.detune; p[EXPO] = m.expo; p[FALLOFF] = m.falloff;
        p[DECAY] = m.decay; p[FOLD] = m.fold; p[FOLDPT] = m.foldpt; p[EXCITER] = (float)e;
        p[ATK] = 0.005f; p[REL] = 0.3f; p[EXRATE] = 4; p[FMIX] = 0.3f;
        std::vector<float> keepC3;
        bool save = (e == 0 || e == 1) && (std::string(m.name) == "metal" || std::string(m.name) == "wood");
        Stats a = render(p, 130.81f, 1.f, 0.5f, 3.f, save ? &keepC3 : nullptr);
        Stats b = render(p, 880.f, 1.f, 0.5f, 3.f);
        std::printf("%-8s %-8s %8.1fdB %8.1fdB %8.1fdB %8.1fdB\n", m.name, exn[e],
                    db(a.peak), db(a.rmsFirst), db(b.peak), db(b.rmsFirst));
        totalNonfinite += a.nonfinite + b.nonfinite; totalFull += a.atFull + b.atFull;
        if (a.peak < 1e-3f) silent++;
        if (b.peak < 1e-3f) silent++;
        if (save) wav(std::string(outdir) + "/modal-" + m.name + "-" + exn[e] + ".wav", keepC3);
    }

    // ---- 2. levelling over random materials ---------------------------------------------
    std::printf("\n== 2. levelling: 300 random materials, random exciter, notes C2..C6\n");
    std::mt19937 rng(12345);
    auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    std::vector<float> peaks, rmss;
    for (int k = 0; k < 300; ++k) {
        float p[NPARAM]; for (int i = 0; i < NPARAM; ++i) p[i] = kRange[i].def;
        p[MODES] = std::round(U(4, 40)); p[DETUNE] = U(-0.02f, 0.5f); p[EXPO] = U(0.8f, 1.6f);
        p[FALLOFF] = U(0.3f, 2.f); p[DECAY] = std::exp(U(std::log(0.15f), std::log(3.f)));
        p[EXCITER] = std::floor(U(0, 5)); p[EXRATE] = std::vector<float>{1, 2, 3, 4, 6, 8}[(int)U(0, 6)];
        p[ATK] = std::exp(U(std::log(0.001f), std::log(0.15f))); p[REL] = U(0.05f, 1.f);
        p[AMP2] = U(0.2f, 1); p[AMP3] = U(0.2f, 1); p[POS2] = U(0.5f, 1); p[POS3] = U(0.5f, 1);
        p[FX] = U(0, 1); p[FY] = U(0, 1); p[THROAT] = U(0, 1); p[FMIX] = U(0, 0.6f);
        float note = 36 + std::floor(U(0, 49));
        float f = 440.f * std::pow(2.f, (note - 69) / 12.f);
        Stats s = render(p, f, 1.f, 0.4f, 1.5f);
        peaks.push_back(db(s.peak)); rmss.push_back(db(s.rmsFirst));
        totalNonfinite += s.nonfinite; totalFull += s.atFull;
        if (s.peak < 1e-3f) silent++;
    }
    auto q = [](std::vector<float> v, float f) { std::sort(v.begin(), v.end()); return v[(size_t)(f * (v.size() - 1))]; };
    std::printf("peak dBFS  min %6.1f  p10 %6.1f  median %6.1f  p90 %6.1f  max %6.1f\n",
                q(peaks, 0), q(peaks, .1f), q(peaks, .5f), q(peaks, .9f), q(peaks, 1));
    std::printf("rms  dBFS  min %6.1f  p10 %6.1f  median %6.1f  p90 %6.1f  max %6.1f\n",
                q(rmss, 0), q(rmss, .1f), q(rmss, .5f), q(rmss, .9f), q(rmss, 1));

    // ---- 3. safety ---------------------------------------------------------------------
    std::printf("\n== 3. safety: non-finite samples %ld, samples at full scale %ld, silent renders %d\n",
                totalNonfinite, totalFull, silent);

    // ---- 4. CPU ------------------------------------------------------------------------
    float p[NPARAM]; for (int i = 0; i < NPARAM; ++i) p[i] = kRange[i].def;
    p[MODES] = 40; p[EXCITER] = NOISE; p[DECAY] = 2; p[ATK] = 0.01f;
    auto t0 = std::chrono::steady_clock::now();
    const int voices = 8; const float secs = 10.f;
    for (int k = 0; k < voices; ++k) render(p, 110.f * (k + 1), 1.f, secs, secs);
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n== 4. CPU: %d voices x %.0f s of 40 modes in %.3f s  ->  %.2f%% of a core per voice\n",
                voices, secs, el, 100.0 * el / (voices * secs));
    return (totalNonfinite == 0 && totalFull == 0 && silent == 0) ? 0 : 1;
}

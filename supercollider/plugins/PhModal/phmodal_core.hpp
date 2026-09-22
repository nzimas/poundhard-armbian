// phmodal_core — one voice of crispinha's modal synthesiser (vendor/modal-synth, GPL-3,
// unmodified), with what PoundHard needs on top of it and nothing else.
//
// A PoundHard hit is one synth, so this is ONE upstream voice (`ModalSynth<40>`): an
// exciter, up to forty resonators tuned as a spectrum, an envelope and a vowel filter.
// Upstream's sixteen-voice controller is not used; PoundHard's own voice allocation and
// per-track polyphony cap do that job.
//
// Three things here are not upstream's, and all three come from NorniOS's modal engine,
// where they were measured (NorniOS docs/adr/0069-modal.md, 0070-modal-crackle.md):
//
//  * THE SPECTRUM IS LEVELLED. A modal bank's loudness is the sum of its modes — one or
//    forty, a steep falloff or a flat one — some thirty decibels from one material to the
//    next. PoundHard rolls materials at random, so the voice divides by what its bank comes
//    to, counting only modes that can be heard, and a given level means the same loudness
//    whatever the material.
//  * A SOFT LIMITER, not a clamp. A clamp on a loud material is hard clipping for most of
//    every note. Under a knee at 0.7 nothing is touched; above it the signal bends towards
//    1.0 along a tanh and never reaches it.
//  * THE GAIN RAMPS across the block. A gain that steps between blocks is a click.
//
// This header is shared by the UGen and the offline test harness, so both run exactly the
// same code. Vendor headers are included with `private` redefined so the levelling can
// read the resonators themselves; every standard header they need is included FIRST, so
// the redefinition never reaches the standard library.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <random>
#include <thread>
#include <type_traits>
#include <utility>
#include <unistd.h>

#include <randutils.hpp>

#define private public
#include <dsp/dsp.hpp>
#include <dsp/modal_synth.hpp>
#undef private

namespace phmodal {

// Upstream's own parameter layout (its PluginProcessor), in its order, ranges and
// defaults. PoundHard's catalog chooses musical ranges inside these.
enum Param {
    AMP2 = 0,  // second-mode amplitude            0..1      1
    AMP3,      // third-mode amplitude             0..1      1
    POS2,      // second-mode position             0..1      1
    POS3,      // third-mode position              0..1      1
    FOLD,      // foldback mode 0 stop/1 under/2 fold (stepped)
    FOLDPT,    // foldback point, Hz               20..20000 1600
    EXCITER,   // 0 impulse/1 noise/2 pulses/3 square/4 chirp (stepped)
    EXRATE,    // exciter rate divider             1..100    4
    ATK,       // exciter attack, s                0..5      0.5
    REL,       // exciter release, s               0..5      0.5
    MODES,     // mode count                       1..40     40 (stepped)
    DETUNE,    // linear inharmonicity             -0.06..2  0
    EXPO,      // exponential inharmonicity        0.1..10   1
    FALLOFF,   // falloff of higher modes          0..3      1
    DECAY,     // decay, s                         0.1..5    1
    FX,        // formant x                        0..1      0.5
    FY,        // formant y                        0..1      0.5
    THROAT,    // throat length                    0..1      0.5
    FMIX,      // formant mix                      0..1      0.5
    NPARAM
};

struct Range { float lo, hi, def; bool stepped; };
extern const Range kRange[NPARAM];

enum Exciter { IMPULSE = 0, NOISE = 1, PULSES = 2, SQUARE = 3, CHIRP = 4 };

class Voice {
public:
    static constexpr int kModes = 40;
    using Synth = modal::dsp::synth::ModalSynth<kModes>;

    // Memory comes from the caller (the UGen uses the realtime allocator); construct in
    // place with placement new, then init().
    void init(float sampleRate);

    void set(int param, float value);          // clamped to upstream's range
    void noteOn(float freqHz, float velocity); // applies pending parameters first
    void noteOff();                            // releases a held exciter

    // Adds nothing, writes n samples: the voice, levelled, at `level` (linear), ramped
    // across the block, soft-limited. Non-finite samples come out as silence.
    void render(float *out, int n, float level);

    bool sounding() const { return on_; }
    float spectrumGain() const { return spectrumGain_; }

private:
    void apply();
    void levelTheSpectrum();

    Synth synth_;
    float value_[NPARAM];
    bool dirty_ = true;
    bool on_ = false;
    float spectrumGain_ = 1.f;
    float noteFreq_ = 261.6f;
    float gain_ = -1.f;   // < 0: the next block jumps straight to its target
};

// Called once at plugin load, off the audio thread: upstream's random generator reads
// the system's random device the first time one is made, and never again.
void warmUp();

} // namespace phmodal

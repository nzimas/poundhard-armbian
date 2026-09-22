// phmodal_core — see phmodal_core.hpp.
#include "phmodal_core.hpp"

// Upstream's DSP compiled into this translation unit, under the same `private`
// redefinition the header used, so the class definitions match exactly.
#define private public
#include "vendor/modal-synth/src/dsp/bonus.cpp"
#include "vendor/modal-synth/src/dsp/filters.cpp"
#include "vendor/modal-synth/src/dsp/formant.cpp"
#include "vendor/modal-synth/src/dsp/mod.cpp"
#include "vendor/modal-synth/src/dsp/osc.cpp"
#include "vendor/modal-synth/src/dsp/resonator.cpp"
#undef private

// Reaching the modes. `ModalSynth` declares `modes` and `currentModes` before any access
// specifier, so they are private by default and the redefinition above does not reach
// them. An explicit instantiation is not access-checked — the standard says so on purpose
// — and instantiating this template defines a function handing back a pointer to the
// member. Upstream is not touched, and if it renames either, this stops compiling rather
// than going quietly wrong. (The same device as NorniOS's ModalSynthEngine.)
namespace phmodal::reach {
template <typename Tag, typename Tag::type Member> struct Instantiate {
    friend typename Tag::type of(Tag) { return Member; }
};
using S = Voice::Synth;
struct Modes {
    using type = std::array<modal::dsp::physical::filters::PhasorResonator, Voice::kModes> S::*;
    friend type of(Modes);
};
struct ModeCount { using type = size_t S::*; friend type of(ModeCount); };
using FF = modal::dsp::physical::FormantFilter;
struct Formants { using type = FF S::*; friend type of(Formants); };
struct FormantMix { using type = modal::dsp::num S::*; friend type of(FormantMix); };
struct FFilters { using type = std::array<modal::dsp::filters::RBJbiquad, 4> FF::*; friend type of(FFilters); };
struct FGains { using type = std::array<modal::dsp::num, 4> FF::*; friend type of(FGains); };
template struct Instantiate<Modes, &S::modes>;
template struct Instantiate<ModeCount, &S::currentModes>;
template struct Instantiate<Formants, &S::formants>;
template struct Instantiate<FormantMix, &S::formant_mix>;
template struct Instantiate<FFilters, &FF::filters>;
template struct Instantiate<FGains, &FF::gains>;
} // namespace phmodal::reach

namespace phmodal {

// The window a hit is judged over: the exciter levels were measured over the first 250 ms.
static constexpr float kHitWindow = 0.25f;

const Range kRange[NPARAM] = {
    {0.f, 1.f, 1.f, false},          // AMP2
    {0.f, 1.f, 1.f, false},          // AMP3
    {0.f, 1.f, 1.f, false},          // POS2
    {0.f, 1.f, 1.f, false},          // POS3
    {0.f, 2.f, 0.f, true},           // FOLD
    {20.f, 20000.f, 1600.f, false},  // FOLDPT
    {0.f, 4.f, 0.f, true},           // EXCITER
    {1.f, 100.f, 4.f, false},        // EXRATE
    {0.f, 5.f, 0.5f, false},         // ATK
    {0.f, 5.f, 0.5f, false},         // REL
    {1.f, 40.f, 40.f, true},         // MODES
    {-0.06f, 2.f, 0.f, false},       // DETUNE
    {0.1f, 10.f, 1.f, false},        // EXPO
    {0.f, 3.f, 1.f, false},          // FALLOFF
    {0.1f, 5.f, 1.f, false},         // DECAY
    {0.f, 1.f, 0.5f, false},         // FX
    {0.f, 1.f, 0.5f, false},         // FY
    {0.f, 1.f, 0.5f, false},         // THROAT
    {0.f, 1.f, 0.5f, false},         // FMIX
};

// ---- The exciters, levelled against each other ------------------------------------------
// The five exciters are some thirty decibels apart through the same bank, and two of them
// move with the note. Measured below the limiter (tests/modal/calibrate*.cpp): the RMS of
// the first 250 ms of a hit, exciter held, through the reference spectrum the levelling
// assumes (24 modes falling as 2/k, ringing 1 s), at level 1.0.
//
//   impulse   -19.2 dB, flat across the keyboard
//   noise     -10.5 dB, flat within a few dB
//   pulses    follows the pulse RATE: +6.1 dB per octave of the note, -6.1 per doubling
//             of the divider (8.3 dB at C4 with divider 1)
//   square    an ODD whole divider puts its odd harmonics exactly on the modes: 39.2 dB
//             minus 20 log10(divider). Anything else is off-resonance: 6.5 dB at C4,
//             falling 6.1 dB per octave.
//   chirp     0.1 dB on average; it scatters by several dB with where its sweep crosses
//             the modes, and that scatter is the chirp's character, not an error
//
// NorniOS does this in its script, starting each exciter at a level of its own; PoundHard
// has no script behind a voice, so the voice does it itself.
static float hitLevelDb(int exciter, float freq, float divider) {
    const float oct = std::log2(std::max(1.f, freq) / 261.6f);
    const float div = std::max(1.f, divider);
    switch (exciter) {
    case IMPULSE: return -19.2f;
    case NOISE:   return -10.5f;
    case PULSES:  return 8.3f + 6.1f * (oct - std::log2(div));
    case SQUARE: {
        const float r = std::round(div);
        const bool oddWhole = std::fabs(div - r) < 0.02f && (static_cast<int>(r) % 2) == 1;
        return oddWhole ? 39.2f - 20.f * std::log10(r) : 6.5f - 6.1f * oct;
    }
    case CHIRP:   return 0.1f;
    default:      return 0.f;
    }
}
static constexpr float kHitTargetDb = -20.f;   // where every hit lands at level 1.0

void warmUp() {
    // The first generator constructed reads std::random_device (a file, on Linux) into a
    // function-local static. Doing it here keeps that off the audio thread for good.
    static Voice::Synth *first = new Voice::Synth();
    (void)first;
}

void Voice::setHold(float seconds) {
    if (std::isfinite(seconds) && seconds != hold_) { hold_ = seconds; if (on_) levelTheSpectrum(); }
}

void Voice::init(float sampleRate) {
    sampleRate_ = sampleRate;
    hold_ = kHitWindow;
    for (int i = 0; i < NPARAM; ++i) value_[i] = kRange[i].def;
    synth_.set_sample_rate(static_cast<modal::dsp::num>(sampleRate));
    dirty_ = true;
    on_ = false;
    spectrumGain_ = 1.f;
    gain_ = -1.f;
}

void Voice::set(int param, float v) {
    if (param < 0 || param >= NPARAM || !std::isfinite(v)) return;
    const Range &r = kRange[param];
    float held = std::max(r.lo, std::min(r.hi, r.stepped ? std::round(v) : v));
    if (held != value_[param]) {
        value_[param] = held;
        dirty_ = true;
    }
}

// Upstream's processBlock, where it hands the parameters to a voice and has it work out
// its modes again — only when something actually changed, as upstream does, because
// recomputing forty modes is not free.
void Voice::apply() {
    dirty_ = false;
    using namespace modal::dsp::synth;
    synth_.set_env_params(value_[ATK], value_[REL]);
    bool changed = synth_.set_params(static_cast<size_t>(value_[MODES]), value_[DETUNE], value_[EXPO],
                                     value_[EXRATE], value_[DECAY], value_[FALLOFF]);
    changed |= synth_.set_mode_freqs({value_[POS2], value_[POS3]});
    changed |= synth_.set_mode_gains({value_[AMP2], value_[AMP3]});
    synth_.set_exciter(static_cast<ModalExiterKind>(static_cast<int>(value_[EXCITER])));
    changed |= synth_.set_foldback_settings(static_cast<ModalFoldbackKind>(static_cast<int>(value_[FOLD])),
                                            value_[FOLDPT]);
    synth_.set_formant_params(value_[FX], value_[FY], value_[THROAT], value_[FMIX]);
    if (changed && on_) synth_.update_mode_coefficients();
    levelTheSpectrum();
}

// ---- The spectrum, levelled -------------------------------------------------------------
// How loud this bank will be, so it can be divided out: a modal bank's loudness runs some
// thirty decibels from one material to the next before a note is played.
//
// NorniOS (whose idea this is) sums the modes' amplitudes, weighting a held mode by its
// full ring time. Three things were wrong with that for PoundHard, each found by measuring:
//
//  * Modes at different frequencies add by ENERGY over a quarter second, harmonic or not,
//    struck or driven. An amplitude sum over-divided many-mode banks (a gong came out 8-10
//    dB quiet, live) and under-divided sparse ones.
//  * A PoundHard hit is short. A slow resonator never reaches its full level inside it, so
//    a struck mode counts the energy it rings with in 250 ms, and a held one how far it
//    builds over the ACTUAL hold (setHold) — a two-second drone builds far past a hit.
//  * The vowel filter takes 13-22 dB out of a bank at full mix (measured, note- and
//    vowel-dependent). Its gain at each mode is computed from the biquads themselves.
//
// Everything is compared against the reference the exciters were measured through, so a
// given level is the same loudness whatever the material, the hold and the vowel.

// Energy a struck mode rings with inside the hit window: its envelope falls 60 dB in t,
// so the integral of its square over T is proportional to t(1 - 0.001^(2T/t)). Over a
// quarter second, modes whose frequencies are more than a few hertz apart add by energy,
// harmonic or not, so the bank's loudness is the root of the sum of these.
static inline float struckEnergy(float t) {
    t = std::fabs(t);
    return t > 1e-6f ? t * (1.f - std::pow(0.001f, 2.f * kHitWindow / t)) : 0.f;
}
static inline float builtUp(float t, float window) {
    t = std::fabs(t);
    return t > 1e-6f ? t * (1.f - std::pow(0.001f, window / t)) : 0.f;
}

void Voice::levelTheSpectrum() {
    const auto &modes = synth_.*of(reach::Modes{});
    const int n = static_cast<int>(synth_.*of(reach::ModeCount{}));
    const int exciter = static_cast<int>(value_[EXCITER]);
    const bool isStruck = exciter == IMPULSE;

    // The vowel filter: four band-passes in parallel, mixed with the dry modes. Its gain at
    // each mode's frequency is exact from the biquads' own coefficients, so a vowel that
    // takes 13-22 dB out of a bank (measured) is put back for exactly this note and vowel.
    const auto &ff = synth_.*of(reach::Formants{});
    const auto &bq = ff.*of(reach::FFilters{});
    const auto &fg = ff.*of(reach::FGains{});
    const float mix = static_cast<float>(synth_.*of(reach::FormantMix{}));
    const float sr = sampleRate_;
    auto dryWet = [&](float f) -> float {
        if (mix <= 1e-4f) return 1.f;
        const std::complex<float> z1 = std::polar(1.f, -2.f * 3.14159265f * f / sr), z2 = z1 * z1;
        std::complex<float> wet(0.f, 0.f);
        for (size_t i = 0; i < 4; ++i) {
            const auto &b = bq[i];
            const std::complex<float> num = b.b0 + b.b1 * z1 + b.b2 * z2, den = b.a0 + b.a1 * z1 + b.a2 * z2;
            if (std::abs(den) > 1e-9f) wet += (num / den) * std::pow(10.f, fg[i] / 20.f);
        }
        return std::norm((1.f - mix) + mix * wet);   // |T|^2
    };

    // Each audible mode's ENERGY inside the hit: modes at different frequencies add by
    // energy over a quarter second, harmonic or not, struck or driven.
    const float hold = std::max(0.03f, std::min(2.f, hold_));
    float energy = 0.f;
    for (int i = 0; i < n && i < kModes; ++i) {
        const auto &m = modes[static_cast<size_t>(i)];
        if (!m.play || m.f < 20.f) continue;
        const float e = isStruck ? m.a * m.a * struckEnergy(m.t)
                                 : (m.a * builtUp(m.t, hold)) * (m.a * builtUp(m.t, hold));
        energy += e * dryWet(m.f);
    }
    // The reference the exciters were measured through: 24 modes falling as 2/k, a 1 s
    // decay, no vowel, a 250 ms hit. A long-held material is levelled to that same hit, so
    // a two-second drone lands where a quarter-second one does rather than building past it.
    static const float struckRef = [] {
        float w = 0; for (int k = 1; k <= 24; ++k) { const float a = 2.f / k; w += a * a * struckEnergy(a); }
        return std::sqrt(w);
    }();
    static const float heldRef = [] {
        float w = 0; for (int k = 1; k <= 24; ++k) { const float a = 2.f / k, b = a * builtUp(a, kHitWindow); w += b * b; }
        return std::sqrt(w);
    }();
    const float weight = std::sqrt(energy);
    const float ref = isStruck ? struckRef : heldRef;
    const float spectrum = std::max(0.02f, std::min(20.f, weight > 1e-6f ? ref / weight : 1.f));
    const float trim = std::pow(10.f, (kHitTargetDb - hitLevelDb(exciter, noteFreq_, value_[EXRATE])) / 20.f);
    spectrumGain_ = std::max(1e-4f, std::min(40.f, spectrum * trim));
}

void Voice::noteOn(float freqHz, float velocity) {
    if (dirty_) apply();
    if (!std::isfinite(freqHz) || freqHz <= 0.f) freqHz = 261.6f;
    velocity = std::max(0.f, std::min(1.f, velocity));
    noteFreq_ = freqHz;
    synth_.on(freqHz, velocity);   // works out the modes for this note
    on_ = true;
    levelTheSpectrum();            // now that the modes sit where this note puts them
}

void Voice::noteOff() {
    if (on_) synth_.off();
}

// Bends rather than clips: untouched under the knee, curved towards 1 above it.
static inline float limited(float s) {
    constexpr float knee = 0.7f, room = 1.f - knee;
    const float a = std::fabs(s);
    if (a <= knee) return s;
    return std::copysign(knee + room * std::tanh((a - knee) / room), s);
}

void Voice::render(float *out, int n, float level) {
    if (dirty_) apply();
    const float target = std::max(0.f, level) * spectrumGain_;
    if (gain_ < 0.f) gain_ = target;   // a strike starts at its level, not faded in
    const float from = gain_;
    const float step = n > 0 ? (target - from) / static_cast<float>(n) : 0.f;
    gain_ = target;
    for (int i = 0; i < n; ++i) {
        float s = static_cast<float>(synth_.tick()) * 0.1f;   // upstream's own output scale
        s *= from + step * static_cast<float>(i);
        if (!std::isfinite(s)) s = 0.f;
        out[i] = limited(s);
    }
}

} // namespace phmodal

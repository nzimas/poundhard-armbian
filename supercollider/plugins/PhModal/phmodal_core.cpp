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
template struct Instantiate<Modes, &S::modes>;
template struct Instantiate<ModeCount, &S::currentModes>;
} // namespace phmodal::reach

namespace phmodal {

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

void Voice::init(float sampleRate) {
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

// How loud this bank is before anything is played through it, so it can be divided out.
// A struck bank rings at the amplitude its modes are set to, so it comes to the sum of
// those. A bank held open by an exciter is different: a resonator asked to ring longer is
// also a narrower one and stands that much further above what is fed into it, so there it
// is the sum of each amplitude times its ring time. Only modes that sound count: a
// stretched spectrum puts most of them past Nyquist, undertones put them below hearing, and
// counting those would divide down the modes you can hear. Both are measured against the
// spectrum the exciters were levelled through: twenty-four modes falling as 2/k, ringing a
// second. Mode frequencies depend on the note, so this runs after the coefficients do.
//
// Two changes from NorniOS, for PoundHard's hits.
//
// STRUCK: NorniOS sums the modes' amplitudes, which assumes they add in step. They only do
// on a harmonic spectrum; thirty inharmonic modes (a gong) drift apart at once and add by
// energy, so the amplitude sum over-divided them by 8-10 dB (measured live). Here a struck
// bank is weighed by the energy it rings with inside the hit (struckEnergy below).
//
// HELD: NorniOS weighs a held mode by its full
// ring time, which is right for a note held long enough to build up. A PoundHard hit feeds
// the bank for a fraction of a second, and a slow resonator gets nowhere near its full
// level in that time: weighing it as if it did made long-ringing materials 10-20 dB too
// quiet. A mode driven for T seconds builds to t(1 - 0.001^(T/t)) of the input: linear in
// T for a slow mode, its full t for a fast one. That is the weight here, T = 250 ms.
static constexpr float kHitWindow = 0.25f;
// Energy a struck mode rings with inside the hit window: its envelope falls 60 dB in t,
// so the integral of its square over T is proportional to t(1 - 0.001^(2T/t)). Over a
// quarter second, modes whose frequencies are more than a few hertz apart add by energy,
// harmonic or not, so the bank's loudness is the root of the sum of these.
static inline float struckEnergy(float t) {
    t = std::fabs(t);
    return t > 1e-6f ? t * (1.f - std::pow(0.001f, 2.f * kHitWindow / t)) : 0.f;
}
static inline float builtUp(float t) {
    t = std::fabs(t);
    return t > 1e-6f ? t * (1.f - std::pow(0.001f, kHitWindow / t)) : 0.f;
}

void Voice::levelTheSpectrum() {
    const auto &modes = synth_.*of(reach::Modes{});
    const int n = static_cast<int>(synth_.*of(reach::ModeCount{}));
    float struck = 0.f, held = 0.f;
    for (int i = 0; i < n && i < kModes; ++i) {
        const auto &m = modes[static_cast<size_t>(i)];
        if (!m.play || m.f < 20.f) continue;
        struck += m.a * m.a * struckEnergy(m.t);
        held += std::fabs(m.a) * builtUp(m.t);
    }
    static const float struckRef = [] {
        float w = 0; for (int k = 1; k <= 24; ++k) { const float a = 2.f / k; w += a * a * struckEnergy(a); }
        return std::sqrt(w);
    }();
    static const float heldRef = [] {
        float w = 0; for (int k = 1; k <= 24; ++k) w += (2.f / k) * builtUp(2.f / k); return w;
    }();
    const int exciter = static_cast<int>(value_[EXCITER]);
    const bool isStruck = exciter == IMPULSE;
    const float weight = isStruck ? std::sqrt(struck) : held;
    const float ref = isStruck ? struckRef : heldRef;
    const float spectrum = std::max(0.02f, std::min(20.f, weight > 1e-6f ? ref / weight : 1.f));
    const float trim = std::pow(10.f, (kHitTargetDb - hitLevelDb(exciter, noteFreq_, value_[EXRATE])) / 20.f);
    spectrumGain_ = std::max(1e-4f, std::min(20.f, spectrum * trim));
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

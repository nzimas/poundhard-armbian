// PhModal — crispinha's modal synthesiser (vendor/modal-synth, GPL-3, unmodified) as a
// SuperCollider unit generator: the MODAL engine. One unit is one voice, the way every
// PoundHard hit is one synth. All the DSP, levelling and limiting live in phmodal_core,
// which the offline checks in tests/modal run too.
//
// Inputs (control rate, read once a block; a change reaches the bank only when it
// actually changes, because recomputing forty modes is not free):
//   0 freq (Hz)  1 vel (0..1)  2 gate (>0 sounds; falling to 0 releases a held exciter)
//   3..21  upstream's parameters in upstream's order (see phmodal_core.hpp)
//   22 level (linear)
// One output: mono audio, levelled and soft-limited, never above full scale.
#include "phmodal_core.hpp"
#include "SC_PlugIn.h"
#include <new>

static InterfaceTable *ft;

struct PhModal : public Unit {
    phmodal::Voice *voice;
    float prev[phmodal::NPARAM];
    bool gateOpen;
};

static void PhModal_next(PhModal *unit, int inNumSamples);
static void PhModal_Ctor(PhModal *unit);
static void PhModal_Dtor(PhModal *unit);

enum { kFreq = 0, kVel = 1, kGate = 2, kFirstParam = 3, kLevel = kFirstParam + phmodal::NPARAM };

static void pushParams(PhModal *unit, bool force) {
    for (int p = 0; p < phmodal::NPARAM; ++p) {
        const float v = IN0(kFirstParam + p);
        if (force || v != unit->prev[p]) {
            unit->prev[p] = v;
            unit->voice->set(p, v);
        }
    }
}

void PhModal_Ctor(PhModal *unit) {
    // The realtime allocator, not new: this runs on the audio thread.
    void *mem = RTAlloc(unit->mWorld, sizeof(phmodal::Voice));
    if (!mem) {
        unit->voice = nullptr;
        SETCALC(ClearUnitOutputs);
        ClearUnitOutputs(unit, 1);
        return;
    }
    unit->voice = new (mem) phmodal::Voice();
    unit->voice->init(static_cast<float>(SAMPLERATE));
    pushParams(unit, true);
    unit->gateOpen = IN0(kGate) > 0.f;
    if (unit->gateOpen) unit->voice->noteOn(IN0(kFreq), IN0(kVel));
    SETCALC(PhModal_next);
    ZOUT0(0) = 0.f;
}

void PhModal_Dtor(PhModal *unit) {
    if (unit->voice) {
        unit->voice->~Voice();
        RTFree(unit->mWorld, unit->voice);
    }
}

void PhModal_next(PhModal *unit, int inNumSamples) {
    pushParams(unit, false);
    const bool open = IN0(kGate) > 0.f;
    if (open && !unit->gateOpen) unit->voice->noteOn(IN0(kFreq), IN0(kVel));   // retrigger
    if (!open && unit->gateOpen) unit->voice->noteOff();
    unit->gateOpen = open;
    unit->voice->render(OUT(0), inNumSamples, IN0(kLevel));
}

PluginLoad(PhModal) {
    ft = inTable;
    phmodal::warmUp();   // off the audio thread: the one read of the system's random device
    DefineDtorUnit(PhModal);
}

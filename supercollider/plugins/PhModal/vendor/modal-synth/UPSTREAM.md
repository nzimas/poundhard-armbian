# modal-synth (vendored, unmodified)

crispinha's modal synthesiser — https://github.com/crispinha/modal-synth
Commit dc91465c18dec29049b96b0626817ee1c2725abd. GPL-3.0-or-later (see LICENSES.md);
randutils.hpp is Melissa O'Neill's, MIT (see RANDUTILS-LICENSE.md).

Only `include/` and `src/dsp/` are taken: the DSP, which is standard C++ with no
framework. The JUCE plugin shell (PluginProcessor, editor) is not needed and not here.
Taken via NorniOS's copy (Vendor/modal-synth), which is the same commit.

To update: copy `include/` and `src/dsp/` from the new commit and re-run
`move/build-modal.sh` and `tests/modal/run.sh`. If upstream renames `ModalSynth::modes`
or `currentModes`, `phmodal_core.cpp` stops compiling rather than going quietly wrong.

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <complex>
#include "dsp.hpp"

namespace modal::dsp::physical::filters {
    /** @brief Modal resonator
     *
     * This implements a modal resonator, which resonates when excited
     * by a single frequency, set as a parameter.
     *
     * This implementation uses
     * [the method described by Matthews and Smith](https://ccrma.stanford.edu/~jos/smac03maxjos/smac03maxjos.pdf),
     * and its implementation is based on
     * [Chowdhury's implementation in modal-waterbottles](https://github.com/jatinchowdhury18/modal-waterbottles/blob/master/WaterbottleSynth/Source/ModeOscillator.h).
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     */
    class PhasorResonator {
     public:
        /** @brief Sets the internal sample rate of the resonator.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr) {
            sample_rate = sr;
            set_params(f, a, t);
        }

        /** @brief Set the mode parameters
         *
         * @param freq Frequency, in Hz
         * @param amp Initial amplitude
         * @param decay Decay time, in seconds.
         */
        void set_params(modal::dsp::num freq, modal::dsp::num amp, modal::dsp::num decay);

        /** @brief Excite the mode so it will ring out, using the set parameters
         */
        void ping() {
            y_del1 = A;
        }

        /** @brief Processes a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        modal::dsp::num tick(modal::dsp::num in);
     private:
        modal::dsp::num f = 0, t = 0, a = 0;
        modal::dsp::num sample_rate = 48000;
        std::complex<modal::dsp::num> A;
        std::complex<modal::dsp::num> y_del1;
        std::complex<modal::dsp::num> filter_coeff;
        bool play = true;
    };
}
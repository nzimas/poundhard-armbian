// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cmath>
#include <dsp/dsp.hpp>

namespace modal::dsp::filters {
    /** @brief All filter types supported by filters::RBJbiquad
     *
     * See filters::RBJbiquad for details.
     * Used internally in filters::RBJbiquad, is unlikely to be used directly.
     */
    enum class BiquadType {
        Zero,
        LPF,
        HPF,
        APF,
        BPF_Q,
        BPF,
        Notch,
        STK_Notch
    };

    /** @brief Biquad implementation of several basic filter types.
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     * Implemented using the [Audio EQ Cookbook](https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html),
     * using Direct Form 1 as recommended in the cookbook.
     */
    struct RBJbiquad {
        /** @brief Processes a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        modal::dsp::num tick(modal::dsp::num in);

        /** @brief Set all coefficients
         *
         * In most cases you should either use one of the defined setting methods
         * or write an new filter-setting method.
         */
        void set_coeffs(modal::dsp::num _a0, modal::dsp::num _a1, modal::dsp::num _a2,
                        modal::dsp::num _b0, modal::dsp::num _b1, modal::dsp::num _b2) {
            a0 = _a0; a1 = _a1; a2 = _a2;
            b0 = _b0; b1 = _b1; b2 = _b2;
        }

        /** @brief Sets the internal sample rate of the filter.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr) {
            sample_rate = sr;
            set_params(type, Fc, Q);
        }
        /** @brief Low-pass filter
         *
         * @param Fc Cutoff frequency in Hz
         * @param Q Resonance / Q
         */
        void set_lpf(modal::dsp::num Fc, modal::dsp::num Q);
        /** @brief High-pass filter
         *
         * @param Fc Cutoff frequency in Hz
         * @param Q Resonance / Q
         */
        void set_hpf(modal::dsp::num Fc, modal::dsp::num Q);
        /** @brief All-pass (phase-changing) filter
         *
         * @param Fc Cutoff frequency in Hz
         * @param Q Resonance / Q
         */
        void set_apf(modal::dsp::num Fc, modal::dsp::num Q);
        /** @brief Band-pass filter with constant skirt gain
         *
         * @param Fc Cutoff frequency in Hz
         * @param Q Peak gain
         */
        void set_bpf_q(modal::dsp::num Fc, modal::dsp::num Q);
        /** @brief Band-pass filter with constant peak gain
         *
         * @param Fc Cutoff frequency in Hz
         * @param BW Bandwidth in octaves
         */
        void set_bpf(modal::dsp::num Fc, modal::dsp::num BW);
        /** @brief Notch filter
         *
         * @param Fc Cutoff frequency in Hz
         * @param BW Bandwidth in octaves
         */
        void set_notch(modal::dsp::num Fc, modal::dsp::num BW);
        /** @brief Notch filter,
         * from the [Synthesis Toolkit](https://ccrma.stanford.edu/software/stk/classstk_1_1TwoZero.html#a3a89facd2b94bbcfc2f06d8766a46329)
         *
         * @param Fc Cutoff frequency in Hz
         * @param r Radius
         */
        void set_stk_notch(modal::dsp::num Fc, modal::dsp::num r);

        /** @brief Sets filter type, from input vaule
         *
         * Internally calls the other set_x methods.
         *
         * @param type Type of filter
         * @param Fc Cutoff frequency in Hz
         * @param Q Q / bandwidth / radius
         */
        void set_params(BiquadType type, modal::dsp::num Fc, modal::dsp::num Q);

     private:
        BiquadType type = BiquadType::Zero;
        modal::dsp::num Fc = 0, Q = 0;
        modal::dsp::num a0 = 0, a1 = 0, a2 = 0;
        modal::dsp::num b0 = 0, b1 = 0, b2 = 0;
        modal::dsp::num sample_rate = 48000;
        modal::dsp::num x[2] = {0, 0}, y[2] = {0, 0};
    };
}

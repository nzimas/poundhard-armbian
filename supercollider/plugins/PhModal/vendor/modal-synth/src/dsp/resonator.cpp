// SPDX-License-Identifier: GPL-3.0-or-later

#include "dsp/dsp.hpp"
#include "dsp/resonator.hpp"

namespace modal::dsp::physical::filters {

    void PhasorResonator::set_params(num freq, num amp, num decay) {
        // don't generate sound if we've above nyquist
        if (freq > 0 && freq < sample_rate / 2) {
            play = true;
        } else {
            play = false;
        }

        f = freq;
        a = amp;
        t = decay;
        A = amp;

        auto decayFactor = std::pow (0.001f, 1.0f / (t * sample_rate));
        auto osc_coeff = std::exp(nums::j * nums::tau * (f / sample_rate));
        filter_coeff = decayFactor * osc_coeff;
    }

    num PhasorResonator::tick(num in) {
        if (!play) return 0;
        // adapted from https://github.com/jatinchowdhury18/modal-waterbottles/blob/master/WaterbottleSynth/Source/ModeOscillator.h
        // auto decayFactor = std::pow (0.001f, 1.0f / (t * sample_rate));
        // auto osc_coeff = std::exp(nums::j * nums::tau * (f / sample_rate));
        // auto filter_coeff = decayFactor * osc_coeff;

        auto y = a * in + filter_coeff * y_del1;
        y_del1 = y;

        return std::imag(y);
    }
}

/*
 *
 * mine, wrong
 *      num lhs = a * in;
        auto rhs = std::exp(nums::j * nums::tau * (f/sample_rate)) * std::exp(-1/(t * sample_rate)) * y_del1;
        auto out = lhs + rhs;
        return std::real(out);
 */
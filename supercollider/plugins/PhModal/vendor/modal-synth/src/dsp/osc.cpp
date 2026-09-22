// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <dsp/osc.hpp>

namespace modal::dsp::osc {
    num saw(num phase) {
        return (phase * 2) - 1;
    }

    num sine(num phase) {
        return std::sin(phase * modal::dsp::nums::pi * 2);
    }

    num tri(num phase) {
        return (std::abs(phase - 0.5_nm) * 4) - 1;
    }

    num rect(num phase, num width) {
        return phase > 1 - width ? 1 : -1;
    }

    num impulse_train(Phasor p) {
        if (p.phase <= p.inc_amount) {
            return 1.0;
        }
        return 0.0;
    }

    num aa_saw(Phasor p) {
        num saw = modal::dsp::osc::saw(p.phase);
        num blep = 0;
        if (p.phase > 1 - p.inc_amount) {
            num t = (p.phase - 1) / p.inc_amount;
            blep = t*t + 2*t + 1;
        } else if (p.phase < p.inc_amount) {
            num t = p.phase / p.inc_amount;
            blep = 2*t - t*t - 1;
        }

        blep *= -1;
        return saw + blep;
    }

    num aa_rect(Phasor p, num width) {
        Phasor p2 = p;
        p2.phase += width;
        if (p2.phase > 1) {
            p2.phase -= 1;
        }

        num blep1 = aa_saw(p);
        num blep2 = aa_saw(p2);

        num dc_correct = 1; /*1.0 / width;
        if (width < 0.5) {
            dc_correct = 1.0 / (1.0 - width);
        }*/

        return (blep1 - blep2) / dc_correct;
    }
}

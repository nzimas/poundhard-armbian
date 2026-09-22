// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <dsp/bonus.hpp>

namespace modal::dsp::bonus {
    num lerp(num a, num b, num t) {
        return a + t * (b - a);
    }

    num midi2freq(num midi_note) {
        return 440 * std::exp2((midi_note - 69)/12);
    }

    num add_cents(num base_freq, num c) {
        return base_freq * std::exp2(c / 1200_nm);
    }

    num db2gain(num db) {
        return std::pow(10_nm, db * 0.05_nm);
    }
}
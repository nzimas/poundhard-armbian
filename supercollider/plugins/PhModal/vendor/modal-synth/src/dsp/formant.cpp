// SPDX-License-Identifier: GPL-3.0-or-later

#include <dsp/formant.hpp>
#include <dsp/bonus.hpp>

void modal::dsp::physical::FormantFilter::set_filters() {
    for (size_t i = 0; i < filters.size(); i++) {
        filters[i].set_bpf(Fcs[i], Qs[i]);
        // filters[i].set_notch(Fcs[i], BWs[i]);
    }
}

modal::dsp::num modal::dsp::physical::FormantFilter::tick(const num in) {
    num out = 0;
    switch (arch) {
        case FormantArch::Cascade: {
            num last = in;
            for (size_t i = 0; i < filters.size(); i++) {
                last = filters[i].tick(last);
                out += last * bonus::db2gain(gains[i]);
            }
        }
        case FormantArch::Parallel: {
            for (size_t i = 0; i < filters.size(); i++) {
                out += filters[i].tick(in) * bonus::db2gain(gains[i]);
            }
        }
    }

    return out;
}

void modal::dsp::physical::FormantFilter::set_vowel(const num x, const num y, const num z, const num throat_len) {
    num throat_ratio = bonus::lerp(1, 1.5, throat_len);
    Fcs = {
        bonus::lerp(270, 660, x) * throat_ratio,
        bonus::lerp(840, 2290, y) * throat_ratio,
        bonus::lerp(1690, 3010, z) * throat_ratio,
        3500 * throat_len
    };
    Qs = {0.1_nm, 0.1_nm, 0.1_nm, 0.1_nm};
    set_filters();
}

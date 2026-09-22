// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dsp/dsp.hpp>
#include <dsp/filters.hpp>

namespace modal::dsp::physical {
    /** @brief Architecture of the filters used in phsycial::FormantFilter
     *
     */
    enum class FormantArch {
        Cascade,
        Parallel
    };

    /** @brief Formant filter to impart vowel-ish qualities to an audio signal.
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     * Uses [formant frequencies as described by Kevin Russel](https://home.cc.umanitoba.ca/~krussll/phonetics/acoustic/formants.html)
     */
    class FormantFilter {
        FormantArch arch;
        std::array<modal::dsp::filters::RBJbiquad, 4> filters;
        std::array<modal::dsp::num, 4> Fcs {0, 0, 0, 0};
        std::array<modal::dsp::num, 4> Qs {0, 0, 0, 0};
        std::array<modal::dsp::num, 4> gains {0, 0, 0, 0};

        void set_filters();

     public:
        /** @brief Constructor.
         *
         * @param architecture Series or parallel processing
         */
        explicit FormantFilter(FormantArch architecture) : arch(architecture) {}

        /** @brief Processes a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        modal::dsp::num tick(modal::dsp::num in);

        /** @brief Sets the internal sample rate of the filter.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(const modal::dsp::num sr) {
            for (auto& f : filters) {
                f.set_sample_rate(sr);
            }
            set_filters();
        }

        /** @brief Sets the filter to a particular vowel sound.
         *
         * @param x First formant frequency, in range 0-1
         * @param y Second formant frequency, in range 0-1
         * @param z Third formant frequency, in range 0-1
         * @param throat_len Length of throat, in range 0-1.
         * Can be used as proxy for gender of voice
         */
        void set_vowel(modal::dsp::num x, modal::dsp::num y, modal::dsp::num z, modal::dsp::num throat_len);

        /** @brief Directly sets parameters for internal band-pass filters
         *
         * Used internally, is unlikely to be used directly.
         *
         * @param new_Fcs Array of cutoff frequencies in Hz
         * @param new_BWs Array of bandwidths in octaves
         */
        void set_formants(std::array<modal::dsp::num, 4> new_Fcs, std::array<modal::dsp::num, 4> new_BWs) {
            Fcs = new_Fcs;
            Qs = new_BWs;
            set_filters();
        }

        /** @brief Sets the filter signal architecture
         *
         * @param architecture Series or parallel processing
         */
        void set_arch(FormantArch architecture) {
            arch = architecture;
        }
    };
}

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dsp/dsp.hpp>

namespace modal::dsp::bonus {
    /**
     * @brief Linear interpolation between a and b at point t.
     *
     * Calculated as \f$ a + t(b-a) \f$
     */
    modal::dsp::num lerp(modal::dsp::num a, modal::dsp::num b, modal::dsp::num t);

    /**
     * @brief Converts given MIDI note number to Hz.
     *
     * MIDI note 69 = 440hz
     */
    modal::dsp::num midi2freq(modal::dsp::num midi_note);

    /**
     * @brief Adds amount of cents to a frequency in Hz.
     *
     * Calculated using [the following equation](https://en.wikipedia.org/wiki/Cent_(music)#Use):
     * \f$ f_{out} = f_{base} * exp2(c / 1200) \f$
     */
    modal::dsp::num add_cents(modal::dsp::num base_freq, modal::dsp::num c);

    /**
     * @brief Converts a value in decibels to a gain value that can be multiplied
     * with audio to change its amplitude by the given amount.
     *
     *  Calculated using [the following equation](https://github.com/juce-framework/JUCE/blob/f2e03eade037e6aa67ba825de871185d66571bf5):
     * \f$ gain = 10^{0.05db} \f$
     */
    modal::dsp::num db2gain(modal::dsp::num db);
}
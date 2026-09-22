// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dsp/bonus.hpp>
#include <array>

namespace modal::dsp {

    /**
     * @brief Round-robin polyphony controller for [instrument classes](docs/DSP Coding Standards.md)
     *
     * @tparam T [Instrument class](docs/DSP Coding Standards.md) to be controlled
     * @tparam count Number of voices
     */
    template <typename T, int count>
    class PolyController {
        std::array<std::optional<int>, count> notes;
        std::array<T, count>& voices;
        size_t last_on_voice = 0;
     public:
        /** @brief Constructor
         *
         * @param v Array of references of voices to be controlled
         */
        explicit PolyController(std::array<T, count>& v) : voices(v) {
            notes.fill(std::nullopt);
        }

        /** @brief Note on
         *
         * Calls note on function of least recently played voice,
         * drops note if all voices are currently on.
         *
         * @param note Note to play, as MIDI note number
         * @param velocity Velocity of note, in range 0-1
         */
        void key_down(int note, float velocity) {
            for (size_t i = 0; i < notes.size(); i++) {
                size_t idx = (i + last_on_voice + 1) % notes.size();
                if (!notes[idx]) {
                    notes[idx] = note;
                    voices[idx].on(modal::dsp::bonus::midi2freq(static_cast<num>(note)), velocity);
                    last_on_voice = idx;
                    return;
                }
            }
        }

        /** @brief Note off
         *
         * Calls note off function of the voice playing specified note,
         * does nothing if no voices are playing that note.
         *
         * @param note Note to release, as MIDI note number
         */
        void key_up(int note) {
            for (size_t i = 0; i < notes.size(); i++) {
                if (notes[i] == note) {
                    voices[i].off();
                    notes[i] = std::nullopt;
                    return;
                }
            }
        }
    };
}
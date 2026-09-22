// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dsp/dsp.hpp>

namespace modal::dsp::mod {
    /** @brief Attack-release envelope.
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     */
    class AREnv {
     public:
       /** @brief Advances the envelope by a single audio sample.
        *
        * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
        * @return The value of the envelope, between 0-1
        */
        modal::dsp::num tick();
        /** @brief Begins the envelope's attack state
         */
        void ping();
        /** @brief Sets the envelope attack and release times.
         *
         * @param atk Attack time, in seconds
         * @param rel Release time, in seconds
         */
        void set_params(modal::dsp::num atk, modal::dsp::num rel);
        /** @brief Sets the internal sample rate of the envelope.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr);
     private:
        enum class ARState {
            Rest,
            Attack,
            Release
        } state;
        modal::dsp::num val;
        modal::dsp::num attack_time, release_time;
        modal::dsp::num attack_inc, release_inc;
        modal::dsp::num sample_rate;
    };

    /** @brief Attack-hold-release envelope.
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     */
    class AHREnv {
     public:
        /** @brief Advances the envelope by a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         * @return The value of the envelope, between 0-1
         */
        modal::dsp::num tick();
        /** @brief Begins the envelope's attack state
         */
        void on();
        /** @brief Begins the envelope's release state
         */
        void off();
        /** @brief Sets the envelope value to 0 and sets it to off.
         */
        void reset();
        /** @brief Sets the envelope attack and release times.
         *
         * @param atk Attack time, in seconds
         * @param rel Release time, in seconds
         */
        void set_params(modal::dsp::num atk, modal::dsp::num rel);
        /** @brief Sets the internal sample rate of the envelope.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr);
     private:
        enum class AHRState {
            Rest,
            Attack,
            Hold,
            Release
        } state = AHRState::Rest;
        modal::dsp::num val = 0;
        modal::dsp::num attack_time = 0, release_time = 0;
        modal::dsp::num attack_inc = 0, release_inc = 0;
        modal::dsp::num sample_rate = 0;
    };
}
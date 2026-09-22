// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <dsp/dsp.hpp>
#include <dsp/bonus.hpp>

namespace modal::dsp::osc {
    /** @brief Phasor-based oscillator.
     *
     * Is a [DSP class](docs/DSP Coding Standards.md).
     * Pass to one of the friend functions in dsp::osc to generate other waves.
     */
    struct Phasor {
        /** @brief Phase of the oscillator (between 0-1)
         */
        modal::dsp::num phase = 0;

        /** @brief Processes a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         * @return The phase of the oscillator after the update.
         */
        modal::dsp::num tick() {
            phase += inc_amount;
            if (phase > 1) {
                phase -= 1;
            }
            return phase;
        }

        /** @brief Sets the frequency of the oscillator
         *
         * @param f Frequency in Hz
         */
        void set_freq(modal::dsp::num f) {
            freq = f;
            inc_amount = f / sample_rate;
        }

        /** @brief Sets the internal sample rate of the oscillator.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr) {
            sample_rate = sr;
            set_freq(freq);
        }

        /** @brief Constructor.
         *
         * @param sr Initial sample rate.
         */
        explicit Phasor (modal::dsp::num sr) : freq{440}, sample_rate{sr} {set_freq(freq);}
     private:
        modal::dsp::num freq;
        modal::dsp::num sample_rate;
        modal::dsp::num inc_amount = 0;

        // declare as friends so they show up in the docs for Phasor
        friend modal::dsp::num saw(modal::dsp::num phase);
        friend modal::dsp::num sine(modal::dsp::num phase);
        friend modal::dsp::num tri(modal::dsp::num phase);
        friend modal::dsp::num rect(modal::dsp::num phase, modal::dsp::num width);

        friend modal::dsp::num impulse_train(Phasor p);
        friend modal::dsp::num aa_saw(Phasor p);
        friend modal::dsp::num aa_rect(Phasor p, modal::dsp::num width);
    };

    /** @brief Un-antialiased saw wave
     *
     * @param phase Phase of the point of the wave to generate, between 0-1.
     * @return A sample of a saw wave, at the phase argument.
     */
    modal::dsp::num saw(modal::dsp::num phase);
    /** @brief Sine wave
     *
     * @param phase Phase of the point of the wave to generate, between 0-1.
     * @return A sample of a sine wave, at the phase argument.
     */
    modal::dsp::num sine(modal::dsp::num phase);
    /** @brief Un-antialiased triangle wave
     *
     * @param phase Phase of the point of the wave to generate, between 0-1.
     * @return A sample of a tri wave, at the phase argument.
     */
    modal::dsp::num tri(modal::dsp::num phase);
    /** @brief Un-antialiased rectangle/pulse/square wave
     *
     * @param phase Phase of the point of the wave to generate, between 0-1.
     * @param width Pulse-width, between 0-1
     * @return A sample of a rectangle wave, at the phase argument.
     */
    modal::dsp::num rect(modal::dsp::num phase, modal::dsp::num width = 0.5);

    /** @brief Impulse train
     *
     * An impulse train produces an evenly-spaced impulse (value of 1.0 for 1 sample)
     * at the rate of the given frequency.
     * @param p Phasor to derive the impulse train from
     * @return A sample of an impulse train, at the point of the phasor
     */
    modal::dsp::num impulse_train(Phasor p);
    /** @brief Antialiased saw wave
     *
     * @param p Phasor to derive the saw wave from
     * @return A sample of a saw wave, at the point of the phasor
     */
    modal::dsp::num aa_saw(Phasor p);
    /** @brief Antialiased rectangle/pulse/square wave
     *
     * @param p Phasor to derive the saw wave from
     * @param width Pulse-width, between 0-1
     * @return A sample of a rectangle wave, at the point of the phasor
     */
    modal::dsp::num aa_rect(Phasor p, modal::dsp::num width = 0.5);

    /** @brief Produces a chirp, or frequency sleep
     *
     * Generates a sine wave with frequency sweeping from 20Hz to 20,000Hz
     * at a set rate.
     * Is a [DSP class](docs/DSP Coding Standards.md).
     */
    struct Chirper {

        /** @brief Processes a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        modal::dsp::num tick() {
            freq_control.tick();
            modal::dsp::num freq_now = bonus::lerp(20, 20'000, freq_control.phase);
            generator.set_freq(freq_now);
            generator.tick();
            return sine(generator.phase);
        }

        /** @brief Sets the rate the generated signal moves from 20Hz to 20,000Hz
         *
         * @param f Rate of frequency sweep, in Hz.
         */
        void set_freq(const modal::dsp::num f) {
            freq_control.set_freq(f);
        }

        /** @brief Sets the internal sample rate of the chirper.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(const modal::dsp::num sr) {
            freq_control.set_sample_rate(sr);
            generator.set_sample_rate(sr);
        }

     private:
        Phasor freq_control {48000};
        Phasor generator {48000};
    };
}


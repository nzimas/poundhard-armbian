// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <dsp/dsp.hpp>
#include "resonator.hpp"
#include <dsp/mod.hpp>
#include <randutils.hpp>
#include <dsp/osc.hpp>

#include <dsp/formant.hpp>

namespace modal::dsp::synth {
    /// @private
    class ModalControls {
        template <size_t>
        friend class ModalSynth;
        std::array<modal::dsp::num, 2> freq_params = {};
        std::array<modal::dsp::num, 2> gain_params = {};
     public:

        [[nodiscard]] modal::dsp::num freq_param_for_mode(const size_t mode) const {
            if (mode <= 0) {
                return 1.0;
            }
            modal::dsp::num factor = 1.0;
            if (mode % 2 == 1) {
                factor *= freq_params[0];
            }
            if (mode % 3 == 2) {
                factor *= freq_params[1];
            }
            return factor;
        }

        [[nodiscard]] modal::dsp::num gain_param_for_mode(const size_t mode) const {
            if (mode <= 0) {
                return 1.0;
            }
            modal::dsp::num factor = 1.0;
            if (mode % 2 == 1) {
                factor *= gain_params[0];
            }
            if (mode % 3 == 2) {
                factor *= gain_params[1];
            }
            return factor;
        }


        void set_freqs(const std::array<modal::dsp::num, 2>& new_freqs) {
            freq_params = new_freqs;
        }

        void set_gains(const std::array<modal::dsp::num, 2>& new_gains) {
            gain_params = new_gains;
        }
    };

    /// @brief Kinds of exciter for the modal synth
    enum class ModalExiterKind {
        /// Impulse (tone will decay).
        Impulse = 0,
        /// White noise
        Noise = 1,
        /// Pitched impulse train
        Impulses = 2,
        /// Pitched square wave
        Square = 3,
        /// Sine rapidly changing in pitch
        Chirp = 4
    };

    /// @brief Spectrum foldback modes for the modal synth
    enum class ModalFoldbackKind {
        /// Modes will not sound at or above the Nyquist frequency
        NyquistStop = 0,
        /// Modes will sound as undertones (successive modes lower than the root).
        Undertones = 1,
        /// Modes will be reflected/aliased around a set frequency
        Foldback = 2
    };

    /// @private
    struct FoldbackSettings {
        ModalFoldbackKind mode = ModalFoldbackKind::NyquistStop;
        modal::dsp::num foldback_point = 1600;
    };

    /**
     * @brief Modal synthesiser
     *
     * This is the implementation of the main synthesiser in the plugin.
     * Has a set of `physical::filters::PhasorResonator` modes, `osc::Phasor` exciters,
     * an `mod::AHREnv` envelope, and a `physical::FormantFilter` filter.
     *
     * Some member functions require the mode coefficients to be updated after they are called.
     * This is an expensive operation, so these functions do not update the coefficients themselves,
     * and require the caller to update the coefficients using `update_mode_coefficients()` after.
     * This is noted in the documentation of those functions.
     *
     * Is an [instrument class](docs/DSP Coding Standards.md).
     * @tparam maxModes Maximum number of modes to synthesise
     */
    template<size_t maxModes>
    class ModalSynth {
        std::array<physical::filters::PhasorResonator, maxModes> modes;
        ModalControls controls;
        size_t currentModes = maxModes;
        modal::dsp::num inharmonicity = 0;
        modal::dsp::num exponent = 0;
        modal::dsp::num freq = 0;
        modal::dsp::num velocity = 1;
        modal::dsp::num decay = 1;
        modal::dsp::num falloff = 1;

        ModalExiterKind exciter = ModalExiterKind::Noise;
        modal::dsp::num exciter_rate = 20;
        modal::dsp::osc::Phasor osc_exciter {48000};
        modal::dsp::osc::Chirper chirp_exciter;
        FoldbackSettings foldback;

        modal::dsp::mod::AHREnv env;
        randutils::default_rng noise;

        modal::dsp::physical::FormantFilter formants {physical::FormantArch::Parallel};
        modal::dsp::num formant_mix = 0.5;

     public:
        /** @brief Note on.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md).
         *
         * Updates mode coefficients.
         *
         * \param key_freq Note to play, as Hz
         * \param vel Velocity of note, in range 0-1
         */
        void on(modal::dsp::num key_freq, modal::dsp::num vel) {
            freq = key_freq;
            velocity = vel;
            update_mode_coefficients();
            switch (exciter) {
                case ModalExiterKind::Impulse:
                    ping();
                    break;
                case ModalExiterKind::Noise:
                case ModalExiterKind::Impulses:
                case ModalExiterKind::Square:
                case ModalExiterKind::Chirp:
                    env.on();
                    break;
            }
        }

        /** @brief Note off.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void off() {
            switch (exciter) {
                case ModalExiterKind::Impulse:
                    break;
                case ModalExiterKind::Noise:
                case ModalExiterKind::Impulses:
                case ModalExiterKind::Square:
                case ModalExiterKind::Chirp:
                    env.off();
                    break;
            }
        }

        /** @brief Synthesises a single audio sample.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        modal::dsp::num tick() {
            // modal::dsp::num to_mode =  * env.tick();
            modal::dsp::num to_mode = 0;

            osc_exciter.tick();
            modal::dsp::num chirp_sig = chirp_exciter.tick();

            switch (exciter) {
                case ModalExiterKind::Noise:
                    to_mode = noise.uniform(-0.05_nm, 0.05_nm);
                    break;
                case ModalExiterKind::Impulses:
                    to_mode = osc::impulse_train(osc_exciter) * 0.6_nm;
                    break;
                case ModalExiterKind::Square:
                    to_mode = osc::aa_rect(osc_exciter, 0.5_nm) * 0.2_nm;
                    break;
                case ModalExiterKind::Chirp:
                    to_mode = chirp_sig * 0.2_nm;
                    break;
                case ModalExiterKind::Impulse:
                    break;
            }

            to_mode *= env.tick();

            modal::dsp::num modes_out = 0;
            for (size_t i = 0; i < currentModes; i++) {
                modes_out += modes[i].tick(to_mode);
            }

            modal::dsp::num formant_out = formants.tick(modes_out);

            modal::dsp::num out = bonus::lerp(modes_out, formant_out, formant_mix);

            out *= (velocity * velocity);

            return out;
        }

        /** @brief Sets the exciter.
         *
         * @param new_exciter New exciter type
         */
        void set_exciter(const ModalExiterKind new_exciter) {
            if (exciter == ModalExiterKind::Noise && new_exciter != ModalExiterKind::Noise) {
                env.reset();
            }
            exciter = new_exciter;
        }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
        /** @brief Sets coefficients related to the spectrum of modes.
         *
         * Requires updating coefficients.
         * @param num_modes Number of modes to synthesise (must be equal or less than `NumModes`)
         * @param inharm Linear inharmonicity factor, usually between -0.06 and 2
         * @param expo Exponential inharmonicity factor, usually between 0.1 and 10
         * @param e_rate Rate or pitch of exciter, in Hz
         * @param dcy Decay time, in seconds
         * @param flof Exponential falloff of increasing modes, usually between 0 and 3
         * @return If coefficients need to be updated
         */
        bool set_params(size_t num_modes, modal::dsp::num inharm, modal::dsp::num expo, modal::dsp::num e_rate, modal::dsp::num dcy, modal::dsp::num flof) {
            bool changed = false;
            if (num_modes != currentModes || inharm != inharmonicity
                || expo != exponent || e_rate != exciter_rate
                || dcy != decay || flof != falloff) {
                changed = true;
            }
            currentModes = num_modes;
            inharmonicity = inharm;
            exponent = expo;
            exciter_rate = e_rate;
            decay = dcy;
            falloff = flof;
            return changed;
        }
#pragma clang diagnostic pop

        /** @brief Update frequency shift of every 2nd and every 3rd mode.
         *
         * Requires updating coefficients.
         * @param new_freqs Frequency shift, like `ModalControls`
         * @return If coefficients need to be updated
         */
        bool set_mode_freqs(const std::array<modal::dsp::num, 2>& new_freqs) {
            bool changed = controls.freq_params != new_freqs;
            controls.set_freqs(new_freqs);
            return changed;
        }

        /** @brief Update frequency shift of every 2nd and every 3rd mode.
         *
         * Requires updating coefficients.
         * @param new_gains
         * @return If coefficients need to be updated
         */
        bool set_mode_gains(const std::array<modal::dsp::num, 2>& new_gains) {
            bool changed = controls.gain_params != new_gains;
            controls.set_gains(new_gains);
            return changed;
        }

        /** @brief Sets the timings for the envelope of the exciter.
         * @param attack Attack time, in seconds
         * @param release Release time, in seconds
         */
        void set_env_params(const modal::dsp::num attack, const modal::dsp::num release) {
            env.set_params(attack, release);
        }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
        /** @brief Set foldback mode.
         *
         * Requires updating coefficients.
         * @param mode `ModalFoldbackKind`, foldback mode for the spectrum
         * @param foldback_point Point to mirror the spectrum when set to `Foldback`, in Hz
         * @return If coefficients need to be updated
         */
        bool set_foldback_settings(const ModalFoldbackKind mode, modal::dsp::num foldback_point) {
            const bool changed = foldback.mode != mode || foldback.foldback_point != foldback_point;
            foldback.mode = mode;
            foldback.foldback_point = foldback_point;
            return changed;
        }
#pragma clang diagnostic pop

        /** @brief Sets the formant filter to a particular vowel sound.
         * @param x First formant position, as 0-1
         * @param y Second formant position, as 0-1
         * @param length length of throat, in range 0-1.
         * Can be used as proxy for gender of voice
         * @param mix Blend between unfiltered and filtered output
         */
        void set_formant_params(modal::dsp::num x, modal::dsp::num y, modal::dsp::num length, modal::dsp::num mix) {
            formants.set_vowel(x, y, 0.5, length);
            formant_mix = mix;
        }

        /** @brief Sets the internal sample rate of the oscillator.
         *
         * Behaves as described in [the DSP coding standards](docs/DSP Coding Standards.md)
         */
        void set_sample_rate(modal::dsp::num sr) {
            for (auto& m : modes) {
                m.set_sample_rate(sr);
            }
            env.set_sample_rate(sr);
            osc_exciter.set_sample_rate(sr);
            chirp_exciter.set_sample_rate(sr);
            formants.set_sample_rate(sr);
        }

        /** @brief Update the internal coefficients of the modal filters
         * to use the updated parameters.
         *
         * Can be expensive, so don't call unnecessarily.
         */
        void update_mode_coefficients() {
            switch (foldback.mode) {
                case ModalFoldbackKind::NyquistStop: {
                    for (size_t i = 0; i < currentModes; i++) {
                        num mode_idx = static_cast<num>(i); // i
                        num mode_idx_p1 = mode_idx + 1; // k
                        modal::dsp::num overtone = mode_idx_p1 * (1 + mode_idx * (inharmonicity * controls.freq_param_for_mode(i)));
                        modal::dsp::num mode_freq = freq * std::pow(overtone, exponent);
                        modal::dsp::num distance = (2.0_nm / std::pow((mode_idx + 1.0_nm), falloff)) * controls.gain_param_for_mode(
                                i);
                        modes[i].set_params(mode_freq, distance, distance * decay);
                    }
                    break;
                }
                case ModalFoldbackKind::Undertones: {
                    for (size_t i = 0; i < currentModes; i++) {
                        num mode_idx = static_cast<num>(i); // i
                        num mode_idx_p1 = mode_idx + 1; // k
                        modal::dsp::num overtone = mode_idx_p1 * (1 + mode_idx * (inharmonicity * controls.freq_param_for_mode(i)));
                        modal::dsp::num mode_freq = freq / std::pow(overtone, exponent);
                        modal::dsp::num distance = (2.0_nm / std::pow((mode_idx + 1.0_nm), falloff)) * controls.gain_param_for_mode(i);
                        modes[i].set_params(mode_freq, distance, distance * decay);
                    }
                    break;
                }
                case ModalFoldbackKind::Foldback: {
                    for (size_t i = 0; i < currentModes; i++) {
                        num mode_idx = static_cast<num>(i); // i
                        num mode_idx_p1 = mode_idx + 1; // k
                        modal::dsp::num overtone = mode_idx_p1 * (1 + mode_idx * (inharmonicity * controls.freq_param_for_mode(i)));
                        modal::dsp::num mode_freq = freq * std::pow(overtone, exponent);
                        // see https://www.desmos.com/calculator/2kbqwfyvjn
                        if (mode_freq > foldback.foldback_point) {
                            mode_freq = (2 * foldback.foldback_point) - mode_freq;
                        }
                        modal::dsp::num distance = (2.0_nm / std::pow((mode_idx + 1.0_nm), falloff)) * controls.gain_param_for_mode(i);
                        modes[i].set_params(mode_freq, distance, distance * decay);
                    }
                    break;
                }
            }
            osc_exciter.set_freq(freq / exciter_rate);
            chirp_exciter.set_freq(freq / exciter_rate);
        }

     private:
        void ping() {
            for (size_t i = 0; i < currentModes; i++) {
                modes[i].ping();
            }
        }
    };
}

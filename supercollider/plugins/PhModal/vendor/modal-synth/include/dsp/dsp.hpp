// SPDX-License-Identifier: GPL-3.0-or-later

# pragma once

#include <complex>

namespace modal::dsp {
    /** @brief Floating point type used in DSP calculations
     *
     * Set with build option `MODAL_NUM_TYPE`, default of `float`
     */
    using num = MODAL_NUM_TYPE;

    /** @brief Literal type for integer literals to dsp::num
     */
    constexpr num operator ""_nm(unsigned long long n) {
        return static_cast<num>(n);
    }

    /** @brief Literal type for floating literals to dsp::num
    */
    constexpr num operator ""_nm(long double d) {
        return static_cast<num>(d);
    }
}

namespace modal::dsp::nums {
    /** @brief \f$i\f$ (\f$\sqrt{-1}\f$)
     */
    const std::complex<num> j = std::complex<num>(0, 1);
    /** @brief \f$\pi\f$ (\f$\tau / 2\f$)
    */
    const num pi = static_cast<const num>(3.14159265358979323);
    /** @brief \f$\tau\f$ (\f$2\pi\f$)
    */
    const num tau = 2 * pi;
}
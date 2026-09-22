// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>

#include <dsp/filters.hpp>
#include <dsp/dsp.hpp>

namespace modal::dsp::filters {
    num RBJbiquad::tick(num in) {
        num out = (b0/a0) * in + (b1/a0) * x[0] + (b2/a0) * x[1]
                                  - (a1/a0) * y[0] - (a2/a0) * y[1];
        if (std::isnan(out) || std::isinf(out)) out = 0;

        x[1] = x[0];
        x[0] = in;
        y[1] = y[0];
        y[0] = out;
        return out;
    }

    void RBJbiquad::set_lpf(num _fc, num _q) {
        type = BiquadType::LPF;
        Fc = _fc;
        Q = _q;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 / (2 * Q);

        b0 = (1 - cos_w0) / 2;
        b1 = 1 - cos_w0;
        b2 = (1 - cos_w0) / 2;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_hpf(num _fc, num _q) {
        type = BiquadType::HPF;
        Fc = _fc;
        Q = _q;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 / (2 * Q);

        b0 = (1 + cos_w0) / 2;
        b1 = -(1 + cos_w0);
        b2 = (1 + cos_w0) / 2;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_apf(num _fc, num _q) {
        type = BiquadType::APF;
        Fc = _fc;
        Q = _q;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 / (2 * Q);

        b0 = 1 - a;
        b1 = -2 * cos_w0;
        b2 = 1 + a;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_bpf_q(num _fc, num _q) {
        type = BiquadType::BPF_Q;
        Fc = _fc;
        Q = _q;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 / (2 * Q);
        num Qa = Q * a;

        b0 = Qa;
        b1 = 0;
        b2 = -Qa;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_bpf(num _fc, num BW) {
        type = BiquadType::BPF;
        Fc = _fc;
        Q = BW;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 * std::sinh((std::log(2_nm)/2) * BW * (w0 / sin_w0));

        b0 = a;
        b1 = 0;
        b2 = -a;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_notch(num _fc, num _q) {
        type = BiquadType::Notch;
        Fc = _fc;
        Q = _q;

        num w0 = modal::dsp::nums::tau * (Fc/sample_rate);
        num cos_w0 = std::cos(w0);
        num sin_w0 = std::sin(w0);
        num a = sin_w0 * std::sinh((std::log(2_nm)/2) * Q * (w0 / sin_w0));

        b0 = 1;
        b1 = -2 * cos_w0;
        b2 = 1;

        a0 = 1 + a;
        a1 = -2 * cos_w0;
        a2 = 1 - a;
    }

    void RBJbiquad::set_stk_notch(num _fc, num r) {
        type = BiquadType::STK_Notch;
        Fc = _fc;
        Q = r;

        a2 = r * r;
        a1 = -2 * r * std::cos(modal::dsp::nums::tau * (Fc/sample_rate));
        a0 = 1;

        b0 = 0.5_nm - 0.5_nm * a2;
        b1 = 0;
        b2 = -b0;
    }

    void RBJbiquad::set_params(BiquadType _type, modal::dsp::num _fc, modal::dsp::num _q) {
        switch (_type) {
            case BiquadType::Zero:
                type = BiquadType::Zero;
                set_coeffs(0, 0, 0, 0, 0, 0);
                break;
            case BiquadType::LPF:
                set_lpf(_fc, _q);
                break;
            case BiquadType::HPF:
                set_hpf(_fc, _q);
                break;
            case BiquadType::APF:
                set_apf(_fc, _q);
                break;
            case BiquadType::BPF_Q:
                set_bpf_q(_fc, _q);
                break;
            case BiquadType::BPF:
                set_bpf(_fc, _q);
                break;
            case BiquadType::Notch:
                set_notch(_fc, _q);
                break;
            case BiquadType::STK_Notch:
                set_stk_notch(_fc, _q);
                break;
        }
    }
}

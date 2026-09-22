// SPDX-License-Identifier: GPL-3.0-or-later

#include <dsp/mod.hpp>

namespace modal::dsp::mod {
	num AREnv::tick() {
		switch (state) {
		case ARState::Rest:
			return 0;
		case ARState::Attack:
			val += attack_inc;
			if (val >= 1) {
				val = 1;
				state = ARState::Release;
			}
			break;
		case ARState::Release:
			val -= release_inc;
			if (val <= 0) {
				val = 0;
				state = ARState::Rest;
			}
			break;
		}
		return val;
	}

	void AREnv::ping() {
		state = ARState::Attack;
	}

	void AREnv::set_params(num atk, num rel) {
        attack_time = atk;
        release_time = rel;
		attack_inc = 1 / (atk * sample_rate);
		release_inc = 1 / (rel * sample_rate);
	}

	void AREnv::set_sample_rate(num sr) {
		sample_rate = sr;
        set_params(attack_time, release_time);
	}

	num AHREnv::tick() {
		switch (state) {
		case AHRState::Rest:
			return 0;
		case AHRState::Attack:
			val += attack_inc;
			if (val >= 1) {
				val = 1;
				state = AHRState::Hold;
			}
			break;
		case AHRState::Hold:
			break;
		case AHRState::Release:
			val -= release_inc;
			if (val <= 0) {
				val = 0;
				state = AHRState::Rest;
			}
			break;
		}
		return val;
	}

	void AHREnv::on() {
		state = AHRState::Attack;
	}

	void AHREnv::off() {
		state = AHRState::Release;
	}

	void AHREnv::reset() {
		state = AHRState::Rest;
		val = 0;
	}

	void AHREnv::set_params(num atk, num rel) {
        attack_time = atk;
        release_time = rel;
		attack_inc = 1 / (atk * sample_rate);
		release_inc = 1 / (rel * sample_rate);
	}

	void AHREnv::set_sample_rate(num sr) {
		sample_rate = sr;
        set_params(attack_time, release_time);
	}
}
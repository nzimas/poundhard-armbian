// PhModal: crispinha's modal synthesiser, one voice — an exciter through up to forty
// resonators tuned as a spectrum, with a vowel filter over them. The MODAL engine.
// Levelled so a given `level` is the same loudness whatever the material, and soft-limited
// so nothing leaves above full scale. Arguments are upstream's parameters, in its order.
//
//   exciter: 0 impulse (struck), 1 noise, 2 pulse train, 3 square, 4 chirp
//   fold:    0 stop at Nyquist, 1 undertones, 2 fold back around foldPoint
//   gate:    a held exciter sounds while gate > 0 and is released when it falls
//   hold:    how long the gate will be open (s) — the levelling uses it, so a long-held
//            drone lands at the same loudness as a short hit
PhModal : UGen {
	*ar { arg freq = 261.6, vel = 1.0, gate = 1.0,
		amp2 = 1.0, amp3 = 1.0, pos2 = 1.0, pos3 = 1.0, fold = 0, foldPoint = 1600,
		exciter = 0, exRate = 4, attack = 0.5, release = 0.5, modes = 40, detune = 0.0,
		expo = 1.0, falloff = 1.0, decay = 1.0, formantX = 0.5, formantY = 0.5,
		throat = 0.5, formantMix = 0.5, level = 1.0, hold = 0.25;
		^this.multiNew('audio', freq, vel, gate,
			amp2, amp3, pos2, pos3, fold, foldPoint, exciter, exRate, attack, release,
			modes, detune, expo, falloff, decay, formantX, formantY, throat, formantMix,
			level, hold);
	}
}

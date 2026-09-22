// Offline render of every hit in out/hits.txt through the REAL CHAOS synthdefs (the block
// is cut out of supercollider/synthdefs.scd and interpreted as is). One hit every `gap` s,
// mono float WAV to out/chaos.wav. Run: sclang render.scd
(
var here = thisProcess.nowExecutingPath.dirname;
var src = File.readAllString(here +/+ "../../supercollider/synthdefs.scd");
var a = src.find("// ================= CHAOS"), b = src.find("// ================= WTABLE");
var lines = File.readAllString(here +/+ "out/hits.txt").split($\n).reject(_.isEmpty);
var gap = 6.0, score, t = 0.5, opts;
~voiceOut = { |out, sig| Out.ar(out, sig * EnvGen.kr(Env.asr(0, 1, 0.025, \sin), \steal.kr(1), doneAction: 2)) };
~killbad = { |sig| LeakDC.ar(sig).clip2(4) };
~spread = { |sig, pan = 0| Pan2.ar(sig, pan.clip(-1, 1)) };
src.copyRange(a, b - 1).interpret;
score = Score.new;
~chaosNames.do { |n| score.add([0, ['/d_recv', SynthDescLib.global[n].def.asBytes]]) };
lines.do { |l, i|
	var w = l.split($ ), args = [\out, 0, \pan, 0, \note, w[2].asFloat, \vel, w[3].asFloat];
	var j = 4;
	while { j < (w.size - 1) } { args = args ++ [w[j].asSymbol, w[j + 1].asFloat]; j = j + 2 };
	score.add([t, ['/s_new', w[1], 2000 + i, 0, 0] ++ args]);
	t = t + gap;
};
score.add([t, ['/c_set', 0, 0]]);
opts = ServerOptions.new.numOutputBusChannels_(2).sampleRate_(44100);
score.recordNRT(nil, here +/+ "out/chaos.wav", sampleRate: 44100, headerFormat: "WAV",
	sampleFormat: "float", options: opts, duration: t,
	action: { "rendered".postln; 0.exit });
)

/*
 * PoundHard appliance host.
 *
 * Replaces Schwung/MoveOriginal for one appliance: provides the host globals
 * ui.js expects, owns the 128x64 1bpp framebuffer, and bridges to a Python
 * JACK client over stdio (which owns system:display / system:midi_*).
 *
 * stdout : F:<base64 1024-byte frame>   L:<base64 3-byte midi>   X
 * stdin  : M:<base64 raw midi>
 */
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { execFile } from 'node:child_process';
import { FONTS } from './fonts.mjs';

const W = 128, H = 64, BUFLEN = 1024;
const fb = new Uint8Array(BUFLEN);
let lastSent = new Uint8Array(BUFLEN);
let refreshHz = 30;
let exiting = false;

function px(x, y, on) {
    x |= 0; y |= 0;
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    const idx = (y >> 3) * W + x, bit = 1 << (y & 7);
    if (on) fb[idx] |= bit; else fb[idx] &= ~bit & 0xFF;
}

const DBG = { clear: 0, fill: 0, rect: 0, print: 0, lit: 0 };
globalThis.clear_screen = function () { DBG.clear++; fb.fill(0); };

globalThis.fill_rect = function (x, y, w, h, c) {
    DBG.fill++;
    const on = c ? 1 : 0;
    x |= 0; y |= 0; w |= 0; h |= 0;
    for (let yy = y; yy < y + h; yy++)
        for (let xx = x; xx < x + w; xx++) px(xx, yy, on);
};

globalThis.draw_rect = function (x, y, w, h, c) {
    DBG.rect++;
    const on = c ? 1 : 0;
    x |= 0; y |= 0; w |= 0; h |= 0;
    if (w <= 0 || h <= 0) return;
    for (let xx = x; xx < x + w; xx++) { px(xx, y, on); px(xx, y + h - 1, on); }
    for (let yy = y; yy < y + h; yy++) { px(x, yy, on); px(x + w - 1, yy, on); }
};

/* Schwung's print() is a TEXT PRIMITIVE, not logging: print(x, y, text, scale).
 * scale 1 -> 5x7 atlas (advance 6), scale >=2 -> tamzen-16 8x16 (advance 8). */
function decodeFont(f) {
    const bits = Buffer.from(f.bits, 'base64');
    const map = new Map();
    f.codes.forEach((c, i) => map.set(c, i));
    return { gw: f.gw, gh: f.gh, bits, map };
}
const F_SMALL = decodeFont(FONTS.small);
const F_BIG = decodeFont(FONTS.big);
const ADV = { small: 6, big: 8 };

function fontFor(scale) { return (!scale || scale < 2) ? F_SMALL : F_BIG; }
function advFor(scale) { return (!scale || scale < 2) ? ADV.small : ADV.big; }

globalThis.print = function (x, y, text, scale) {
    if (typeof x !== 'number') { process.stderr.write([x, y, text, scale].join(' ') + '\n'); return; }
    DBG.print++;
    const f = fontFor(scale), adv = advFor(scale);
    const s = String(text == null ? '' : text);
    let cx = x | 0;
    for (const chr of s) {
        const gi = f.map.get(chr.charCodeAt(0));
        if (gi !== undefined) {
            const base = gi * f.gh;
            for (let r = 0; r < f.gh; r++) {
                const row = f.bits[base + r];
                if (!row) continue;
                for (let c = 0; c < f.gw; c++)
                    if (row & (1 << (7 - c))) px(cx + c, (y | 0) + r, 1);
            }
        }
        cx += adv;
    }
    return cx;
};

globalThis.text_width = function (text, scale) {
    return String(text == null ? '' : text).length * advFor(scale);
};

globalThis.host_read_file = function (path) {
    try { return readFileSync(path, 'utf8'); } catch { return ''; }
};

globalThis.host_write_file = function (path, data) {
    try {
        mkdirSync(dirname(path), { recursive: true });
        writeFileSync(path, String(data));
        return true;
    } catch (e) { globalThis.print('write_file failed', path, e.message); return false; }
};

globalThis.host_system_cmd = function (cmd) {
    try { execFile('/bin/sh', ['-c', cmd], () => {}); } catch {}
};

globalThis.host_set_refresh_rate = function (hz) {
    hz = Number(hz) || 30;
    refreshHz = Math.max(1, Math.min(60, hz));
};

globalThis.host_exit_module = function () {
    exiting = true;
    process.stdout.write('X\n');
    setTimeout(() => process.exit(0), 120);
};

globalThis.move_midi_internal_send = function (packet) {
    /* USB-MIDI 4-byte packet [CIN, status, d1, d2] -> raw 3-byte JACK MIDI.
     *
     * Schwung emits channel 1 (MidiNoteOn = 0x90, MidiCC = 0xB0) and relied on
     * MoveOriginal to translate. JackMoveDriver::provideMidi accepts ONLY
     * note/CC on channel 16 and silently DROPS everything else:
     *     and w2, w1, #0xf ; cmp w2, #0xf ; b.ne <drop>
     * So force the channel nibble to 0xF. 0x90->0x9F, 0xB0->0xBF, 0x80->0x8F. */
    if (!packet || packet.length < 4) return;
    const status = ((packet[1] & 0xF0) | 0x0F) & 0xFF;
    const b = Buffer.from([status, packet[2] & 0x7F, packet[3] & 0x7F]);
    process.stdout.write('L:' + b.toString('base64') + '\n');
};
globalThis.move_midi_external_send = function () {};

let lastFrameAt = 0;
function sendFrameIfChanged() {
    let same = true;
    for (let i = 0; i < BUFLEN; i++) if (fb[i] !== lastSent[i]) { same = false; break; }
    const now = Date.now();
    // Resend periodically even when unchanged: a frame dropped anywhere in the
    // bridge would otherwise leave the screen stale forever.
    if (same && now - lastFrameAt < 1000) return;
    lastFrameAt = now;
    lastSent.set(fb);
    process.stdout.write('F:' + Buffer.from(fb).toString('base64') + '\n');
}

const MODULE = process.argv[2];
if (!MODULE) { console.error('usage: phhost.mjs <ui.js>'); process.exit(2); }

await import(MODULE);

if (typeof globalThis.init === 'function') {
    try { globalThis.init(); } catch (e) { globalThis.print('init threw:', e.stack || e.message); }
}
sendFrameIfChanged();

// stdin: inbound MIDI
let inbuf = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', chunk => {
    inbuf += chunk;
    let nl;
    while ((nl = inbuf.indexOf('\n')) >= 0) {
        const line = inbuf.slice(0, nl); inbuf = inbuf.slice(nl + 1);
        if (line.startsWith('M:') && typeof globalThis.onMidiMessageInternal === 'function') {
            const d = Buffer.from(line.slice(2), 'base64');
            try { globalThis.onMidiMessageInternal(Array.from(d)); } catch {}
        }
    }
});

let timer = null;
function schedule() {
    if (timer) clearInterval(timer);
    timer = setInterval(() => {
        if (exiting) return;
        if (typeof globalThis.tick === 'function') {
            try { globalThis.tick(); } catch (e) { globalThis.print('tick threw:', e.message); }
        }
        sendFrameIfChanged();
        if (currentHz !== refreshHz) { currentHz = refreshHz; schedule(); }
    }, Math.round(1000 / refreshHz));
}
let currentHz = refreshHz;
schedule();
setInterval(() => {
    let lit = 0; for (let i = 0; i < BUFLEN; i++) lit += (fb[i] ? 1 : 0);
    process.stderr.write(`DBG clear=${DBG.clear} fill=${DBG.fill} rect=${DBG.rect} print=${DBG.print} nonzero_bytes=${lit}\n`);
}, 1000);
process.stderr.write('phhost up, refresh ' + refreshHz + ' Hz\n');

"""music.mus (LDS/Loudness OPL2 songs, docs/data_formats.md §7.1) ->
VGM 1.61 for the Genesis (YM2612 + SN76489), input of SGDK's XGM2 rescomp.

The songs are not re-implemented here: tools/lds_dump runs OpenTyrian's own
player (lds_play.c:lds_update at 69.5 Hz, loudness.c:audioCallback) and logs
the OPL2 register writes per tick.  This module replays that register stream
on a model of the OPL2 register file and re-expresses it on the Genesis chips:

* FM: each kept OPL channel becomes one YM2612 channel (1..5; channel 6 is
  the XGM2 PCM/DAC channel and is never touched).  The 2-operator OPL voice
  uses operators 1 (modulator) and 2 (carrier) of the OPN channel:
  algorithm 4 (OP1->OP2, OP3->OP4) for OPL FM connection, algorithm 7 (all
  carriers) for OPL additive connection; OP3/OP4 are silenced (TL 127, never
  keyed).  See `fm_patch` for the per-parameter mapping.
* PSG: up to 3 OPL channels on the square channels, with the OPL carrier
  envelope simulated in software per tick (`Envelope`), and one drum channel
  on the noise generator.
* Allocation: static per song, see `allocate`.

Timing is the player's: tick n starts at sample n*88200//139 (44100 Hz).
XGM2 quantizes to 60 Hz frames, so ticks (69.5 Hz) will jitter by up to one
frame on the Genesis.
"""
import math
import os
import struct
import subprocess

OPL_CLOCK = 3579545
OPL_RATE = OPL_CLOCK / 72.0                  # 49715.9 Hz sample rate
YM_CLOCK = 7670453
PSG_CLOCK = 3579545
VGM_RATE = 44100
TICK_RATE2 = 139                             # loudness.c ldsUpdate2Rate: 69.5 Hz
FM_SLOTS = 5                                 # YM2612 channels 1..5 (6 = XGM2 DAC)
PSG_TONES = 3
PSG_ATTEN_OFFSET = 2                         # PSG squares vs FM balance, 2 dB steps
# Size reduction (XGM2 blob budget): between note triggers, pitch is only
# rewritten when it moved by PITCH_CENTS or more (coarser vibrato/slides),
# TL and PSG attenuation only when they moved by TL_STEPS / ATT_STEPS (or
# reach silence), and channels whose envelope has died get no updates.
PITCH_CENTS = int(os.environ.get("MUSIC_PITCH_CENTS", 20))
TL_STEPS = int(os.environ.get("MUSIC_TL_STEPS", 3))
ATT_STEPS = int(os.environ.get("MUSIC_ATT_STEPS", 2))

OPL_OP = (0, 1, 2, 8, 9, 10, 16, 17, 18)     # lds_play.c op_table
OPL_MULT_TO_OPN = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15)

# OPL2 envelope times (YMF262/OPL2 manual, rate value 4*R with no key
# scaling): attack 96 dB -> 0 and decay/release 0 -> 96 dB, in ms, for R=1.
OPL_ATTACK_MS_R1 = 2826.24
OPL_DECAY_MS_R1 = 39280.64


def tick_sample(tick):
    """First 44100 Hz sample of LDS tick `tick` (loudness.c accumulation)."""
    return tick * 2 * VGM_RATE // TICK_RATE2


# ── Frequencies ────────────────────────────────────────────────────────────
def opl_hz(fnum, block):
    """OPL2 f-number (10 bits) + block -> Hz."""
    return fnum * OPL_RATE / (1 << (20 - block))


def opn_fnum(hz):
    """Hz -> (fnum 11 bits, block 3 bits) for the YM2612 at YM_CLOCK
    (Hz = fnum * clock/144 / 2^(21-block)); lowest block that fits keeps
    the most precision."""
    for block in range(8):
        f = round(hz * 144 * (1 << (21 - block)) / YM_CLOCK)
        if f < 2048:
            return max(f, 0), block
    return 2047, 7


def opn_hz(fnum, block):
    return fnum * YM_CLOCK / 144 / (1 << (21 - block))


def psg_period(hz):
    """Hz -> SN76489 10-bit tone period (clock / (32 * Hz)); notes below the
    PSG's 109 Hz floor are raised by octaves, 0 Hz gives the max period."""
    if hz <= 0:
        return 1023
    while PSG_CLOCK / (32 * hz) > 1023:
        hz *= 2
    return max(1, min(1023, round(PSG_CLOCK / (32 * hz))))


# ── Log reader ─────────────────────────────────────────────────────────────
class Log:
    """Output of tools/lds_dump (format in lds_dump.c): `writes[t]` is the
    list of (reg, val) of tick t, `end` the length in ticks and `loop` the
    loop-start tick or None."""

    def __init__(self, text):
        recs, self.end, self.loop = [], 0, None
        for line in text.splitlines():
            p = line.split()
            if not p or p[0] == "#":
                continue
            if p[0] == "W":
                recs.append((int(p[1]), int(p[2], 16), int(p[3], 16)))
            elif p[0] == "R":
                assert int(p[1]) == TICK_RATE2, "unexpected tick rate"
            elif p[0] == "E":
                self.end = int(p[1])
                self.loop = int(p[2]) if int(p[2]) >= 0 else None
        self.writes = [[] for _ in range(self.end)]
        for t, r, v in recs:
            self.writes[min(t, self.end - 1)].append((r, v))


def run_dump(exe, music_path, song, log_path, wav_path=None):
    args = [exe, music_path, str(song), log_path] + ([wav_path] if wav_path else [])
    subprocess.run(args, check=True)
    with open(log_path) as f:
        return Log(f.read())


# ── OPL state per tick ─────────────────────────────────────────────────────
class Voice:
    """Snapshot of one OPL channel at the end of a tick."""
    __slots__ = ("on", "trig", "fnum", "block", "mod", "car", "c0")

    def __init__(self, on, trig, fnum, block, mod, car, c0):
        self.on, self.trig, self.fnum, self.block = on, trig, fnum, block
        self.mod, self.car, self.c0 = mod, car, c0      # mod/car: regs 20,40,60,80,E0

    @property
    def hz(self):
        return opl_hz(self.fnum, self.block)


def opl_frames(log):
    """Replay the log on an OPL2 register file -> list (per tick) of 9
    Voices.  `trig` marks a key-on edge inside the tick (lds_playsound keys
    off then on in the same tick to retrigger)."""
    regs = [0] * 256
    frames = []
    for writes in log.writes:
        trig = [False] * 9
        for r, v in writes:
            if 0xB0 <= r <= 0xB8 and (v & 0x20) and not (regs[r] & 0x20):
                trig[r - 0xB0] = True
            regs[r] = v
        fr = []
        for c in range(9):
            o = OPL_OP[c]
            fr.append(Voice(bool(regs[0xB0 + c] & 0x20), trig[c],
                            regs[0xA0 + c] | (regs[0xB0 + c] & 3) << 8, (regs[0xB0 + c] >> 2) & 7,
                            tuple(regs[b + o] for b in (0x20, 0x40, 0x60, 0x80, 0xE0)),
                            tuple(regs[b + o + 3] for b in (0x20, 0x40, 0x60, 0x80, 0xE0)),
                            regs[0xC0 + c]))
        frames.append(fr)
    return frames


# ── Software envelope (PSG volume, channel loudness) ───────────────────────
def _rate(r, ksr, block, fnum):
    """Effective OPL rate 4*R + key-scale offset (keycode = block*2 + fnum
    bit 9, full with KSR, >>2 without)."""
    kc = block * 2 + ((fnum >> 9) & 1)
    return 4 * r + (kc if ksr else kc >> 2)


class Envelope:
    """Attenuation (dB, 0 = loudest) of an OPL operator, stepped once per
    tick from its registers: attack, decay to SL, then hold (EG-TYP 1) or
    keep decaying at the release rate (EG-TYP 0), release on key-off."""
    DT_MS = 2000.0 / TICK_RATE2

    def __init__(self):
        self.db, self.stage = 96.0, "off"

    def step(self, v, op):
        misc, _tl, ad, sr, _wave = op
        ar, dr, sl, rr = ad >> 4, ad & 15, sr >> 4, sr & 15
        ksr, sustain = misc & 0x10, misc & 0x20
        if v.trig:
            self.stage = "attack"
        elif not v.on and self.stage not in ("off", "release"):
            self.stage = "release"
        dt = self.DT_MS

        def slope(r, base_ms):          # dB per ms at rate r
            if r == 0:
                return 0.0
            eff = _rate(r, ksr, v.block, v.fnum)
            return 96.0 / max(base_ms * 2 ** (-(eff - 4) / 4), 0.05)
        if self.stage == "attack":
            self.db = 0.0 if ar == 15 else max(0.0, self.db - slope(ar, OPL_ATTACK_MS_R1) * dt)
            if self.db <= 0:
                self.stage = "decay"
        elif self.stage == "decay":
            target = sl * 3.0 if sl < 15 else 93.0
            self.db = min(target, self.db + slope(dr, OPL_DECAY_MS_R1) * dt)
            if self.db >= target:
                self.stage = "sustain"
        elif self.stage == "sustain" and not sustain:
            self.db += slope(rr, OPL_DECAY_MS_R1) * dt
        elif self.stage == "release":
            self.db += slope(rr, OPL_DECAY_MS_R1) * dt
        if self.db >= 96.0 and self.stage in ("sustain", "release"):
            self.db, self.stage = 96.0, "off"
        return self.db

    def level(self, op):
        """Total carrier attenuation in dB including TL (0.75 dB/step)."""
        return self.db + (op[1] & 0x3F) * 0.75


# ── Analysis and voice allocation ──────────────────────────────────────────
def is_noisy(v):
    """Drum-like OPL voice: strong self-feedback (FB >= 6) on a loud FM
    modulator, carrier that dies away (EG-TYP 0 or decay rate >= 4).  This
    is how the Tyrian songs build snares/hi-hats (e.g. song 0 ch 7/8)."""
    fb, additive = (v.c0 >> 1) & 7, v.c0 & 1
    return (fb >= 6 and not additive and (v.mod[1] & 0x3F) <= 20
            and (not (v.car[0] & 0x20) or (v.car[2] & 15) >= 4))


def analyze(frames):
    """Per OPL channel: notes, energy (sum of linear carrier amplitude per
    tick), mean pitch of notes, share of noisy notes, modulation depth."""
    stats = []
    for c in range(9):
        env, notes, noisy, energy, logf, modn = Envelope(), 0, 0, 0.0, 0.0, 0.0
        for fr in frames:
            v = fr[c]
            env.step(v, v.car)
            if v.trig:
                notes += 1
                noisy += is_noisy(v)
                logf += math.log2(max(v.hz, 1.0))
                if not v.c0 & 1:      # FM: modulation index ~ modulator level
                    modn += 10 ** (-(v.mod[1] & 0x3F) * 0.75 / 20)
            energy += 10 ** (-env.level(v.car) / 20)
        stats.append(dict(chan=c, notes=notes, energy=energy,
                          noisy=noisy / notes if notes else 0.0,
                          pitch=2 ** (logf / notes) if notes else 0.0,
                          mod=modn / notes if notes else 0.0))
    return stats


def allocate(stats):
    """Static voice allocation of one song -> {opl_chan: ("fm", slot) |
    ("psg", n) | ("noise", 3)}; unlisted channels are dropped.

    1. Noise: among channels whose notes are mostly drum-like (`is_noisy`
       >= 50%), the one with the highest notes x pitch weight (hi-hats and
       snares before kicks) goes to the PSG noise channel.
    2. The other used channels are ranked by energy (how loud and how long
       they sound); the top 8 are kept, the rest dropped.
    3. Of those, the 3 with the lowest "PSG cost" go to the squares, the
       rest to FM.  cost = energy share x (1 + 2 x modulation depth) x
       (3 if the mean pitch is below 220 Hz): quiet, pure (weakly
       modulated), high voices lose the least on a square wave; basses and
       bright FM timbres stay FM.  With 5 voices or fewer all go to FM.
    Ties break on channel number, so the result is deterministic."""
    used = [s for s in stats if s["notes"]]
    alloc = {}
    drums = [s for s in used if s["noisy"] >= 0.5]
    if drums:
        d = max(drums, key=lambda s: (s["notes"] * math.log2(max(s["pitch"], 2)), -s["chan"]))
        alloc[d["chan"]] = ("noise", 3)
        used = [s for s in used if s is not d]
    used.sort(key=lambda s: (-s["energy"], s["chan"]))
    kept = used[:FM_SLOTS + PSG_TONES]
    total = sum(s["energy"] for s in kept) or 1.0
    psg = []
    if len(kept) > FM_SLOTS:
        def cost(s):
            return (s["energy"] / total * (1 + 2 * s["mod"]) * (3 if s["pitch"] < 220 else 1), s["chan"])
        psg = sorted(kept, key=cost)[:len(kept) - FM_SLOTS]
    fm = sorted((s for s in kept if s not in psg), key=lambda s: s["chan"])
    for i, s in enumerate(fm):
        alloc[s["chan"]] = ("fm", i)
    for i, s in enumerate(sorted(psg, key=lambda s: s["chan"])):
        alloc[s["chan"]] = ("psg", i)
    return alloc


# ── FM patch mapping ───────────────────────────────────────────────────────
def opl_rate_to_opn(r, bits5=True):
    """OPL 4-bit rate -> OPN rate.  OPL rate 4R steps the envelope every
    sample at 49.7 kHz; the OPN envelope runs at 1/3 of its 53.3 kHz sample
    rate, i.e. ~6 rate units (4 per doubling) slower: OPN rate = 4R+6, so
    5-bit AR/D1R/D2R = 2R+3, 4-bit RR (rate 4RR+2) = R+1.  0 stays 0."""
    if r == 0:
        return 0
    return min(31, 2 * r + 3) if bits5 else min(15, r + 1)


def fm_operator(op, am_ok):
    """OPL operator regs (20,40,60,80,E0) -> OPN (30,40,50,60,70,80)."""
    misc, tl, ad, sr, _wave = op
    mul = OPL_MULT_TO_OPN[misc & 15]
    rs = 2 if misc & 0x10 else 0                 # KSR on: keycode/2 ~ OPL kc
    ar, dr, sl, rr = ad >> 4, ad & 15, sr >> 4, sr & 15
    d2r = 0 if misc & 0x20 else opl_rate_to_opn(rr)   # EG-TYP 0: decay on at RR
    am = 0x80 if (misc & 0x80) and am_ok else 0
    return (mul, tl & 0x3F, rs << 6 | opl_rate_to_opn(ar), am | opl_rate_to_opn(dr),
            d2r, sl << 4 | opl_rate_to_opn(rr, False))


def fm_patch(v):
    """Voice -> dict reg_offset -> value for one YM2612 channel (offsets
    relative to the channel; operator order OP1 +0, OP3 +4, OP2 +8, OP4 +C).
    FB (3 bits) is copied: OPL and OPN feedback use the same 0..4pi scale.
    Waveform select (OPL2 0xE0) has no OPN equivalent: all sine.  KSL is
    ignored.  OPL vibrato bit -> PMS 2 (~±7 cents, OPL's shallow depth),
    tremolo bit -> AM with AMS 1 (1.4 dB, OPL's is 1 dB)."""
    fb, additive = (v.c0 >> 1) & 7, v.c0 & 1
    regs = {0xB0: fb << 3 | (7 if additive else 4)}
    vib = (v.mod[0] | v.car[0]) & 0x40
    regs[0xB4] = 0xC0 | (0x10 if (v.mod[0] | v.car[0]) & 0x80 else 0) | (2 if vib else 0)
    for opofs, op in ((0x0, v.mod), (0x8, v.car)):
        mul, tl, ksar, amdr, d2r, slrr = fm_operator(op, True)
        regs[0x30 + opofs] = mul
        regs[0x40 + opofs] = tl
        regs[0x50 + opofs] = ksar
        regs[0x60 + opofs] = amdr
        regs[0x70 + opofs] = d2r
        regs[0x80 + opofs] = slrr
        regs[0x90 + opofs] = 0
    for opofs in (0x4, 0xC):                     # OP3/OP4: silent
        regs[0x30 + opofs], regs[0x40 + opofs], regs[0x80 + opofs] = 1, 127, 0xFF
    return regs


# ── VGM writer ─────────────────────────────────────────────────────────────
class VgmWriter:
    """Command stream with register caches (only changed values are
    written) and 44100 Hz waits."""

    def __init__(self):
        self.out = bytearray()
        self.ym = {}                 # (port, reg) -> value
        self.psg = {}                # key -> value
        self.samples = 0
        self.loop_ofs = None
        self.loop_samples = 0

    def ym_write(self, port, reg, val, force=False):
        if force or self.ym.get((port, reg)) != val:
            self.ym[(port, reg)] = val
            self.out += bytes((0x52 + port, reg, val))

    def psg_write(self, key, *data):
        if self.psg.get(key) != data:
            self.psg[key] = data
            for b in data:
                self.out += bytes((0x50, b))

    def wait(self, n):
        self.samples += n
        while n > 0:
            if n == 735:
                self.out.append(0x62); n = 0
            elif n == 882:
                self.out.append(0x63); n = 0
            elif n <= 16:
                self.out.append(0x70 + n - 1); n = 0
            else:
                k = min(n, 65535)
                self.out += bytes((0x61, k & 255, k >> 8)); n -= k

    def mark_loop(self):
        """Loop point here; caches are cleared so the state at the loop start
        is written in full (the chips hold the end-of-song state on a jump)."""
        self.loop_ofs, self.loop_samples = len(self.out), self.samples
        self.ym.clear()
        self.psg.clear()

    def finish(self):
        self.out.append(0x66)
        hdr = bytearray(0x100)
        struct.pack_into("<4sIIII", hdr, 0, b"Vgm ", 0x100 + len(self.out) - 4, 0x161, PSG_CLOCK, 0)
        struct.pack_into("<I", hdr, 0x18, self.samples)
        if self.loop_ofs is not None:
            struct.pack_into("<II", hdr, 0x1C, 0x100 + self.loop_ofs - 0x1C, self.samples - self.loop_samples)
        struct.pack_into("<HB", hdr, 0x28, 0x0009, 16)      # SN76489 (Sega VDP PSG)
        struct.pack_into("<I", hdr, 0x2C, YM_CLOCK)
        struct.pack_into("<I", hdr, 0x34, 0x100 - 0x34)
        return bytes(hdr) + bytes(self.out)


def _key_code(slot):
    port, ch = divmod(slot, 3)
    return ch | port << 2


def noise_rate(hz):
    """Noise shift rate from pitch: 0 = clock/512 (highest) .. 2 = /2048."""
    return 0 if hz >= 1000 else 1 if hz >= 250 else 2


def psg_atten(db):
    return min(15, max(0, round(db / 2.0) + PSG_ATTEN_OFFSET))


def _moved(old_hz, hz):
    return old_hz is None or abs(1200 * math.log2(max(hz, 1e-3) / max(old_hz, 1e-3))) >= PITCH_CENTS


def convert(log, alloc=None):
    """Log -> (vgm bytes, alloc, stats)."""
    frames = opl_frames(log)
    stats = analyze(frames)
    if alloc is None:
        alloc = allocate(stats)
    w = VgmWriter()
    envs = [Envelope() for _ in range(9)]
    keyed, last_hz = {}, {}
    lfo = any(((v.mod[0] | v.car[0]) & 0xC0) for fr in frames for v in fr if v.trig)
    for slot in range(FM_SLOTS):                 # silence OP3/OP4 up front
        w.out += bytes((0x52, 0x28, _key_code(slot)))
    for t, fr in enumerate(frames):
        if t == log.loop:
            w.mark_loop()
            keyed.clear()
            last_hz.clear()
        if t == 0 or t == log.loop:
            w.ym_write(0, 0x22, 0x09 if lfo else 0x00)
            w.ym_write(0, 0x27, 0x00)
        for c in range(9):
            v = fr[c]
            envs[c].step(v, v.car)
            kind = alloc.get(c)
            if kind is None:
                continue
            what, n = kind
            silent = envs[c].stage == "off" and not v.trig
            if what == "fm":
                port, ch = divmod(n, 3)
                kc = _key_code(n)
                if v.trig and keyed.get(n):
                    w.out += bytes((0x52, 0x28, kc))
                    keyed[n] = False
                if not silent:
                    for ofs, val in fm_patch(v).items():
                        old = w.ym.get((port, ofs + ch))
                        if (ofs & 0xF0) == 0x40 and not v.trig and old is not None \
                                and abs(old - val) < TL_STEPS and val != 127:
                            continue
                        w.ym_write(port, ofs + ch, val)
                    if v.trig or _moved(last_hz.get(c), v.hz):
                        last_hz[c] = v.hz
                        fnum, block = opn_fnum(v.hz)
                        hi = block << 3 | fnum >> 8
                        if w.ym.get((port, 0xA4 + ch)) != hi or w.ym.get((port, 0xA0 + ch)) != fnum & 255:
                            w.ym_write(port, 0xA4 + ch, hi, force=True)
                            w.ym_write(port, 0xA0 + ch, fnum & 255, force=True)
                want = v.on
                if keyed.get(n) != want:
                    w.out += bytes((0x52, 0x28, (0x30 if want else 0) | kc))
                    keyed[n] = want
            else:
                att = psg_atten(envs[c].level(v.car)) if not silent else 15
                key = ("v", n if what == "psg" else 3)
                old = w.psg.get(key)
                if not v.trig and old is not None and att != 15 and abs((old[0] & 15) - att) < ATT_STEPS:
                    att = old[0] & 15
                if what == "psg":
                    if not silent and (v.trig or _moved(last_hz.get(c), v.hz)):
                        last_hz[c] = v.hz
                        p = psg_period(v.hz)
                        w.psg_write(("t", n), 0x80 | n << 5 | (p & 15), (p >> 4) & 0x3F)
                    w.psg_write(key, 0x90 | n << 5 | att)
                else:
                    if v.trig:
                        w.psg.pop(("n",), None)          # rewrite resets the LFSR: new hit
                        w.psg_write(("n",), 0xE4 | noise_rate(v.hz))
                    w.psg_write(key, 0xF0 | att)
        w.wait(tick_sample(t + 1) - tick_sample(t))
    return w.finish(), alloc, stats


# ── VGM parsing (tests, stats) ─────────────────────────────────────────────
def vgm_commands(data):
    """Yield (cmd, args bytes) of a VGM data stream (subset this writer
    emits; raises on anything else)."""
    pos = 0x34 + struct.unpack_from("<I", data, 0x34)[0]
    while pos < len(data):
        c = data[pos]
        if c in (0x52, 0x53):
            yield c, data[pos + 1:pos + 3]; pos += 3
        elif c == 0x50:
            yield c, data[pos + 1:pos + 2]; pos += 2
        elif c == 0x61:
            yield c, data[pos + 1:pos + 3]; pos += 3
        elif c in (0x62, 0x63) or 0x70 <= c <= 0x7F:
            yield c, b""; pos += 1
        elif c == 0x66:
            yield c, b""; return
        else:
            raise ValueError(f"unexpected VGM command {c:#x} at {pos:#x}")


def default_dump_exe():
    return os.path.join(os.path.dirname(__file__), "..", "..", "out", "lds_dump")

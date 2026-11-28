#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert a single-SID tune to PMD86 FM/SSG, with D418 digis in P86 PCM.

Requires Python 3.10+. Build the cycle capture helper with --setup first.
No MML compiler is needed: this program writes the PMD binary directly.
"""
from __future__ import annotations

import argparse
import bisect
from array import array
from dataclasses import dataclass
import hashlib
from functools import lru_cache
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(__file__).with_suffix("")
BUILD = ROOT / "build" / "sid2pmd"
OPNA_CLOCK = 7_987_200
TIMER_B = 187
# OPNA timer B advances once per 2304 input clocks (24 operators, prescale 6,
# and the timer's divide by 16). Choose the closest raw period to 50 Hz.
TICK = 2304 * (256 - TIMER_B) / OPNA_CLOCK
PCM_RATE = 16_537.5  # PMD86 o5g, from the original driver's TUNEDATA.INC.
FNUM = (0x26A, 0x28F, 0x2B6, 0x2DF, 0x30B, 0x339,
        0x36A, 0x39E, 0x3D5, 0x410, 0x44E, 0x48F)
ADSR_PERIODS = (9, 32, 63, 95, 149, 220, 267, 313, 392, 977,
                1954, 3126, 3907, 11720, 19532, 31251)
ADSR_THRESHOLDS = {255: 1, 93: 2, 54: 4, 26: 8, 14: 16, 6: 30, 0: 1}


class ConversionError(ValueError):
    pass


@dataclass
class Trace:
    clock: int
    cycles: int
    initial: bytes
    times: array
    registers: bytearray
    values: bytearray

    @property
    def duration(self):
        return self.cycles / self.clock


def read_trace(path: Path) -> Trace:
    times, registers, values = array("Q"), bytearray(), bytearray()
    with path.open("rb") as f:
        head = f.read(52)
        if len(head) != 52 or head[:4] != b"S2PT":
            raise ConversionError("Invalid SID capture header")
        version, clock, cycles = struct.unpack_from("<IIQ", head, 4)
        if version != 1 or clock not in (985248, 1022727) or not 0 < cycles <= clock * 900:
            raise ConversionError("Unsupported SID capture version, clock or duration")
        previous = 0
        while block := f.read(10 * 65536):
            if len(block) % 10:
                raise ConversionError("Truncated SID capture")
            for time, reg, value in struct.iter_unpack("<QBB", block):
                if time < previous or reg > 31:
                    raise ConversionError("Invalid SID capture event")
                previous = time
                if time < cycles:
                    times.append(time); registers.append(reg); values.append(value)
    return Trace(clock, cycles, head[20:52], times, registers, values)


def sid_metadata(path: Path, song: int | None, clock: str):
    with path.open("rb") as f:
        data = f.read(124)
    if len(data) < 118 or data[:4] not in (b"PSID", b"RSID"):
        raise ConversionError("Input must be a PSID or RSID .sid file")
    version = struct.unpack_from(">H", data, 4)[0]
    if version not in (1, 2, 3, 4):
        raise ConversionError(f"Unsupported SID version {version}")
    songs, default = struct.unpack_from(">HH", data, 14)
    song = default if song is None else song
    if not 1 <= song <= songs:
        raise ConversionError(f"Subtune must be between 1 and {songs}")
    flags = struct.unpack_from(">H", data, 118)[0] if version >= 2 and len(data) >= 124 else 0
    if version >= 2 and len(data) < 124:
        raise ConversionError("Truncated SID v2+ header")
    if version >= 3 and (data[122] or data[123]):
        raise ConversionError("Multi-SID tunes are not supported by this converter")
    if clock == "auto":
        clock = "ntsc" if (flags >> 2) & 3 == 2 else "pal"
    strings = [data[p:p+32].split(b"\0")[0].decode("latin1") for p in (22, 54, 86)]
    return dict(title=strings[0], composer=strings[1], copyright=strings[2],
                subtune=song, songs=songs, clock=clock, format=data[:4].decode(),
                sid_model="8580" if (flags >> 4) & 3 == 2 else "6581")


def volume_events(trace):
    times, levels = array("Q", [0]), bytearray([trace.initial[24] & 15])
    for t, r, v in zip(trace.times, trace.registers, trace.values):
        if r == 24 and (v & 15) != levels[-1]:
            times.append(t); levels.append(v & 15)
    return times, levels


def digi_regions(trace, mode, ticks):
    """Identify high-rate volume changes; slow fades remain master volume."""
    if mode == "off":
        return [], [False] * ticks
    if mode == "force":
        return [(0, ticks)], [True] * ticks
    times, _ = volume_events(trace)
    counts = [0] * ticks
    for a, b in zip(times, times[1:]):
        if 0 < b - a <= trace.clock / 200:
            counts[min(ticks - 1, int(b / trace.clock / TICK))] += 1
    mask = [False] * ticks
    for i, count in enumerate(counts):
        if count >= 3:
            for j in range(max(0, i-1), min(ticks, i+2)):
                mask[j] = True
    regions = []
    start = None
    for i, active in enumerate(mask + [False]):
        if active and start is None:
            start = i
        elif not active and start is not None:
            regions.append((start, i)); start = None
    # Keep repeated drum bursts usable in P86's 256-slot table. Joining the
    # shortest intervening gaps costs less PCM than rendering the entire tune.
    while len(regions) > 256:
        join = min(range(len(regions)-1), key=lambda n: regions[n+1][0]-regions[n][1])
        regions[join:join+2] = [(regions[join][0], regions[join+1][1])]
    return regions, mask


def make_digi_samples(trace, regions, gain):
    """Area-average D418's DAC signal, DC-block, and fade region boundaries."""
    times, levels = volume_events(trace)
    samples = []
    event = 0
    step = trace.clock / PCM_RATE
    alpha = math.exp(-2 * math.pi * 16 / PCM_RATE)
    for first, last in regions:
        start, finish = first * TICK * trace.clock, min(last * TICK * trace.clock, trace.cycles)
        while event + 1 < len(times) and times[event + 1] <= start:
            event += 1
        count = max(1, math.ceil((finish - start) / step))
        out = bytearray(count)
        dc = float(levels[event])
        fade = min(int(.002 * PCM_RATE), count // 2)
        for i in range(count):
            lo, hi = start + i * step, min(start + (i+1) * step, finish)
            at, total = lo, 0.
            while event + 1 < len(times) and times[event+1] < hi:
                boundary = max(at, float(times[event+1]))
                total += (boundary - at) * levels[event]
                at = boundary; event += 1
            total += (hi-at) * levels[event]
            level = total / max(1e-12, hi-lo)
            dc = alpha * dc + (1-alpha) * level
            value = (level-dc) * 12 * gain
            if fade:
                value *= min(1., i/fade, (count-1-i)/fade)
            out[i] = max(-128, min(127, round(value))) & 255
        samples.append(bytes(out))
    return samples


def make_p86(samples):
    if not 1 <= len(samples) <= 256:
        raise ConversionError("P86 bank requires 1..256 samples")
    total = 0x610 + sum(map(len, samples))
    if total > 0xFFFFFF:
        raise ConversionError("PCM bank exceeds P86's 24-bit size limit")
    header = bytearray(b"PCM86 DATA\n\0" + b"\x11" + total.to_bytes(3, "little"))
    header.extend(bytes(256 * 6))
    offset = 0x610
    for i, sample in enumerate(samples):
        # P86DRV stores absolute file offsets (including the 0x610 header).
        header[16+i*6:22+i*6] = offset.to_bytes(3, "little") + len(sample).to_bytes(3, "little")
        offset += len(sample)
    return bytes(header) + b"".join(samples)


class Envelope:
    """SID byte envelope with retained rate/exponential counters and delay bug.

    Advance between register writes, not only at score frames. The analog
    output and one-cycle pipeline details remain approximations.
    """
    def __init__(self, clock=985248):
        self.clock = clock
        self.value = 0
        self.rate_counter = 0
        self.exponential_counter = 0
        self.exponential_period = 1
        self.fraction = 0.
        self.hold_zero = True
        self.stage = "release"
        self.gate = False

    @property
    def level(self):
        return self.value / 255

    def advance(self, seconds, ad, sr):
        elapsed = max(0., seconds) * self.clock + self.fraction
        cycles = int(elapsed)
        self.fraction = elapsed - cycles
        while cycles:
            index = ad >> 4 if self.stage == "attack" else ad & 15 if self.gate else sr & 15
            period = ADSR_PERIODS[index]
            distance = period - self.rate_counter
            if distance <= 0:
                distance += 0x8000  # Changing to a faster rate can miss the compare.
            if cycles < distance:
                self.rate_counter = (self.rate_counter + cycles) & 0x7fff
                break
            if self.stage != "attack" and (self.hold_zero or self.gate and self.value == (sr >> 4)*17):
                shots, remainder = divmod(cycles-distance, period)
                self.rate_counter = remainder
                self.exponential_counter = (self.exponential_counter+shots+1) % self.exponential_period
                break
            cycles -= distance
            self.rate_counter = 0
            if self.stage == "attack":
                self.exponential_counter = 0
                self.value = (self.value + 1) & 255
                if self.value == 255:
                    self.stage = "decay"
            else:
                self.exponential_counter += 1
                if self.exponential_counter < self.exponential_period:
                    continue
                self.exponential_counter = 0
                if not self.hold_zero and (not self.gate or self.value != (sr >> 4) * 17):
                    self.value = (self.value - 1) & 255
            if self.value in ADSR_THRESHOLDS:
                self.exponential_period = ADSR_THRESHOLDS[self.value]
            if self.value == 0:
                self.hold_zero = True

    def control(self, gate):
        if gate and not self.gate:
            self.stage = "attack"
            self.hold_zero = False
        elif not gate:
            self.stage = "release"
        self.gate = gate


def pitch(freq, clock, anchor=None):
    hz = freq * clock / 16777216
    if hz <= 0:
        return 0x0F, 0
    midi = max(24, min(119, round(69 + 12 * math.log2(hz / 440))))
    block, note = midi // 12 - 2, midi % 12
    if anchor is not None and anchor != 0x0f:
        old_block = anchor >> 4
        candidate = hz * (144 * 1048576) / OPNA_CLOCK / (2 ** old_block)
        if 300 <= candidate <= 1900:
            block, note = old_block, anchor & 15
    wanted = hz * (144 * 1048576) / OPNA_CLOCK / (2 ** block)
    return (block << 4) | note, max(-32768, min(32767, round(wanted - FNUM[note])))


def attenuation(amplitude, maximum=110):
    return 0 if amplitude <= .0001 else max(0, min(maximum, round(maximum + 20 * math.log10(amplitude) / .75)))


class Instruments:
    def __init__(self):
        self.ids = {}
        self.data = []

    def get(self, wave, pulse=2048, cutoff=2047, filter_type=0, routed=False):
        # Stable carrier layout; PWM/filter motion uses live TL writes instead
        # of allocating/re-keying a new instrument at each sweep step.
        harmonics = (1, 3, 5, 7) if wave & 0x70 == 0x10 else (1, 2, 3, 4)
        key = harmonics
        if key in self.ids:
            return self.ids[key]
        if len(self.data) >= 256:
            raise ConversionError("Too many FM instruments")
        # Four parallel carriers. Fast attack, constant sustain; software ADSR
        # drives FD volume commands so releases survive SID gate-off.
        # PMD patch slots are stored in register order 1,3,2,4.
        order = (0, 2, 1, 3)
        patch = bytes(harmonics[n] for n in order) + bytes([127]*4) + bytes([31]*4) + bytes(8) + bytes([15]*4) + b"\x07"
        instrument = len(self.data)
        self.ids[key] = instrument; self.data.append(patch)
        return instrument


@lru_cache(maxsize=8192)
def waveform_spectrum(wave, duty, source_ratio):
    """Four partials of PWM/combined/ring/sync waveforms, at a stable FM base.

    Combined waveforms use a digital AND approximation; the real SID's
    model-dependent analog bit coupling is deliberately not claimed here.
    """
    harmonics = (1, 3, 5, 7) if wave & 0x70 == 0x10 else (1, 2, 3, 4)
    duty = duty / 128
    ratio = source_ratio / 16
    if not wave & 6 and wave & 0x70 in (0x10, 0x20, 0x40):
        if wave & 0x10:
            return harmonics, tuple(1 / (n*n) for n in harmonics)
        if wave & 0x20:
            return harmonics, tuple(.8 / n for n in harmonics)
        return harmonics, tuple(abs(math.sin(math.pi*n*duty)) / n for n in harmonics)
    values = []
    for i in range(128):
        phase = (i+.5)/128
        # With sync, the voice fundamental is anchored to its source oscillator.
        oscillator = (phase * ratio) % 1 if wave & 2 and ratio else phase
        bits = 4095
        if wave & 0x10:
            triangle = 1 - abs(2*oscillator-1)
            if wave & 4 and int(phase*ratio*2) & 1:
                triangle = 1-triangle
            bits &= int(triangle*4095)
        if wave & 0x20: bits &= int(oscillator*4095)
        if wave & 0x40: bits &= 4095 if oscillator < duty else 0
        values.append(bits / 4095)
    amplitudes = []
    for harmonic in harmonics:
        real = sum(v*math.cos(2*math.pi*harmonic*(i+.5)/128) for i,v in enumerate(values))
        imag = sum(v*math.sin(2*math.pi*harmonic*(i+.5)/128) for i,v in enumerate(values))
        amplitudes.append(min(1., 4*math.hypot(real,imag)/128))
    return harmonics, tuple(amplitudes)


def filter_response(hz, cutoff, resonance, modes, model="6581"):
    """Complex state-variable response: mode sums also reproduce notch sweeps."""
    position = cutoff / 2047
    # A smooth approximate calibration; real 6581 cutoff varies between chips.
    center = 30 + 12500*position if model == "8580" else 180 + 11800*position**1.65
    ratio = hz / max(1., center)
    q = .707 + (resonance/15)**1.4 * (7.5 if model == "8580" else 5.5)
    denominator = complex(1-ratio*ratio, ratio/q)
    response = 0j
    if modes & 0x10: response += 1 / denominator
    if modes & 0x20: response += complex(0, ratio) / denominator
    if modes & 0x40: response += -ratio*ratio / denominator
    return min(6., abs(response))


def operator_levels(wave, pulse, hz, source_hz, amplitude, cutoff, resonance, modes, routed, model):
    ratio = hz/source_hz if wave & 2 and source_hz else source_hz/max(1., hz)
    harmonics, amplitudes = waveform_spectrum(wave & 0x76, round(pulse/4095*128),
                                            max(0, min(512, round(ratio*16))))
    fundamental = source_hz if wave & 2 and source_hz else hz
    levels = []
    for harmonic, partial in zip(harmonics, amplitudes):
        frequency = fundamental*harmonic
        gain = filter_response(frequency, cutoff, resonance, modes, model) if routed else 1.
        strength = amplitude * partial * gain
        if frequency > 22050 or strength < 1e-6:
            levels.append(127)
        else:
            levels.append(max(0, min(127, round(18 - 20*math.log10(strength)/.75))))
    return tuple(levels)


def voice_states(trace, mask, ticks, instruments, model="6581"):
    regs = bytearray(trace.initial)
    envelopes = [Envelope(trace.clock) for _ in range(3)]
    generations = [0, 0, 0]
    for voice, env in enumerate(envelopes):
        env.control(bool(regs[voice*7+4] & 1))
    streams = [[] for _ in range(6)]
    event = 0
    now = 0.
    master = regs[24] & 15
    last_tone = [(0x30, 0)] * 3
    pitch_generation = [-1]*3
    features = set()
    for tick in range(ticks):
        end = min((tick+1) * TICK, trace.duration)
        while event < len(trace.times) and trace.times[event] / trace.clock <= end:
            at = trace.times[event] / trace.clock
            for voice, env in enumerate(envelopes):
                env.advance(max(0, at-now), regs[voice*7+5], regs[voice*7+6])
            now = at
            reg, value = trace.registers[event], trace.values[event]
            if reg in (4, 11, 18):
                voice = (reg-4)//7
                gate = bool(value & 1)
                if gate and not (regs[reg] & 1):
                    generations[voice] += 1
                envelopes[voice].control(gate)
                if value & 2: features.add("oscillator sync follows the source pitch with a resampled harmonic spectrum")
                if value & 4: features.add("ring modulation follows the source oscillator ratio in the waveform spectrum")
                if bin(value >> 4).count("1") > 1: features.add("combined waveforms use a sampled digital-AND spectrum")
            regs[reg] = value
            if reg == 24 and not mask[tick]:
                master = value & 15
            event += 1
        for voice, env in enumerate(envelopes):
            offset = voice*7
            env.advance(max(0, end-now), regs[offset+5], regs[offset+6])
            wave = regs[offset+4]
            amp = env.level * (master/15)
            if wave & 8 or not wave & 0xF0: amp = 0.
            freq = regs[offset] | (regs[offset+1] << 8)
            hz = freq * trace.clock / 16777216
            source = ((voice+2)%3)*7
            source_freq = regs[source] | (regs[source+1] << 8)
            source_hz = source_freq * trace.clock / 16777216
            effective_freq = source_freq if wave & 2 and source_freq else freq
            if effective_freq:
                anchor = last_tone[voice][0] if pitch_generation[voice] == generations[voice] else None
                last_tone[voice] = pitch(effective_freq, trace.clock, anchor)
                pitch_generation[voice] = generations[voice]
            note, detune = last_tone[voice]
            cutoff = (regs[21] & 7) | (regs[22] << 3)
            routed = bool(regs[23] & (1 << voice))
            if voice == 2 and regs[24] & 0x80 and not routed: amp = 0.
            if routed:
                features.add("routed SID filter cutoff, resonance and LP/BP/HP combinations drive live FM operator levels")
                if not regs[24] & 0x70: amp = 0.
            levels = operator_levels(wave, regs[offset+2] | ((regs[offset+3]&15)<<8),
                                     hz, source_hz, amp, cutoff, regs[23] >> 4,
                                     regs[24] & 0x70, routed, model)
            fmvol = 127 if wave & 0x70 and min(levels) < 127 else 0
            inst = instruments.get(wave, regs[offset+2] | ((regs[offset+3]&15)<<8), cutoff, regs[24], routed)
            streams[voice].append((note if fmvol else 0x0F, detune, inst, fmvol, generations[voice], levels))
            noise_amp = amp
            if routed and wave & 0x80:
                noise_amp *= math.sqrt(sum(filter_response(f, cutoff, regs[23] >> 4, regs[24]&0x70, model)**2
                                          for f in (125, 250, 500, 1000, 2000, 4000, 8000, 16000))/8)
            noisevol = max(0, min(15, round(15 + 20 * math.log10(max(noise_amp, 1e-8))/3))) if wave & 0x80 else 0
            # The YM2608 has one noise frequency shared by all SSG channels.
            period = max(1, min(31, round(2_000_000 / (16 * max(1, freq * trace.clock / 16777216)))))
            if routed and regs[24] & 0x30:
                center = 180+11800*(cutoff/2047)**1.65 if model == "6581" else 30+12500*cutoff/2047
                period = max(period, min(31, round(125000/max(1., center))))
            streams[voice+3].append((0x30 if noisevol else 0x0F, period, 0, noisevol, generations[voice]))
        now = end
    if any(s[3] for stream in streams[3:] for s in stream):
        features.add("SID noise uses SSG noise; its pitch is shared between active noise voices")
    return streams, sorted(features)


def emit_note(out, note, length, continuous=False):
    while length:
        chunk = min(length, 255)
        if continuous and note != 0x0F:
            out.append(0xFB)
        out.extend((note, chunk))
        length -= chunk
        continuous = note != 0x0F


def encode_voice(states, noise=False):
    if not any(state[3] for state in states):
        return b"\x80"
    out = bytearray((0xFE, 0, 0xF1, 0))
    if noise:
        out.extend((0xED, 0x38, 0xF0, 0, 0, 0, 0))
    else:
        out.extend((0xEC, 3))
    previous = None
    start = 0
    while start < len(states):
        state = states[start]
        end = start+1
        while end < len(states) and states[end] == state:
            end += 1
        note, detune, instrument, volume, generation = state[:5]
        if previous is None or previous[3] != volume:
            out.extend((0xFD, volume))
        if previous is None or previous[1] != detune:
            if noise: out.extend((0xEE, detune))
            else: out.append(0xFA); out.extend(struct.pack("<h", detune))
        patch_changed = not noise and (previous is None or previous[2] != instrument)
        if patch_changed:
            out.extend((0xFF, instrument))
        if not noise and len(state) > 5:
            old_levels = previous[5] if previous is not None and len(previous) > 5 and not patch_changed else (None,)*4
            groups = {}
            for operator, (level, old) in enumerate(zip(state[5], old_levels)):
                if level != old:
                    groups[level] = groups.get(level, 0) | (1 << operator)
            for level, slots in groups.items():
                out.extend((0xB8, slots, level))
        tie = previous is not None and previous[0] != 0x0F and note != 0x0F and previous[4] == generation
        # A changed patch needs a new key-on to apply its operator settings.
        if not noise and previous is not None and previous[2] != instrument:
            tie = False
        emit_note(out, note, end-start, tie)
        previous, start = state, end
    out.extend((0x0F, 1, 0x80))  # Explicit final key-off, including tied notes.
    return bytes(out)


def encode_pcm(regions, ticks):
    if not regions:
        return b"\x80"
    out = bytearray((0xFE, 0, 0xFD, 96, 0xEC, 3))
    position = 0
    for instrument, (first, last) in enumerate(regions):
        emit_note(out, 0x0F, first-position)
        out.extend((0xFF, instrument))
        emit_note(out, 0x47, last-first)  # o5g: native 16.5375kHz.
        position = last
    emit_note(out, 0x0F, ticks-position)
    out.extend((0x0F, 1, 0x80))
    return bytes(out)


class CompressedTrack(bytes):
    def __new__(cls, data, relocations):
        result = super().__new__(cls, data)
        result.relocations = relocations
        return result


def compress_track(track):
    """Replace exact adjacent command patterns with relocatable PMD loops."""
    lengths = {0xFE: 1, 0xF1: 1, 0xEC: 1, 0xED: 1, 0xF0: 4,
               0xFD: 1, 0xFA: 2, 0xFF: 1, 0xEE: 1, 0xFC: 1, 0xFB: 0, 0xB8: 2, 0x80: 0}
    tokens, position = [], 0
    while position < len(track):
        cmd = track[position]
        length = 2 if cmd < 0x80 else 1 + lengths[cmd]
        tokens.append(track[position:position+length]); position += length
    locations = {}
    for i, token in enumerate(tokens):
        locations.setdefault(token, []).append(i)
    out, relocations, i = bytearray(), [], 0
    while i < len(tokens):
        best = None
        # Find repeated blocks on command boundaries. Never include track-end.
        choices = locations[tokens[i]]
        choice = bisect.bisect_right(choices, i)
        for j in choices[choice:choice+96]:
            width = j-i
            if width > 1024 or i+width*2 >= len(tokens): break
            block = tokens[i:j]
            if b"\x80" in block or tokens[j:j+width] != block: continue
            size = sum(map(len, block))
            repeats = 2
            while repeats < 255 and tokens[i+repeats*width:i+(repeats+1)*width] == block:
                repeats += 1
            saving = size*(repeats-1)-8
            if saving > 0 and (best is None or saving > best[0]):
                best = (saving, width, repeats, b"".join(block))
        if best is None:
            out.extend(tokens[i]); i += 1
        else:
            _, width, repeats, block = best
            start = len(out)
            out.extend(b"\xf9\0\0"); out.extend(block)
            end = len(out)
            out.extend(bytes((0xF8, repeats, 0, 0, 0)))
            relocations.extend(((start+1, end+1), (end+3, start+1)))
            i += width*repeats
    return CompressedTrack(out, relocations)


def make_pmd(tracks, patches, pcmname, title, composer):
    if len(tracks) != 11:
        raise ConversionError("PMD requires eleven parts")
    data = bytearray(26)
    for part, track in enumerate(tracks):
        if len(data) > 65535:
            raise ConversionError("PMD score exceeds 64 KiB; shorten --seconds")
        track_start = len(data)
        struct.pack_into("<H", data, part*2, track_start)
        data.extend(track)
        for position, target in getattr(track, "relocations", []):
            if track_start+target > 65535:
                raise ConversionError("PMD score exceeds 64 KiB; shorten --seconds")
            struct.pack_into("<H", data, track_start+position, track_start+target)
    # Rhythm is unused; still provide a valid subroutine table.
    struct.pack_into("<H", data, 22, len(data))
    data.extend(struct.pack("<H", len(data)+2)); data.append(0xFF)
    marker = len(data)
    data.extend(b"\0\0\x48\xfe")
    struct.pack_into("<H", data, 24, len(data))
    for index, patch in enumerate(patches):
        if len(patch) != 25:
            raise ConversionError("Invalid FM patch")
        data.append(index); data.extend(patch)
    data.extend(b"\0\xff")
    pointers = []
    for string in ("", "", pcmname, title, composer, "sid2pmd"):
        pointers.append(len(data))
        data.extend(string.replace("\0", " ").encode("cp932", "replace") + b"\0")
    if len(data) + 14 > 65535:
        raise ConversionError("PMD score exceeds 64 KiB; shorten --seconds")
    struct.pack_into("<H", data, marker, len(data))
    data.extend(struct.pack("<7H", *pointers, 0))
    return b"\0" + bytes(data)


def reduce_voice(states, precision, noise=False):
    """Error-bounded sample/hold; keep gate edges, rests and patch changes.

    Each parameter has its own retained value, so a filter update does not
    redundantly emit a nearly unchanged pitch or reset the amplitude curve.
    """
    if not states: return []
    result = [states[0]]
    retained = states[0]
    for state in states[1:]:
        edge = state[4] != retained[4] or state[2] != retained[2] or ((state[0] == 15) != (retained[0] == 15))
        if edge:
            retained = state
        elif state[0] != 15:
            updated = list(retained)
            if noise:
                if abs(state[1]-retained[1]) >= max(1, precision//3): updated[1] = state[1]
                if abs(state[3]-retained[3]) >= max(1, precision//3): updated[3] = state[3]
            else:
                fnum = FNUM[state[0]&15] + state[1]
                old = (FNUM[retained[0]&15] + retained[1]) * 2**((retained[0]>>4)-(state[0]>>4))
                cents = abs(1200*math.log2(max(1,fnum)/max(1,old)))
                if cents >= max(.5, precision*.8): updated[0:2] = state[0:2]
                if len(state) > 5:
                    updated[5] = tuple(new if abs(new-old) >= precision else old
                                       for new,old in zip(state[5],retained[5]))
                elif abs(state[3]-retained[3]) >= precision: updated[3] = state[3]
            retained = tuple(updated)
        result.append(retained)
    return result


def convert_trace(trace, metadata, bankname, digi="auto", gain=1., resolution=1, fit="auto", rendered=None):
    ticks = math.ceil(trace.duration / TICK)
    regions, mask = digi_regions(trace, digi, ticks)
    samples = make_digi_samples(trace, regions, gain)
    instruments = Instruments()
    if rendered is None:
        voices, features = voice_states(trace, mask, ticks, instruments, metadata.get("sid_model", "6581"))
    else:
        voices, features, instruments = rendered
    precisions = [resolution]
    if fit == "auto": precisions += [n for n in (2, 3, 4, 6, 8, 12, 16, 24, 32) if n > resolution]
    score = None
    for precision in precisions:
        reduced = [reduce_voice(stream, precision, n >= 3) for n,stream in enumerate(voices)]
        tracks = [encode_voice(s) for s in reduced[:3]] + [b"\x80"]*3
        tracks += [encode_voice(s, noise=True) for s in reduced[3:]]
        tracks += [encode_pcm(regions, ticks), b"\x80"]
        tracks[0] = bytes((0xFC, TIMER_B)) + tracks[0]
        original_size = sum(map(len, tracks))
        tracks = [compress_track(track) for track in tracks]
        try:
            candidate = make_pmd(tracks, instruments.data, bankname if samples else "", metadata["title"], metadata["composer"])
        except ConversionError as error:
            if "64 KiB" not in str(error): raise
            continue
        if len(candidate) <= 40*1024:
            score = candidate
            break
    if score is None:
        raise ConversionError("Note events exceed PMD86's 40 KiB buffer; shorten --seconds")
    report = dict(metadata, duration_seconds=trace.duration, pmd_duration_seconds=(ticks+1)*TICK,
                  pmd_bytes=len(score), uncompressed_track_bytes=original_size,
                  compressed_track_bytes=sum(map(len, tracks)), tick_seconds=TICK, timer_b=TIMER_B,
                  control_interval_seconds=TICK, resolution=precision, requested_resolution=resolution,
                  reduction="adaptive; gate edges preserved", fit=fit,
                  operator_tolerance_db=.75*precision, pitch_tolerance_cents=max(.5, precision*.8),
                  fm_instruments=len(instruments.data), digi_mode=digi,
                  digi_regions=[dict(start_seconds=a*TICK, end_seconds=min(b*TICK, trace.duration),
                                     pcm_bytes=len(sample)) for (a,b), sample in zip(regions, samples)],
                  pcm_rate=PCM_RATE, pcm_bytes=sum(map(len, samples)),
                  pcm_ems_pages=max(1, math.ceil((sum(map(len, samples))+0x610)/16384)), approximations=features,
                  limitations=[f"Source controls sampled at roughly {1/TICK:.1f} Hz; adaptive FM level tolerance {precision*.75:.2f} dB",
                               "SID rate-counter ADSR is mapped to quantized FM levels, including release and rate-change delay",
                               "Four FM partials approximate SID spectra and resonant filter modes; analog distortion and phase are not reproduced",
                               "Only D418 volume-register digis are routed to PCM; oscillator/PWM digis are not decoded"])
    if not any(state[3] for stream in voices for state in stream) and not samples:
        report["warnings"] = ["No audible voice or digi activity detected; check subtune, ROMs or warmup"]
    return score, make_p86(samples) if samples else None, report


def trace_section(trace, first, last):
    begin = round(first*TICK*trace.clock)
    finish = min(trace.cycles, round(last*TICK*trace.clock))
    initial = bytearray(trace.initial)
    start_event = bisect.bisect_left(trace.times, begin)
    stop_event = bisect.bisect_left(trace.times, finish)
    for reg,value in zip(trace.registers[:start_event], trace.values[:start_event]):
        initial[reg] = value
    return Trace(trace.clock, finish-begin, bytes(initial),
                 array('Q', (t-begin for t in trace.times[start_event:stop_event])),
                 trace.registers[start_event:stop_event], trace.values[start_event:stop_event])


def convert_sections(trace, metadata, output, digi, gain, resolution, fit, seconds):
    """Render envelopes once, then package continuous state across score files."""
    ticks = math.ceil(trace.duration/TICK)
    _, mask = digi_regions(trace, digi, ticks)
    instruments = Instruments()
    voices, features = voice_states(trace, mask, ticks, instruments, metadata.get('sid_model', '6581'))
    pending = [(first, min(ticks, first+max(1,int(seconds/TICK))))
               for first in range(0, ticks, max(1,int(seconds/TICK)))]
    parts = []
    while pending:
        first, last = pending.pop(0)
        if len(parts) >= 99:
            raise ConversionError('Too many score sections; reduce capture duration')
        path = output.with_name(output.stem[:6]+f'{len(parts)+1:02d}.M')
        section = trace_section(trace, first, last)
        count = math.ceil(section.duration/TICK)
        # Rounding C64 cycles can add a sub-cycle fraction to a score section.
        section_voices = [list(stream[first:last]) for stream in voices]
        for stream in section_voices:
            while len(stream) < count: stream.append(stream[-1])
        try:
            score, bank, report = convert_trace(section, metadata, path.with_suffix('.P86').name,
                digi, gain, resolution, fit, (section_voices, features, instruments))
        except ConversionError as error:
            if '40 KiB' not in str(error) or last-first < 2: raise
            midpoint = (first+last)//2
            pending[0:0] = [(first,midpoint),(midpoint,last)]
            continue
        report['source_offset_seconds'] = first*TICK
        parts.append((path, score, bank, report))
    return parts


def build_helper():
    cmake = shutil.which("cmake")
    if cmake is None and os.name == "nt" and Path("C:/msys64/mingw64/bin/cmake.exe").exists():
        cmake = "C:/msys64/mingw64/bin/cmake.exe"
        os.environ["PATH"] = str(Path(cmake).parent) + os.pathsep + os.environ.get("PATH", "")
    if not cmake:
        raise ConversionError("Building sidtrace needs CMake and a C++17 compiler")
    command = [cmake, "-S", str(SOURCE), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release"]
    if "msys64" in cmake.lower(): command += ["-G", "Ninja"]
    subprocess.run(command, check=True)
    subprocess.run([cmake, "--build", str(BUILD), "--config", "Release", "--parallel", "4"], check=True)


def helper_path(explicit=None):
    if explicit:
        path = Path(explicit).resolve()
        if not path.is_file(): raise ConversionError(f"Capture helper does not exist: {path}")
        return path
    name = "sidtrace.exe" if os.name == "nt" else "sidtrace"
    for path in (SOURCE / name, BUILD / name, BUILD / "Release" / name):
        if path.is_file(): return path
    raise ConversionError("SID capture helper is missing. Run: python tools/sid2pmd.py --setup")


def dos_name(stem):
    cleaned = re.sub(r"[^A-Z0-9_]", "", stem.upper()) or "SID"
    if len(cleaned) > 8:
        cleaned = cleaned[:5] + hashlib.sha256(stem.encode()).hexdigest()[:3].upper()
    if cleaned in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}:
        cleaned = "S" + cleaned[:7]
    return cleaned


def write_outputs(output, score, bank, report, force):
    files = {output: score,
             output.with_suffix(".json"): (json.dumps(report, indent=2, ensure_ascii=False)+"\n").encode()}
    pages = max(8, math.ceil(len(bank)/16384)) if bank else 0
    batch = ("@echo off\r\nPMD86 /M40 /V0 /E0\r\n" + (f"P86DRV /{pages}\r\n" if bank else "") + f"PMP {output.name}\r\n")
    files[output.with_suffix(".BAT")] = batch.encode("ascii")
    if bank: files[output.with_suffix(".P86")] = bank
    if not force:
        exists = [str(p) for p in files if p.exists()]
        if exists: raise ConversionError("Output already exists (use --force): " + ", ".join(exists))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stage every output before replacing any destination.
    with tempfile.TemporaryDirectory(prefix="sid2pmd-", dir=output.parent) as folder:
        for index, (path, content) in enumerate(files.items()):
            Path(folder, str(index)).write_bytes(content)
        for index, path in enumerate(files):
            os.replace(Path(folder, str(index)), path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", nargs="?", type=Path)
    parser.add_argument("output", nargs="?", type=Path, help="DOS 8.3 output .M path")
    parser.add_argument("--seconds", type=float, default=120, help="capture length (default 120, maximum 900)")
    parser.add_argument("--song", type=int, help="1-based subtune (default SID's start song)")
    parser.add_argument("--clock", choices=("auto", "pal", "ntsc"), default="auto")
    parser.add_argument("--digi", choices=("auto", "off", "force"), default="auto")
    parser.add_argument("--digi-gain", type=float, default=1.)
    parser.add_argument("--resolution", type=int, choices=range(1, 9), default=1,
                        help="adaptive level tolerance in 0.75 dB steps (default 1); pitch and gates remain independently tracked")
    parser.add_argument("--fit", choices=("auto", "strict"), default="auto", help="automatically relax control precision to fit PMD86's buffer, or fail at requested precision")
    parser.add_argument("--segment-seconds", type=float, default=0., help="split into sequenced .M sections, preserving envelope state across boundaries")
    parser.add_argument("--warmup", type=float, default=0., help="discard startup seconds before capture (default 0)")
    parser.add_argument("--kernal", type=Path, help="8192-byte C64 Kernal ROM, required for RSID")
    parser.add_argument("--basic", type=Path, help="8192-byte BASIC ROM for BASIC RSID tunes")
    parser.add_argument("--chargen", type=Path, help="4096-byte character ROM")
    parser.add_argument("--sidtrace", type=Path, help="path to compiled capture helper")
    parser.add_argument("--setup", action="store_true", help="download/build the pinned capture helper")
    parser.add_argument("--force", action="store_true", help="replace existing output files")
    args = parser.parse_args(argv)
    try:
        if args.setup: build_helper()
        if args.input is None:
            if args.setup: return 0
            parser.error("an input .sid file is required")
        if not math.isfinite(args.seconds) or not 0 < args.seconds <= 900:
            raise ConversionError("--seconds must be between 0 and 900")
        if not math.isfinite(args.warmup) or not 0 <= args.warmup <= 60:
            raise ConversionError("--warmup must be between 0 and 60")
        if not math.isfinite(args.digi_gain) or not 0 < args.digi_gain <= 8:
            raise ConversionError("--digi-gain must be greater than 0 and at most 8")
        if not math.isfinite(args.segment_seconds) or not 0 <= args.segment_seconds <= 900:
            raise ConversionError('--segment-seconds must be between 0 and 900')
        metadata = sid_metadata(args.input, args.song, args.clock)
        if metadata["format"] == "RSID" and not args.kernal:
            raise ConversionError("RSID requires a C64 Kernal ROM: --kernal path/to/kernal.rom")
        output = args.output or args.input.with_name(dos_name(args.input.stem) + ".M")
        if output.suffix.upper() != ".M" or dos_name(output.stem) != output.stem.upper():
            raise ConversionError("Output needs a DOS 8.3 name such as SONG.M")
        for romarg in (args.kernal, args.basic, args.chargen):
            if romarg and romarg.resolve() == output.resolve():
                raise ConversionError("Output would overwrite an input ROM")
        targets = [output, output.with_suffix(".P86"), output.with_suffix(".json"), output.with_suffix(".BAT")]
        if args.input.resolve() in [p.resolve() for p in targets]:
            raise ConversionError("Output would overwrite the input")
        if not args.force and any(p.exists() for p in targets):
            raise ConversionError("Output files already exist; choose a new name or use --force")
        with tempfile.TemporaryDirectory(prefix="sid2pmd-") as folder:
            capture = Path(folder, "capture.trace")
            command = [str(helper_path(args.sidtrace)), str(args.input.resolve()), str(capture),
                       str(args.seconds), str(metadata["subtune"]), metadata["clock"],
                       *(str(p.resolve()) if p else "-" for p in (args.kernal, args.basic, args.chargen)), str(args.warmup)]
            subprocess.run(command, check=True)
            trace = read_trace(capture)
            if args.segment_seconds:
                sections = convert_sections(trace, metadata, output, args.digi, args.digi_gain,
                                             args.resolution, args.fit, args.segment_seconds)
                playlist = dict(metadata, source=str(args.input.resolve()), duration_seconds=trace.duration,
                    segmented=True, parts=[])
                # Complete conversion before publishing any output.
                destinations = [path.with_suffix(ext) for path,_,bank,_ in sections
                                for ext in ('.M','.json','.BAT',*(('.P86',) if bank else ()))]
                if not args.force and any(p.exists() for p in destinations):
                    raise ConversionError('A section output already exists; use --force')
                for path,score,bank,report in sections:
                    report['source'] = str(args.input.resolve())
                    write_outputs(path,score,bank,report,args.force)
                    playlist['parts'].append(dict(file=path.name, **report))
                    print(f'Created {path} ({len(score)} bytes)', flush=True)
                output.with_suffix('.json').write_text(json.dumps(playlist,indent=2)+'\n',encoding='utf-8')
                return 0
            score, bank, report = convert_trace(trace, metadata, output.with_suffix(".P86").name.upper(), args.digi, args.digi_gain, args.resolution, args.fit)
        report["source"] = str(args.input.resolve())
        write_outputs(output, score, bank, report, args.force)
        print(f"Created {output} ({len(score)} bytes)")
        if bank: print(f"Created {output.with_suffix('.P86')} ({len(bank)} bytes; {len(report['digi_regions'])} digi regions)")
        for warning in report.get("warnings", []): print("Warning:", warning, file=sys.stderr)
        print(f"Playback: {output.with_suffix('.BAT')} (copy with PMD86.COM, PMP.COM" + (", P86DRV.COM)" if bank else ")"))
        return 0
    except (ConversionError, OSError, subprocess.CalledProcessError) as error:
        print(f"sid2pmd: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

# SPDX-License-Identifier: GPL-2.0-or-later
"""Run with python -m unittest discover -s tools -p test_sid2pmd.py.

Set SID2PMD_TRACE to the helper path for C64 capture integration tests.
"""
from array import array
import importlib.util
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

import sid2pmd as converter


def synthetic_sid():
    """Original two-subtune fixture: FM pitch changes, noise and D418 bursts."""
    init = bytearray(b"\x85\x03")  # save the subtune accumulator
    for addr, value in ((0xD418, 15), (0xD404, 0x41), (0xD405, 0x20),
                        (0xD406, 0xF3), (0xD400, 0x40), (0xD401, 0x10),
                        (0xD412, 0x81), (0xD413, 0x10), (0xD414, 0xF3),
                        (0xD40E, 0x80), (0xD40F, 0x10)):
        init.extend((0xA9, value, 0x8D, addr & 255, addr >> 8))
    init.append(0x60)
    playaddr = 0x1000 + len(init)
    play = bytearray(b"\xe6\x02\xa5\x02\x29\x03\x18\x69\x10\x8d\x01\xd4")
    play.extend(b"\xa5\x02\x29\x07\xd0\x08\xad\x04\xd4\x49\x01\x8d\x04\xd4")
    play.extend(b"\xa2\x00")
    loop = len(play)
    play.extend(b"\x8a\x29\x0f\x8d\x18\xd4\xa0\x1e\x88\xd0\xfd\xe8\xe0\x40\xd0")
    play.append((loop-(len(play)+1)) & 255)
    play.extend(b"\xa9\x0f\x8d\x18\xd4\x60")
    header = bytearray(124)
    header[:4] = b"PSID"
    struct.pack_into(">7HI", header, 4, 2, 124, 0x1000, 0x1000, playaddr, 2, 2, 0)
    header[22:22+len(b"SID2PMD Test")] = b"SID2PMD Test"
    header[54:54+len(b"Original fixture")] = b"Original fixture"
    struct.pack_into(">H", header, 118, 0x14)  # PAL, MOS6581
    return bytes(header + init + play)


def trace(events, duration=.2, initial=None):
    initial = bytes(32) if initial is None else bytes(initial)
    events = sorted(events)
    return converter.Trace(985248, round(duration*985248), initial,
                           array("Q", [e[0] for e in events]),
                           bytearray(e[1] for e in events), bytearray(e[2] for e in events))


def decode_track(score, part):
    data = bytearray(score[1:])
    ptr = struct.unpack_from("<H", data, part*2)[0]
    total, notes = 0, []
    lengths = {0xFE: 1, 0xF1: 1, 0xEC: 1, 0xED: 1, 0xF0: 4,
               0xFD: 1, 0xFA: 2, 0xFF: 1, 0xEE: 1, 0xFC: 1, 0xFB: 0}
    while data[ptr] != 0x80:
        cmd = data[ptr]; ptr += 1
        if cmd < 0x80:
            total += data[ptr]; notes.append((cmd, data[ptr])); ptr += 1
        elif cmd == 0xF9:
            end = struct.unpack_from("<H", data, ptr)[0]
            data[end+1] = 0; ptr += 2
        elif cmd == 0xF8:
            count = data[ptr]; data[ptr+1] += 1
            target = struct.unpack_from("<H", data, ptr+2)[0]
            ptr = target+2 if data[ptr+1] < count else ptr+4
        else:
            if cmd not in lengths: raise AssertionError(f"Unexpected opcode {cmd:02x}")
            ptr += lengths[cmd]
    return total, notes


class ConverterTests(unittest.TestCase):
    def test_p86_absolute_offsets_and_signed_samples(self):
        bank = converter.make_p86([b"\x80\x00\x7f", b"\xff"])
        self.assertEqual(bank[:12], b"PCM86 DATA\n\0")
        self.assertEqual(int.from_bytes(bank[13:16], "little"), len(bank))
        self.assertEqual(int.from_bytes(bank[16:19], "little"), 0x610)
        self.assertEqual(int.from_bytes(bank[22:25], "little"), 0x613)
        self.assertEqual(bank[0x610:], b"\x80\x00\x7f\xff")

    def test_digi_detection_keeps_slow_volume_fades_out(self):
        slow = trace([(i*20000, 24, i) for i in range(10)])
        regions, _ = converter.digi_regions(slow, "auto", math.ceil(slow.duration/converter.TICK))
        self.assertEqual(regions, [])
        fast = trace([(50000+i*100, 24, i & 15) for i in range(100)])
        regions, _ = converter.digi_regions(fast, "auto", math.ceil(fast.duration/converter.TICK))
        self.assertTrue(regions)
        samples = converter.make_digi_samples(fast, regions, 1)
        self.assertTrue(any(value != 0 for sample in samples for value in sample))

    def test_constant_d418_has_no_pcm_dc(self):
        initial = bytearray(32); initial[24] = 15
        data = trace([], initial=initial)
        samples = converter.make_digi_samples(data, [(0, 5)], 1)
        self.assertEqual(set(samples[0]), {0})

    def test_known_pitch_matches_original_pmd_fnum(self):
        frequency = round(440 * 16777216/985248)
        note, detune = converter.pitch(frequency, 985248)
        self.assertEqual(note, 0x39)
        frequency2 = (converter.FNUM[9] + detune) * converter.OPNA_CLOCK/(144*1048576) * 8
        self.assertAlmostEqual(frequency2, 440, delta=.4)

    def test_release_and_same_tick_gate_retrigger(self):
        initial = bytearray(32)
        initial[0:7] = b"\x40\x10\x00\x08\x41\x00\xf3"
        initial[24] = 15
        data = trace([(10000, 4, 0x40), (11000, 4, 0x41), (50000, 4, 0x40)], initial=initial)
        ticks = math.ceil(data.duration / converter.TICK)
        states, _ = converter.voice_states(data, [False]*ticks, ticks, converter.Instruments())
        self.assertEqual(states[0][0][4], 1)
        self.assertGreater(states[0][3][3], 0)  # the release continues after gate-off

    def test_pmd_parts_metadata_and_pcm_timing(self):
        data = trace([(50000+i*100, 24, i&15) for i in range(100)])
        meta = dict(title="Fixture", composer="Test")
        score, bank, report = converter.convert_trace(data, meta, "TEST.P86")
        self.assertEqual(score[:3], b"\x00\x1a\x00")
        self.assertIsNotNone(bank)
        self.assertEqual(decode_track(score, 9)[0], math.ceil(data.duration/converter.TICK)+1)
        self.assertIn(b"TEST.P86\0Fixture\0Test\0", score)
        self.assertEqual(len(score), report["pmd_bytes"])
        data = score[1:]
        tone = struct.unpack_from("<H", data, 24)[0]
        self.assertEqual(data[tone-2:tone], b"\x48\xfe")
        memo = struct.unpack_from("<H", data, tone-4)[0]
        pcmname = struct.unpack_from("<H", data, memo+4)[0]
        self.assertTrue(data[pcmname:].startswith(b"TEST.P86\0"))

    def test_rejects_oversized_score_before_output(self):
        with self.assertRaisesRegex(converter.ConversionError, "64 KiB"):
            converter.make_pmd([bytes(66000)] + [b"\x80"]*10, [], "", "", "")

    def test_pattern_loops_relocate_and_preserve_all_notes(self):
        track = b"\xfc\x75" + b"\xfd\x64\xfa\0\0\x39\x03\xfb\x3a\x03"*12 + b"\x0f\x01\x80"
        compressed = converter.compress_track(track)
        self.assertLess(len(compressed), len(track))
        reference = converter.make_pmd([track]+[b"\x80"]*10, [], "", "", "")
        actual = converter.make_pmd([compressed]+[b"\x80"]*10, [], "", "", "")
        self.assertEqual(decode_track(reference, 0), decode_track(actual, 0))

    def test_subtune_header_and_dos_names(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "sample.sid"); path.write_bytes(synthetic_sid())
            self.assertEqual(converter.sid_metadata(path, None, "auto")["subtune"], 2)
            with self.assertRaises(converter.ConversionError): converter.sid_metadata(path, 3, "auto")
        self.assertEqual(converter.dos_name("CON"), "SCON")
        self.assertEqual(len(converter.dos_name("An extremely long title")), 8)

    def test_outputs_are_not_overwritten_without_force(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "TEST.M")
            path.write_bytes(b"original")
            with self.assertRaises(converter.ConversionError):
                converter.write_outputs(path, b"new", None, {}, False)
            self.assertEqual(path.read_bytes(), b"original")

    def test_invalid_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory, "bad.trace")
            path.write_bytes(b"S2PT"+bytes(10))
            with self.assertRaises(converter.ConversionError): converter.read_trace(path)

    @unittest.skipUnless(os.environ.get("SID2PMD_TRACE"), "Set SID2PMD_TRACE for emulation integration")
    def test_actual_c64_program_to_pmd_and_pcm(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory, "source.sid"); source.write_bytes(synthetic_sid())
            output = Path(directory, "TEST.M")
            rc = converter.main([str(source), str(output), "--seconds", ".5", "--sidtrace", os.environ["SID2PMD_TRACE"]])
            self.assertEqual(rc, 0)
            score = output.read_bytes()
            self.assertTrue(any(n[0] != 15 for n in decode_track(score, 0)[1]))
            self.assertEqual(decode_track(score, 0)[0], decode_track(score, 9)[0])
            self.assertTrue(output.with_suffix(".P86").exists())
            self.assertIn(b"P86DRV /76", output.with_suffix(".BAT").read_bytes())
            # Capture lasts the requested CPU time, including interleaved CIA
            # and VIC events; event-count scheduling used to stop too early.
            capture = Path(directory, "check.trace")
            subprocess.run([os.environ["SID2PMD_TRACE"], str(source), str(capture),
                            "0.5", "2", "pal", "-", "-", "-", "0"], check=True)
            traced = converter.read_trace(capture)
            self.assertGreater(traced.times[-1]/traced.clock, .48)


if __name__ == "__main__":
    unittest.main()

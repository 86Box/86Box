# Additional PC-98 sound boards

The C-BUS sound selection now includes Speak Board and Spark Board, in
addition to the NEC PC-9801-26K and PC-9801-86. Both new boards use the
existing ymfm synthesis cores rather than placeholder register storage.

Speak Board exposes a YM2608 at 188h/18Ah/18Ch/18Eh (or 088h/08Ah/08Ch/08Eh),
with six FM channels active after reset and 256 KiB of ADPCM-B sample RAM.
Spark Board adds a YM3438 at 588h/58Ah/58Ch/58Eh (or 488h/48Ah/48Ch/48Eh).
The Spark status ports read the first chip's timer status. Neither board
decodes the PC-9801-86's A460h control or its separate PCM FIFO ports.

Interrupt settings INT0/INT6/INT41/INT5 select IRQ3/13/10/12 and supply the
same setting on the PSG Port A jumper bits. The first mapped host joystick
supplies active-low directions and two buttons. PSG Port B bit 6 disconnects
its lines. The PC-9801-26K now also uses this joystick path.

An optional 16 KiB `sound_spb.rom` maps at C8000h, CC000h, D0000h or D4000h.
The default is CC000h; disabling it is available in configuration. The loader
checks `roms/sound/speakboard`, `roms/machines/pc9801` and `roms/speakboard`
under the configured ROM roots. The YM2608 rhythm section uses the existing
`ym2608_adpcm_rom.bin` search paths. Direct FM/SSG/ADPCM-B access works without
a BIOS ROM; rhythm samples require the rhythm ROM. ROMs are not distributed.

The board routing and reset behavior are cross-checked against the primary
[NP2kai boardspb.c implementation](https://github.com/AZO234/NP2kai/blob/master/cbus/boardspb.c)
and [PSG joystick wiring](https://github.com/AZO234/NP2kai/blob/master/sound/fmboard.c).
These are emulator sources rather than a complete hardware specification.
Analog volume controls, precise bus timing and guest sound BIOS execution
have not been verified. PC-9801-118, Sound Blaster C-BUS variants, Sound
Orchestra and WaveStar require additional board/codec/MIDI routing and are
not represented as equivalent aliases.

## Regression checks

The standalone `tests/sound/pc98` target compiles the production boards,
OPN bridge and actual ymfm cores. Its host adapters provide I/O dispatch,
timer scheduling, interrupt capture and mixer collection. It uses no BIOS
or rhythm ROM. It verifies both port bases, all IRQ jumpers, odd-port decode,
joystick/button/disconnect reads, all four 64 KiB sample RAM banks, retained
RAM after reset, audible FM channel 4 without an A460h enable sequence,
shared status ports, Spark stereo DAC output and reset silence, and handler
and timer cleanup. The Spark DAC check exposed ymfm's YM3438 reset preserving
DAC latches; the bridge now resets those latches explicitly.

```sh
cmake -S tests/sound/pc98 -B build/tests-pc98-sound -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/tests-pc98-sound --parallel 4
ctest --test-dir build/tests-pc98-sound --output-on-failure
```

The suite passed 1,682 checks on the Windows MinGW build. This establishes
device/core behavior, not a successful guest game run or physical timing
equivalence.

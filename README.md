(We need help in developing this emulator! See https://github.com/86Box/86Box/issues/7386 for details. Any and all help is appreciated.)

86Box
=====

[![Build Status](https://ci.86box.net/job/86Box/badge/icon)](https://ci.86box.net/job/86Box/)
[![License](https://img.shields.io/github/license/86Box/86Box)](COPYING)
[![Latest release](https://img.shields.io/github/release/86Box/86Box.svg)](https://github.com/86Box/86Box/releases)
[![Downloads](https://img.shields.io/github/downloads/86Box/86Box/total.svg)](https://github.com/86Box/86Box/releases)
[![Translation status](https://weblate.86box.net/widget/86box/86box/language-badge.svg)](https://weblate.86box.net/engage/86box/)

86Box AWE32
-----------

**This is a fork of 86Box 6.0 (branch `masterAWE32`) with a Sound Blaster AWE32 that
sounds like the real card.** Everything else is 86Box 6.0 - the rest of this README is
the original one.

86Box emulates the AWE32 and its EMU8000 wavetable synthesizer already. In this fork the
EMU8000 is replaced by the chip of the [AWE32Emu](https://github.com/turican0/AWE32Emu)
project: 86Box's EMU8000 corrected against recordings of a real Sound Blaster AWE32,
made with a calibration program written for that purpose. The same file
(`src/sound/snd_emu8k.c`) is used byte for byte in AWE32Emu and in
[DOSBox-X AWE32](https://github.com/turican0/dosbox-x-AWE32), so all three produce the
same sound from the same register writes.

| | 86Box 6.0 | this fork |
|---|---|---|
| filter | cutoff about 1.8 octaves higher than on the card | Chamberlin state-variable filter with the cutoff mapping and resonance measured on the card |
| envelopes | sustain in linear steps, slow rates rounded to zero | sustain in 0.75 dB steps, fractional rates, attack shape as measured |
| interpolation | cubic (Catmull-Rom) | cubic B-spline, as the card's aliasing shows |
| reverb | generic comb/all-pass network | fitted per preset to the card's line output, including the echo presets 6/7 and early reflections |
| chorus | generic | structure, delays and feedback as measured |
| equaliser | not implemented | bass and treble shelves decoded from INIT3/INIT4 |
| output level | - | level and headroom of the card's output stage |

The changes are marked `AWE32Emu:` in the code. Outside the chip only
`src/sound/snd_sb.c` changes: it clears the chip's buffer with `emu8k_reset_buffer()`
after reading it, as 86Box does after 6.0. Nothing else in the Sound Blaster, the
settings or the machine configuration changes - an existing VM with an AWE32 just
sounds different.

For comparing runs: with the environment variable `EMU8K_TRACE=<file>` every EMU8000
port write is recorded (the format of AWE32Emu `--replay` and of DOSBox-X AWE32), and
`AWE32_WAV=<file>` writes the chip's output as a WAV. Without them nothing happens.

Related projects:

* [AWE32Emu](https://github.com/turican0/AWE32Emu) - the same EMU8000 as a `.mid` /
  `.xmi` player with the logic of Creative's drivers, the measurements and the tools
* [DOSBox-X AWE32](https://github.com/turican0/dosbox-x-AWE32) - DOSBox-X with a
  complete Sound Blaster AWE32 (EMU8000, `AWEUTIL`, wave ROM download)

### Thanks

Many thanks to **Mysterium Xerxes** (orzipan), who patiently recorded a real Sound
Blaster AWE32 again and again - test program after test program, the games, the line
output and the card's own capture. Every measured detail of the EMU8000 in this fork
comes from those recordings.

---

**The original 86Box README follows.**

**86Box** is a low level x86 emulator that runs older operating systems and software designed for IBM PC systems and compatibles from 1981 through fairly recent system designs based on the PCI bus.

Features
--------

* Easy to use interface inspired by mainstream hypervisor software
* Low level emulation of 8086-based processors up to the Mendocino-era Celeron with focus on accuracy
* Great range of customizability of virtual machines
* Many available systems, such as the very first IBM PC 5150 from 1981, or the more obscure IBM PS/2 line of systems based on the Micro Channel Architecture
* Lots of supported peripherals including video adapters, sound cards, network adapters, hard disk controllers, and SCSI adapters
* MIDI output to Windows built-in MIDI support, FluidSynth, or emulated Roland synthesizers
* Supports running MS-DOS, older Windows versions, OS/2, many Linux distributions, or vintage systems such as BeOS or NEXTSTEP, and applications for these systems

Minimum system requirements and recommendations
-----------------------------------------------

* 64-bit Intel Core 2, AMD Athlon 64 or ARMv8 processor or newer
* 4 GB of RAM or higher
* **Windows version:** Windows 7 Service Pack 1 or newer on Intel/AMD systems; Windows 11 or newer on ARM systems
* **Linux version:** Ubuntu 16.04, Debian 9.0 or other distributions from 2016 onwards
* **macOS version:** macOS 10.14 Mojave or newer

Performance may vary depending on host and guest configuration. Most emulation logic is executed in a single thread. Therefore, systems with greater IPC (instructions per clock) capacity should be able to emulate higher clock speeds.

While 86Box already includes a manager for easy handling of multiple machines, some third-party ones exist as well:

* [Avalonia 86](https://github.com/notBald/Avalonia86) by [notBald](https://github.com/notBald) (Windows and Linux)
* [86Box Manager](https://github.com/86Box/86BoxManager) by [Overdoze](https://github.com/daviunic) (Windows only)
* [86Box Manager X](https://github.com/RetBox/86BoxManagerX) by [xafero](https://github.com/xafero) (Cross platform Port of 86Box Manager using Avalonia)
* [sl86](https://github.com/DDXofficial/sl86) by [DDX](https://github.com/DDXofficial) (Command-line 86Box machine manager written in Python)
* [Linbox-qt5](https://github.com/Dungeonseeker/linbox-qt5) by [Dungeonseeker](https://github.com/Dungeonseeker/) (Linux focused, should work on Windows though untested)
* [MacBox for 86Box](https://github.com/Moonif/MacBox) by [Moonif](https://github.com/Moonif) (MacOS only)

To manually use 86Box with multiple machines without a manager, use the `--vmpath`/`-P` command line option.

Getting started
---------------

See [our documentation](https://86box.readthedocs.io/en/latest/index.html) for an overview of the emulator's features and user interface.

Community
---------

We operate an IRC channel and a Discord server for discussing 86Box, its development, and anything related to retro computing. We look forward to hearing from you!

[![Visit our IRC channel](https://kiwiirc.com/buttons/irc.ringoflightning.net/86Box.png)](https://kiwiirc.com/client/irc.ringoflightning.net/?nick=86box|?#86Box)

[![Visit our Discord server](https://discordapp.com/api/guilds/262614059009048590/embed.png)](https://discord.gg/QXK9XTv)

[Forum: SoftHistory](https://forum.softhistory.org/)

[Wiki: SoftHistory](https://wiki.softhistory.org/)

[Twitter: @86BoxEmulator](https://twitter.com/86BoxEmulator)

[YouTube: 86Box](https://youtube.com/c/86Box)

Contributions
-------------

We welcome all contributions to the project, as long as the [contribution guidelines](CONTRIBUTING.md) are followed.

Building
---------
For instructions on how to build 86Box from source, see the [build guide](https://86box.readthedocs.io/en/latest/dev/buildguide.html).

Licensing
---------

86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or later. For more information, see the `COPYING` file in the root of the repository.

The emulator can also optionally make use of [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/) and [Ghostscript](https://www.ghostscript.com/), which are distributed under their respective licenses.

Donations
---------

We do not charge you for the emulator but donations are still welcome:
<https://paypal.me/86Box>.
You can also support the project on Patreon:
<https://www.patreon.com/86box>.

Acknowledgments
---------------

### Powered by
[![JetBrains logo.](https://resources.jetbrains.com/storage/products/company/brand/logos/jetbrains.svg)](https://jb.gg/OpenSource)

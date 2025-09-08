Immersive86Box
=====

**Immersive86Box** is a fork of 86box v5.0, which is a low level x86 emulator that runs older operating systems and software designed for IBM PC systems and compatibles from 1981 through fairly recent system designs based on the PCI bus.

**Immersive86Box** projects aims to add sound emulation for hardware device likes floppy disk drives, harddrives, CD/DVD-drives and PC fans. Also we aim to add visualization layer that will display the image of the actual PC front-bezel that with working HDD and FDD leds + image of physical display screen front-bezel that outputs CRT-like image. Thus making it feel like you're actually using that PC and not an emulator - thus the **IMMERSIVE** aspect.

Features
--------

* Typical 3,5" 1,44MB 300RPM FDD motor sound starting, running and stopping. FDD single track step sound an multi-track seek sound (not merged yet)

Future plans
------------

* Typical system fan sounds
* Typical 5400 RPM 3,5" HDD motor and seeking sounds
* Typical CD/DVD motor and seekings sounds
* IBM PS/1 Model 2121 front-bezel with working leds
* IBM PS/1 Model 2121 VGA-display front-bezel that displays the emulator output in CRT-fashion

Minimum system requirements and recommendations
-----------------------------------------------

* Intel Core 2 or AMD Athlon 64 processor or newer
* Windows version: Windows 7 Service Pack 1 or later
* Linux version: Ubuntu 16.04, Debian 9.0 or other distributions from 2016 onwards
* macOS version: macOS High Sierra 10.13 or newer
* 4 GB of RAM or higher

Performance may vary depending on host and guest configuration. Most emulation logic is executed in a single thread. Therefore, systems with greater IPC (instructions per clock) capacity should be able to emulate higher clock speeds.

Getting started
---------------

See [our documentation](https://86box.readthedocs.io/en/latest/index.html) for an overview of the emulator's features and user interface.

Building
---------
For instructions on how to build 86Box from source, see the [build guide](https://86box.readthedocs.io/en/latest/dev/buildguide.html).

Licensing
---------

Immersive86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or later. For more information, see the `COPYING` file in the root of the repository.

The emulator can also optionally make use of [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/), [Ghostscript](https://www.ghostscript.com/) and [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), which are distributed under their respective licenses.

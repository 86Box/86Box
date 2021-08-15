86Box
=====
**86Box** is a low level x86 emulator that runs older operating systems and software designed for IBM PC systems and compatibles from 1981 through fairly recent system designs based on the PCI bus.

Features
--------
* Easy to use interface inspired by mainstream hypervisor software
* Low level emulation of 8086-based processors up to the Pentium with focus on accuracy
* Great range of customizability of virtual machines
* Many available systems, such as the very first IBM PC 5150 from 1981, or the more obscure IBM PS/2 line of systems based on the Micro Channel Architecture
* Lots of supported peripherals including video adapters, sound cards, network adapters, hard disk controllers, and SCSI adapters
* MIDI output to Windows built-in MIDI support, FluidSynth, or emulated Roland synthesizers
* Supports running MS-DOS, older Windows versions, OS/2, many Linux distributions, or vintage systems such as BeOS or NEXTSTEP, and applications for these systems

System requirements and recommendations
---------------------------------------
* Intel Core 2 or AMD Athlon 64 processor
* Windows 7 Service Pack 1, Windows 8.1 or Windows 10
* 4 GB of RAM

Performance may vary depending on both host and guest configuration. Most emulation logic is executed in a single thread, therefore generally systems with better IPC (instructions per clock) should be able to emulate higher clock speeds.

It is also recommended to use the [86Box Manager](https://github.com/86Box/86BoxManager) by [daviunic](https://github.com/daviunic) (Overdoze) to manage virtual machines. However, it is also possible to use 86Box on its own with the `--vmpath`/`-P` command line option.

Downloads
---------
The latest stable version of 86Box is version 2.07, which was released on November 20, 2019, and is available from our [GitHub repository](https://github.com/86Box/86Box/releases/tag/v2.07).

### Automatic builds
We also offer automatic builds, which are built from the latest source code and contain the latest bugfixes and improvements, but may not be as stable and/or optimized as stable builds.

| Regular | Debug | Experimental |
|:-------:|:-----:|:------------:|
|[![Build Status](http://ci.86box.net/job/86Box/badge/icon)](http://ci.86box.net/job/86Box)|[![Build Status](http://ci.86box.net/job/86Box-Debug/badge/icon)](http://ci.86box.net/job/86Box-Debug)|[![Build Status](http://ci.86box.net/job/86Box-Dev/badge/icon)](http://ci.86box.net/job/86Box-Dev)

#### Legend
* **Regular** builds are compiled using the settings in the building guide above. Use these if you don't know which build to use.
* **Debug** builds are same as regular builds but include debug symbols. If you don't need them, you don't need to use this build.
* **Experimental (Dev)** builds are compiled with certain unfinished features enabled. These builds are not optimized for maximum performance.

Getting started
---------------
See [our documentation](https://86box.readthedocs.io/en/latest/index.html) for an overview of the emulator's features and user interface.

Community
---------
We operate an IRC channel and a Discord server for discussing 86Box, its development and anything related to retro computing. We look forward to hearing from you!

[![Visit our IRC channel](https://kiwiirc.com/buttons/irc.ringoflightning.net/86Box.png)](https://kiwiirc.com/client/irc.ringoflightning.net/?nick=86box|?#86Box)

[![Visit our Discord server](https://discordapp.com/api/guilds/262614059009048590/embed.png)](https://discord.gg/QXK9XTv)

Licensing
---------
86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) only. For more information, see the `COPYING` file in the root of the repository.

The emulator can also optionally make use of [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/), [Ghostscript](https://www.ghostscript.com/) and [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), which are distributed under their respective licenses.

Donations
---------
We do not charge you for the emulator but donations are still welcome:
https://paypal.me/86Box.

You can also support the project on Patreon:
https://www.patreon.com/86box.

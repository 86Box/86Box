86Box [![Build Status](http://ci.86box.net/job/86Box/badge/icon)](http://ci.86box.net/job/86Box)
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
* Windows version: Windows 7 Service Pack 1, Windows 8.1 or Windows 10
* Linux version: Ubuntu 16.04, Debian 9.0 or other distributions from 2016 onwards
* 4 GB of RAM

Performance may vary depending on both host and guest configuration. Most emulation logic is executed in a single thread, therefore generally systems with better IPC (instructions per clock) should be able to emulate higher clock speeds.

It is also recommended to use a manager application (**Windows only**) with 86Box for easier handling of multiple virtual machines.
* [WinBox for 86Box](https://github.com/86Box/WinBox-for-86Box) by Laci bá'
  * The new manager with improved new user experience; installer, automatic updates of emulator files and more.
  * While development is suspended and the repository is archived, the latest version is fully functional.
* [86Box Manager](https://github.com/86Box/86BoxManager) by [daviunic](https://github.com/daviunic) (Overdoze)
  * The traditional 86Box manager with simple interface.

However, it is also possible to use 86Box on its own with the `--vmpath`/`-P` command line option.

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
86Box is released under the [GNU General Public License, version 2](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html) or later. For more information, see the `COPYING` file in the root of the repository.

The emulator can also optionally make use of [munt](https://github.com/munt/munt), [FluidSynth](https://www.fluidsynth.org/), [Ghostscript](https://www.ghostscript.com/) and [Discord Game SDK](https://discord.com/developers/docs/game-sdk/sdk-starter-guide), which are distributed under their respective licenses.

Donations
---------
We do not charge you for the emulator but donations are still welcome:
https://paypal.me/86Box.

You can also support the project on Patreon:
https://www.patreon.com/86box.

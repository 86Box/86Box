PCBox
=====

PCBox is a low-level PC emulator based on 86Box.

You can find CI builds under our Actions tab.

<<<<<<< HEAD
Visit our Discord! https://discord.gg/mWStgCdXus
=======
Performance may vary depending on both host and guest configuration. Most emulation logic is executed in a single thread, therefore generally systems with better IPC (instructions per clock) should be able to emulate higher clock speeds.

It is also recommended to use a manager application with 86Box for easier handling of multiple virtual machines.
* [WinBox for 86Box](https://github.com/laciba96/WinBox-for-86Box) by [Laci bá'](https://github.com/laciba96)
  * The new manager with improved new user experience; installer, automatic updates of emulator files and more.
* [86Box Manager](https://github.com/86Box/86BoxManager) by [daviunic](https://github.com/daviunic) (Overdoze)
  * The traditional 86Box manager with simple interface.

However, it is also possible to use 86Box on its own with the `--vmpath`/`-P` command line option.

Downloads
---------
The latest stable version of 86Box is version 2.07, which was released on November 20, 2019, and is available from our [GitHub repository](https://github.com/86Box/86Box/releases/tag/v2.07).

### Automatic builds
We also offer automatic builds, which are built from the latest source code and contain the latest bugfixes and improvements, but may not be as stable and/or optimized as stable builds.

[![Build Status](http://ci.86box.net/job/86Box/badge/icon)](http://ci.86box.net/job/86Box)

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
>>>>>>> c6c2988b640ddf9e8c18384bcbc0ee940772cc9b

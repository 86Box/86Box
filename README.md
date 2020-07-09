86Box
=====
**86Box** is a hypervisor and IBM PC system emulator that specializes in
running old operating systems and software designed for IBM PC systems and
compatibles from 1981 through fairly recent system designs based on the
PCI bus.

86Box is released under the GNU General Public License, version 2 or later.
For more information, see the `COPYING` file.

The project maintainer is OBattler.

If you need a configuration manager for 86Box, use the [86Box Manager](https://github.com/86Box/86BoxManager), our
officially endorsed 86Box configuration manager, developed by Overdoze (daviunic).

Community
---------
We operate an IRC channel and a Discord server for discussing anything related 
to retro computing and, of course, 86Box. We look forward to hearing from you!

[![Visit our IRC channel](https://kiwiirc.com/buttons/irc.ringoflightning.net/softhistory.png)](https://kiwiirc.com/client/irc.ringoflightning.net/?nick=86box|?#softhistory)

[![Visit our Discord server](https://discordapp.com/api/guilds/262614059009048590/embed.png)](https://discord.gg/QXK9XTv)

Getting started
---------------
See [this](https://86box.github.io/gettingstarted) page on our website for a quick guide that should help you get started with the emulator.

Building
--------
See the [build guide](doc/build.md).

Automatic builds
--------------
For your convenience, we compile a number of 86Box builds per revision on our
Jenkins instance.

| Regular | Debug | Optimized | Experimental |
|:-------:|:-----:|:---------:|:------------:|
|[![Build Status](http://ci.86box.net/job/86Box/badge/icon)](http://ci.86box.net/job/86Box)|[![Build Status](http://ci.86box.net/job/86Box-Debug/badge/icon)](http://ci.86box.net/job/86Box-Debug)|[![Build Status](http://ci.86box.net/job/86Box-Optimized/badge/icon)](http://ci.86box.net/job/86Box-Optimized)|[![Build Status](http://ci.86box.net/job/86Box-Dev/badge/icon)](http://ci.86box.net/job/86Box-Dev)

### Legend
* **Regular** builds are compiled using the settings in the building guide
  above. Use these if you don't know which build to use.
* **Debug** builds are same as regular builds but include debug symbols.
  If you don't need them, you don't need to use this build.
* **Optimized** builds have the same feature set as regular builds, but are
  optimized for every modern Intel and AMD processor architecture, which might
  improve the emulator's performance in certain scenarios.
* **Experimental (Dev)** builds are similar to regular builds but are compiled
  with certain unfinished features enabled. These builds are not optimized for maximum performance.

Donations
---------
We do not charge you for the emulator but donations are still welcome:
https://paypal.me/86Box.

You can now also support the project on Patreon:
https://www.patreon.com/86box.

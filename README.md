86Box
=====
**86Box** is a hypervisor and IBM PC system emulator that specializes in
running old operating systems and software designed for IBM PC systems and
compatibles from 1981 through fairly recent system designs based on the
PCI bus.

86Box is released under the GNU General Public License, version 2. For more
information, see the `LICENSE` file.

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
In order to compile 86Box from this repository, please follow this step-by-step 
guide:
1. Install the [MSYS2](https://www.msys2.org/) environment. The rest of the guide will refer to the directory that you install it to (C:\msys32 or C:\msys64 by default) as the MSYS2 root.
2. Launch your MSYS2 environment using the `MSYS2 MinGW 32-bit` shortcut.
3. Once launched, run `pacman -Syu` in order to update the environment. You may need to do this twice, just follow the on-screen instructions. Make sure you re-run `pacman -Syu` periodically to keep the environment up-to-date.
4. Run the following command to install all of the dependencies: `pacman -S gdb make git mingw-w64-i686-toolchain mingw-w64-i686-openal mingw-w64-i686-freetype mingw-w64-i686-SDL2 mingw-w64-i686-zlib mingw-w64-i686-libpng mingw-w64-i686-ghostscript`. Additionally, you will need to download the developer's pack of WinPcap [from here](https://www.winpcap.org/devel.htm), and extract it into `<MSYS2 root>\mingw32\`.
5. Once the environment is fully updated and all dependencies are installed, `cd` into your cloned `86box\src`
   directory.
6. Run `make -jN -f win/makefile.mingw` to start the actual compilation process.
   Substitute `N` with the number of threads you want to use for the compilation
   process. The optimal number depends entirely on your processor, and it is
   up to you to determine the optimal number. A good starting point is the total
   number of threads (AKA Logical Processors) you have available.
7. If the compilation succeeded (which it almost always should), you will find
   `86Box.exe` in the src directory.
8. In order to test your fresh build, replace the `86Box.exe` in your current
   86Box enviroment with your freshly built one. If you do not have a
   pre-existing 86Box environment, download the latest successful build from
   http://ci.86box.net, and the ROM set from https://tinyurl.com/rs20191022.
9. Enjoy using and testing the emulator! :)

If you encounter issues at any step or have additional questions, please join
the IRC channel or the appropriate channel on our Discord server and wait patiently for someone to help you.

Nightly builds
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
https://paypal.me/86Box .

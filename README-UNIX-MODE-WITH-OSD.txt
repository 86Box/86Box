UNIX MODE WITH OSD

86Box supports running on the linux framebuffer without QT and without X, making the pc appear as a nearly native old machine.

running it that way is already supported but when doing it, 86box loses all menu and all abilities to mount floppies and CDs, it also becomes the owner of the entire pc with no way of quitting it or changing virtual console.

to overcome this, an on screen display menu is available that allows doing everything the textual console (src/unix/unix.c) does;
mount/unmount all supported media like floppy and cd
hard reset the machine
quit 86box
seeing the current performance %

key bindings:
Right Control + F11 opens the osd

while is open:
arrows up, down moves the cursor
enter does the action, at mount options it enters a list of appropriate files (*.img, *.iso)
ESC goes back to main or closes the OSD

current limitations:
	OSD can mount images to first floppy and first cd, secondary devices are not supported
	it does not show if an image is mounted or not
	the option "version" does actully print it, but it can't be seen beacuse its printed under 86box display
	extremely long filenames can overflow the blue window
	the title does actually overflow the window width :)

These are the steps to install a machine fully dedicated to 86Box and tuned to make it appear almost native.
This works almost the same for on a Raspberry Pi


1) install a vanilla Debian Trixie with netinst and without any graphical environment, or a "server" distro for RPI
   depending on the machine speed, this will make boot time extremely short

2) apt update if necessary

3) install git and almost all required packages
	apt install git build-essential cmake extra-cmake-modules pkg-config ninja-build libfreetype-dev libsdl2-dev libpng-dev libopenal-dev librtmidi-dev libfluidsynth-dev libsndfile1-dev libserialport-dev libevdev-dev libxkbcommon-dev libxkbcommon-x11-dev libslirp-dev
	
4) setup git and clone
	git@github.com:86Box/86Box.git
	git@github.com:86Box/roms.git

5) build (128 => super speed, too much for a 2GB machine)
	cd 86Box
	mkdir build
	cd build
	cmake .. --preset regular -D QT=OFF -D PREFER_STATIC=ON
	cmake --build regular -j 128
	cd ../../
	ln -s 86Box/build/regular/src/86Box 86Box.exe
	
6) boot
	as root so it can take complete ownership of the linux framebuffer

7) notes:
- 86Box will complain to be unable to find readline, this is fine, we don't need the command line at all
- ALSOFT will complain it can't connect to PipeWire, no problem, sounds will come from standard ALSA

8) additional steps
	add a new udev rule to automount any USB key to a known location so files in it can be listed in the OSD floppy/cd mount options

	create /etc/udev/rules.d/99-automount.rules

	with:
	ACTION=="add", ENV{ID_BUS}=="usb", ENV{ID_TYPE}=="disk", ENV{ID_FS_TYPE}=="exfat", RUN+="/usr/bin/systemd-mount --no-block --automount=yes --collect /dev/%k '/mnt'"

	replicate this line for each filesystem you expect the usb key to be formatted, in this example "exfat"
	this is going to conflict if multiple keys are inserted, don't do it

final step
	configure some 86box vm using another pc with the GUI
	copy the VM definitions to this new pc and manually launch 86box from the textual command line
	optionally craft a boot menu to be shown in place of the login prompt or setup 86box as a debiasn service, it will start at boot
	just make sure the pc will be accessible via ssh or some other way, autobooting 86box can√ make difficult terminating it




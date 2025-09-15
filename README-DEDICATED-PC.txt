These are the steps to install a machine fully dedicated to 86Box and tuned to make it appear almost native.

1) install a vanilla Debian Trixie with netinst and without any graphical environment
   depending on the machine speed, this will make boot time extremely short

2) apt update if necessary

3) install git and almost all required packages
	apt install git build-essential cmake extra-cmake-modules pkg-config ninja-build libfreetype-dev libsdl2-dev libpng-dev libopenal-dev librtmidi-dev libfluidsynth-dev libsndfile1-dev libserialport-dev libevdev-dev libxkbcommon-dev libxkbcommon-x11-dev libslirp-dev
	
4) setup git and clone
	git@github.com:Valefungo/86Box.git
	git@github.com:86Box/roms.git
   git checkout unix_ods

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



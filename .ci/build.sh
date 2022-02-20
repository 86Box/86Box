#!/bin/sh
#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		Jenkins build script.
#
#
# Authors:	RichardG, <richardg867@gmail.com>
#
#		Copyright 2021-2022 RichardG.
#

#
# While this script was made for our Jenkins infrastructure, you can run it
# to produce Jenkins-like builds on your local machine by following these notes:
#
# - Run build.sh without parameters to see its usage
# - For Windows (MSYS MinGW) builds:
#   - Packaging requires 7-Zip on Program Files
#   - Packaging the Ghostscript DLL requires 32-bit and/or 64-bit Ghostscript on Program Files
#   - Packaging the FluidSynth DLL requires it to be at /home/86Box/dll32/libfluidsynth.dll
#     and/or /home/86Box/dll64/libfluidsynth64.dll (for 32-bit and 64-bit builds respectively)
#   - Packaging the Discord DLL requires wget (MSYS should come with it)
# - For Linux builds:
#   - Only Debian and derivatives are supported
#   - dpkg and apt-get are called through sudo to manage dependencies
# - For macOS builds:
#   - TBD
#

# Define common functions.
alias is_windows='[ ! -z "$MSYSTEM" ]'
alias is_mac='uname -s | grep -q Darwin'

make_tar() {
	# Install dependencies.
	if ! which tar xz > /dev/null 2>&1
	then
		which apt-get > /dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive sudo apt-get install -y tar xz-utils
	fi

	# Determine the best supported compression type.
	local compression_flag=
	local compression_ext=
	if which xz > /dev/null 2>&1
	then
		local compression_flag=-J
		local compression_ext=.xz
	elif which bzip2 > /dev/null 2>&1
	then
		local compression_flag=-j
		local compression_ext=.bz2
	elif which gzip > /dev/null 2>&1
	then
		local compression_flag=-z
		local compression_ext=.gz
	fi

	# Make tar verbose if requested.
	[ ! -z "$VERBOSE" ] && local compression_flag="$compression_flag -v"

	# tar is notorious for having many diverging implementations. For instance,
	# the flags we use to strip UID/GID metadata can be --owner/group (GNU),
	# --uid/gid (bsdtar) or even none at all (MSYS2 bsdtar). Account for such
	# flag differences by checking if they're mentioned on the help text.
	local ownership_flags=
	local tar_help=$(tar --help 2>&1)
	if echo $tar_help | grep -q -- --owner
	then
		local ownership_flags="--owner=0 --group=0"
	elif echo $tar_help | grep -q -- --uid
	then
		local ownership_flags="--uid 0 --gid 0"
	fi

	# Run tar.
	tar -c $compression_flag -f "$1$compression_ext" $ownership_flags *
	return $?
}

# Set common variables.
project=86Box
project_lower=86box
cwd=$(pwd)

# Parse arguments.
package_name=
arch=
tarball_name=
strip=0
cmake_flags=
while [ $# -gt 0 ]
do
	case $1 in
		-b)
			shift
			package_name="$1"
			shift
			arch="$1"
			shift
			;;

		-s)
			shift
			tarball_name="$1"
			shift
			;;

		-t)
			shift
			strip=1
			;;

		*)
			if echo $1 | grep -q " "
			then
				cmake_flag="\"$1\""
			else
				cmake_flag="$1"
			fi
			if [ -z "$cmake_flags" ]
			then
				cmake_flags="$cmake_flag"
			else
				cmake_flags="$cmake_flags $cmake_flag"
			fi
			shift
			;;
	esac
done
cmake_flags_extra=

# Check if mandatory arguments were specified.
if [ -z "$package_name" -a -z "$tarball_name" ] || [ ! -z "$package_name" -a -z "$arch" ]
then
	echo '[!] Usage: build.sh -b {package_name} {architecture} [-t] [cmake_flags...]'
	echo '           build.sh -s {source_tarball_name}'
	exit 100
fi

# Switch to the repository root directory.
cd "$(dirname "$0")/.."

# Make source tarball if requested.
if [ ! -z "$tarball_name" ]
then
	echo [-] Making source tarball [$tarball_name]

	# Clean local tree of gitignored files.
	git clean -dfX

	# Recreate working directory if it was removed by git clean.
	[ ! -d "$cwd" ] && mkdir -p "$cwd"

	# Save current HEAD commit to VERSION.
	git log --stat -1 > VERSION || rm -f VERSION

	# Archive source.
	make_tar "$cwd/$tarball_name.tar"
	status=$?

	# Check if the archival succeeded.
	if [ $status -ne 0 ]
	then
		echo [!] Tarball creation failed with status [$status]
		exit 1
	else
		echo [-] Source tarball [$tarball_name] created successfully
		[ -z "$package_name" ] && exit 0
	fi
fi

echo [-] Building [$package_name] for [$arch] with flags [$cmake_flags]

# Determine CMake toolchain file for this architecture.
case $arch in
	32 | x86)	toolchain="flags-gcc-i686";;
	64 | x86_64)	toolchain="flags-gcc-x86_64";;
	ARM32 | arm32)	toolchain="flags-gcc-armv7";;
	ARM64 | arm64)	toolchain="flags-gcc-aarch64";;
	*)		toolchain="flags-gcc-$arch";;
esac

# Perform platform-specific setup.
strip_binary=strip
if is_windows
then
	# Switch into the correct MSYSTEM if required.
	msys=MINGW$arch
	[ ! -d "/$msys" ] && msys=CLANG$arch
	if [ -d "/$msys" ]
	then
		if [ "$MSYSTEM" != "$msys" ]
		then
			# Call build with the correct MSYSTEM.
			echo [-] Switching to MSYSTEM [$msys]
			cd "$cwd"
			strip_arg=
			[ $strip -ne 0 ] && strip_arg="-t "
			CHERE_INVOKING=yes MSYSTEM="$msys" bash -lc 'exec "'"$0"'" -b "'"$package_name"'" "'"$arch"'" '"$strip_arg""$cmake_flags"
			exit $?
		fi
	else
		echo [!] No MSYSTEM for architecture [$arch]
		exit 2
	fi
	echo [-] Using MSYSTEM [$MSYSTEM]

	# Point CMake to the toolchain file.
	cmake_flags_extra="$cmake_flags_extra -D \"CMAKE_TOOLCHAIN_FILE=cmake/$toolchain.cmake\""
elif is_mac
then
	# macOS lacks nproc, but sysctl can do the same job.
	alias nproc='sysctl -n hw.logicalcpu'
else
	# Determine Debian architecture.
	case $arch in
		x86)	arch_deb="i386";;
		x86_64)	arch_deb="amd64";;
		arm32)	arch_deb="armhf";;
		*)	arch_deb="$arch";;
	esac

	# Establish general dependencies.
	pkgs="cmake pkg-config git imagemagick wget p7zip-full wayland-protocols tar gzip"
	if [ "$(dpkg --print-architecture)" = "$arch_deb" ]
	then
		pkgs="$pkgs build-essential"
	else
		sudo dpkg --add-architecture "$arch_deb"
		pkgs="$pkgs crossbuild-essential-$arch_deb"
	fi

	# Establish architecture-specific dependencies we don't want listed on the readme...
	pkgs="$pkgs linux-libc-dev:$arch_deb extra-cmake-modules:$arch_deb qttools5-dev:$arch_deb qtbase5-private-dev:$arch_deb"

	# ...and the ones we do want listed. Non-dev packages fill missing spots on the list.
	libpkgs=""
	longest_libpkg=0
	for pkg in libc6-dev libstdc++6 libopenal-dev libfreetype6-dev libx11-dev libsdl2-dev libpng-dev librtmidi-dev qtdeclarative5-dev libwayland-dev libevdev-dev libglib2.0-dev libslirp-dev
	do
		libpkgs="$libpkgs $pkg:$arch_deb"
		length=$(echo -n $pkg | sed 's/-dev$//' | sed "s/qtdeclarative/qt/" | wc -c)
		[ $length -gt $longest_libpkg ] && longest_libpkg=$length
	done

	# Determine GNU toolchain architecture.
	case $arch in
		x86)	arch_gnu="i686-linux-gnu";;
		arm32)	arch_gnu="arm-linux-gnueabihf";;
		arm64)	arch_gnu="aarch64-linux-gnu";;
		*)	arch_gnu="$arch-linux-gnu";;
	esac

	# Determine library directory name for this architecture.
	case $arch in
		x86)	libdir="i386-linux-gnu";;
		*)	libdir="$arch_gnu";;
	esac

	# Create CMake toolchain file.
	cat << EOF > toolchain.cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $arch)

set(CMAKE_AR $arch_gnu-ar)
set(CMAKE_ASM_COMPILER $arch_gnu-gcc)
set(CMAKE_C_COMPILER $arch_gnu-gcc)
set(CMAKE_CXX_COMPILER $arch_gnu-g++)
set(CMAKE_LINKER $arch_gnu-ld)
set(CMAKE_OBJCOPY $arch_gnu-objcopy)
set(CMAKE_RANLIB $arch_gnu-ranlib)
set(CMAKE_SIZE $arch_gnu-size)
set(CMAKE_STRIP $arch_gnu-strip)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/$libdir/pkgconfig:/usr/share/$libdir/pkgconfig")

include("$(pwd)/cmake/$toolchain.cmake")
EOF
	cmake_flags_extra="$cmake_flags_extra -D CMAKE_TOOLCHAIN_FILE=toolchain.cmake"
	strip_binary="$arch_gnu-strip"

	# Install or update dependencies.
	echo [-] Installing dependencies through apt
	sudo apt-get update
	DEBIAN_FRONTEND=noninteractive sudo apt-get -y install $pkgs $libpkgs
	sudo apt-get clean

	# Link against the system libslirp instead of compiling ours.
	cmake_flags_extra="$cmake_flags_extra -D SLIRP_EXTERNAL=ON"

	# Use OpenAL for Linux builds before FAudio builds are set up.
	cmake_flags_extra="$cmake_flags_extra -D OPENAL=ON"
fi

# Clean workspace.
echo [-] Cleaning workspace
if [ -d "build" ]
then
	MAKEFLAGS=-j$(nproc) cmake --build build --target clean 2> /dev/null
	rm -rf build
fi
find . \( -name Makefile -o -name CMakeCache.txt -o -name CMakeFiles \) -exec rm -rf "{}" \; 2> /dev/null

# Add ARCH to skip the arch_detect process.
case $arch in
	32 | x86)	cmake_flags_extra="$cmake_flags_extra -D ARCH=i386";;
	64 | x86_64)	cmake_flags_extra="$cmake_flags_extra -D ARCH=x86_64";;
	ARM32 | arm32)	cmake_flags_extra="$cmake_flags_extra -D ARCH=arm";;
	ARM64 | arm64)	cmake_flags_extra="$cmake_flags_extra -D ARCH=arm64";;
	*)		cmake_flags_extra="$cmake_flags_extra -D \"ARCH=$arch\"";;
esac

# Add git hash.
git_hash=$(git rev-parse --short HEAD 2> /dev/null)
if [ "$CI" = "true" ]
then
	# Backup strategy when running under Jenkins.
	[ -z "$git_hash" ] && git_hash=$(echo $GIT_COMMIT | cut -c 1-8)
elif [ ! -z "$git_hash" ]
then
	# Append + to denote a dirty tree.
	git diff --quiet 2> /dev/null || git_hash="$git_hash+"
fi
[ ! -z "$git_hash" ] && cmake_flags_extra="$cmake_flags_extra -D \"EMU_GIT_HASH=$git_hash\""

# Add copyright year.
year=$(date +%Y)
[ ! -z "$year" ] && cmake_flags_extra="$cmake_flags_extra -D \"EMU_COPYRIGHT_YEAR=$year\""

# Run CMake.
echo [-] Running CMake with flags [$cmake_flags $cmake_flags_extra]
eval cmake -G \"Unix Makefiles\" -B build $cmake_flags $cmake_flags_extra .
status=$?
if [ $status -ne 0 ]
then
	echo [!] CMake failed with status [$status]
	exit 3
fi

# Run actual build.
make_flags=-j$(nproc)
echo [-] Running build with make flags [$make_flags]
MAKEFLAGS=$make_flags cmake --build build
status=$?
if [ $status -ne 0 ]
then
	echo [!] Make failed with status [$status]
	exit 4
fi

# Download Discord Game SDK from their CDN if necessary.
if [ ! -e "discord_game_sdk.zip" ]
then
	echo [-] Downloading Discord Game SDK
	wget -qO discord_game_sdk.zip "https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip"
	status=$?
	if [ $status -ne 0 ]
	then
		echo [!] Discord Game SDK download failed with status [$status]
		rm -f discord_game_sdk.zip
	fi
fi

# Determine Discord Game SDK architecture.
case $arch in
	32)	arch_discord="x86";;
	64)	arch_discord="x86_64";;
	*)	arch_discord="$arch";;
esac

# Create temporary directory for archival.
echo [-] Gathering archive files
rm -rf archive_tmp
mkdir archive_tmp
if [ ! -d "archive_tmp" ]
then
	echo [!] Archive directory creation failed
	exit 5
fi

# Archive the executable and its dependencies.
# The executable should always be archived last for the check after this block.
status=0
if is_windows
then
	# Determine Program Files directory for Ghostscript and 7-Zip.
	# Manual checks because MSYS is bad at passing the ProgramFiles variables.
	pf="/c/Program Files"
	sevenzip="$pf/7-Zip/7z.exe"
	[ "$arch" = "32" -a -d "/c/Program Files (x86)" ] && pf="/c/Program Files (x86)"

	# Archive freetype from local MSYS installation.
	.ci/static2dll.sh -p freetype2 /$MSYSTEM/lib/libfreetype.a archive_tmp/freetype.dll

	# Archive Ghostscript DLL from local official distribution installation.
	for gs in "$pf"/gs/gs*.*.*
	do
		cp -p "$gs"/bin/gsdll*.dll archive_tmp/
	done

	# Archive Discord Game SDK DLL.
	"$sevenzip" e -y -o"archive_tmp" discord_game_sdk.zip "lib/$arch_discord/discord_game_sdk.dll"
	[ ! -e "archive_tmp/discord_game_sdk.dll" ] && echo [!] No Discord Game SDK for architecture [$arch_discord]

	# Archive other DLLs from local directory.
	cp -p "/home/$project/dll$arch/"* archive_tmp/

	# Archive executable, while also stripping it if requested.
	if [ $strip -ne 0 ]
	then
		"$strip_binary" -o "archive_tmp/$project.exe" "build/src/$project.exe"
		status=$?
	else
		mv "build/src/$project.exe" "archive_tmp/$project.exe"
		status=$?
	fi
elif is_mac
then
	# TBD
	:
else
	cwd_root=$(pwd)

	# Build openal-soft 1.21.1 manually to fix audio issues. This is a temporary
	# workaround until a newer version of openal-soft trickles down to Debian repos.
	if [ -d "openal-soft-1.21.1" ]
	then
		rm -rf openal-soft-1.21.1/build/*
	else
		wget -qO - https://github.com/kcat/openal-soft/archive/refs/tags/1.21.1.tar.gz | tar zxf -
	fi
	cd openal-soft-1.21.1/build
	[ -e Makefile ] && make clean
	cmake -G "Unix Makefiles" -D "CMAKE_TOOLCHAIN_FILE=$cwd_root/toolchain.cmake" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" ..
	make -j$(nproc) install || exit 99
	cd "$cwd_root"

	# Build rtmidi without JACK support to remove the dependency on libjack.
	if [ -d "rtmidi-4.0.0" ]
	then
		rm -rf rtmidi-4.0.0/CMakeCache.txt rtmidi-4.0.0/CMakeFiles
	else
		wget -qO - http://www.music.mcgill.ca/~gary/rtmidi/release/rtmidi-4.0.0.tar.gz | tar zxf -
	fi
	cwd_root=$(pwd)
	cd rtmidi-4.0.0
	[ -e Makefile ] && make clean
	cmake -G "Unix Makefiles" -D RTMIDI_API_JACK=OFF -D "CMAKE_TOOLCHAIN_FILE=$cwd_root/toolchain.cmake" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" .
	make -j$(nproc) install || exit 99
	cd "$cwd_root"

	# Build SDL2 for joystick support with most components disabled to remove the dependencies on PulseAudio and libdrm.
	if [ ! -d "SDL2-2.0.20" ]
	then
		wget -qO - https://www.libsdl.org/release/SDL2-2.0.20.tar.gz | tar zxf -
	fi
	rm -rf sdlbuild
	mkdir sdlbuild
	cd sdlbuild
	cmake -G "Unix Makefiles" -D SDL_DISKAUDIO=OFF -D SDL_DIRECTFB_SHARED=OFF -D SDL_OPENGL=OFF -D SDL_OPENGLES=OFF -D SDL_OSS=OFF -D SDL_ALSA=OFF \
		-D SDL_ALSA_SHARED=OFF -D SDL_JACK=OFF -D SDL_JACK_SHARED=OFF -D SDL_ESD=OFF -D SDL_ESD_SHARED=OFF -D SDL_PIPEWIRE=OFF -D SDL_PIPEWIRE_SHARED=OFF \
		-D SDL_PULSEAUDIO=OFF -D SDL_PULSEAUDIO_SHARED=OFF -D SDL_ARTS=OFF -D SDL_ARTS_SHARED=OFF -D SDL_NAS=OFF -D SDL_NAS_SHARED=OFF -D SDL_SNDIO=OFF \
		-D SDL_SNDIO_SHARED=OFF -D SDL_FUSIONSOUND=OFF -D SDL_FUSIONSOUND_SHARED=OFF -D SDL_LIBSAMPLERATE=OFF -D SDL_LIBSAMPLERATE_SHARED=OFF -D SDL_X11=OFF \
		-D SDL_X11_SHARED=OFF -D SDL_WAYLAND=OFF -D SDL_WAYLAND_SHARED=OFF -D SDL_WAYLAND_LIBDECOR=OFF -D SDL_WAYLAND_LIBDECOR_SHARED=OFF \
		-D SDL_WAYLAND_QT_TOUCH=OFF -D SDL_RPI=OFF -D SDL_VIVANTE=OFF -D SDL_VULKAN=OFF -D SDL_KMSDRM=OFF -D SDL_KMSDRM_SHARED=OFF -D SDL_OFFSCREEN=OFF \
		-D SDL_HIDAPI_JOYSTICK=ON -D SDL_VIRTUAL_JOYSTICK=ON -D SDL_SHARED=ON -D SDL_STATIC=OFF -S "$cwd_root/SDL2-2.0.20" \
		-D "CMAKE_TOOLCHAIN_FILE=$cwd_root/toolchain.cmake" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr"
	make -j$(nproc) install || exit 99
	cd "$cwd_root"

	# Archive Discord Game SDK library.
	7z e -y -o"archive_tmp/usr/lib" discord_game_sdk.zip "lib/$arch_discord/discord_game_sdk.so"
	[ ! -e "archive_tmp/usr/lib/discord_game_sdk.so" ] && echo [!] No Discord Game SDK for architecture [$arch_discord]

	# Archive readme with library package versions.
	echo Libraries used to compile this $arch build of $project: > archive_tmp/README
	dpkg-query -f '${Package} ${Version}\n' -W $libpkgs | sed "s/-dev / /" | sed "s/qtdeclarative/qt/" | while IFS=" " read pkg version
	do
		for i in $(seq $(expr $longest_libpkg - $(echo -n $pkg | wc -c)))
		do
			echo -n " " >> archive_tmp/README
		done
		echo $pkg $version >> archive_tmp/README
	done

	# Archive icon, while also shrinking it to 512x512 if necessary.
	convert src/win/assets/$project_lower.png -resize '512x512>' icon.png
	icon_base="$(identify -format 'archive_tmp/usr/share/icons/%wx%h' icon.png)"
	mkdir -p "$icon_base"
	mv icon.png "$icon_base/$project_lower.png"

	# Archive executable, while also stripping it if requested.
	mkdir -p archive_tmp/usr/local/bin
	if [ $strip -ne 0 ]
	then
		"$strip_binary" -o "archive_tmp/usr/local/bin/$project" "build/src/$project"
		status=$?
	else
		mv "build/src/$project" "archive_tmp/usr/local/bin/$project"
		status=$?
	fi
fi

# Check if the executable strip/move succeeded.
if [ $status -ne 0 ]
then
	echo [!] Executable strip/move failed with status [$status]
	exit 6
fi

# Produce artifact archive.
echo [-] Creating artifact archive
if is_windows
then
	# Create zip.
	cd archive_tmp
	"$sevenzip" a -y "$(cygpath -w "$cwd")\\$package_name.zip" *
	status=$?
elif is_mac
then
	# TBD
	:
else
	# Determine AppImage runtime architecture.
	case $arch in
		x86)	arch_appimage="i686";;
		arm32)	arch_appimage="armhf";;
		arm64)	arch_appimage="aarch64";;
		*)	arch_appimage="$arch";;
	esac

	# Get version for AppImage metadata.
	project_version=$(grep -oP '#define\s+EMU_VERSION\s+"\K([^"]+)' "build/src/include/$project_lower/version.h" 2> /dev/null)
	[ -z "$project_version" ] && project_version=unknown
	build_num=$(grep -oP '#define\s+EMU_BUILD_NUM\s+\K([0-9]+)' "build/src/include/$project_lower/version.h" 2> /dev/null)
	[ ! -z "$build_num" -a "$build_num" != "0" ] && project_version="$project_version-b$build_num"

	# Download appimage-builder if necessary.
	[ ! -e "appimage-builder.AppImage" ] && wget -qO appimage-builder.AppImage \
		https://github.com/AppImageCrafters/appimage-builder/releases/download/v0.9.2/appimage-builder-0.9.2-35e3eab-x86_64.AppImage
	chmod u+x appimage-builder.AppImage

	# Remove any dangling AppImages which may interfere with the renaming process.
	rm -rf "$project-"*".AppImage"

	# Run appimage-builder in extract-and-run mode for Docker compatibility.
	project="$project" project_lower="$project_lower" project_version="$project_version" arch_deb="$arch_deb" arch_appimage="$arch_appimage" \
		APPIMAGE_EXTRACT_AND_RUN=1 ./appimage-builder.AppImage --recipe .ci/AppImageBuilder.yml
	status=$?

	# Rename AppImage to the final name if the build succeeded.
	if [ $status -eq 0 ]
	then
		mv "$project-"*".AppImage" "$cwd/$package_name.AppImage"
		status=$?
	fi
fi
cd ..

# Check if the archival succeeded.
if [ $status -ne 0 ]
then
	echo [!] Artifact archive creation failed with status [$status]
	exit 7
fi

# All good.
echo [-] Build of [$package_name] for [$arch] with flags [$cmake_flags] successful
exit 0

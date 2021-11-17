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
#		Copyright 2021 RichardG.
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

try_make() {
	# Try makefiles on two locations. I don't know what causes
	# CMake to pick ./ instead of build/, but :worksonmymachine:
	if [ -e "build/Makefile" ]
	then
		build_dir="$(pwd)/build"
		cd build
		make -j$(nproc) $*
		local status=$?
		cd ..
		return $status
	elif [ -e "Makefile" ]
	then
		build_dir="$(pwd)"
		make -j$(nproc) $*
		return $?
	else
		echo [!] No makefile found
		return 1
	fi
}

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
cwd=$(pwd)

# Parse arguments.
package_name=
arch=
tarball_name=
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

		*)
			break
			;;
	esac
done
cmake_flags=$*
cmake_flags_extra=

# Check if mandatory arguments were specified.
if [ -z "$package_name" -a -z "$tarball_name" ]
then
	echo '[!] Usage: build.sh -b {package_name} {architecture} [cmake_flags...]'
	echo '           build.sh -s {source_tarball_name}'
	exit 100
fi

# Switch to the correct directory.
[ -e "build.sh" ] && cd ..

# Make source tarball if requested.
if [ ! -z "$tarball_name" ]
then
	echo [-] Making source tarball [$tarball_name]

	# Clean local tree of gitignored files.
	git clean -dfX

	# Save current HEAD commit to VERSION.
	git log --stat -1 > VERSION || rm -f VERSION

	# Archive source.
	make_tar $tarball_name.tar
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

# Perform platform-specific setup.
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
			CHERE_INVOKING=yes MSYSTEM="$msys" bash -lc 'exec "'"$0"'" -b "'"$package_name"'" "'"$arch"'" '"$cmake_flags"
			exit $?
		fi
	else
		echo [!] No MSYSTEM for architecture [$arch]
		exit 2
	fi
	echo [-] Using MSYSTEM [$MSYSTEM]
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

	# Establish general and architecture-specific dependencies.
	pkgs="cmake git tar xz-utils dpkg-dev rpm"
	if [ "$(dpkg --print-architecture)" = "$arch_deb" ]
	then
		pkgs="$pkgs build-essential"
	else
		sudo dpkg --add-architecture $arch_deb
		pkgs="$pkgs crossbuild-essential-$arch_deb"
	fi
	libpkgs=""
	longest_libpkg=0
	for pkg in libc6-dev linux-libc-dev libopenal-dev libfreetype6-dev libsdl2-dev libpng-dev
	do
		libpkgs="$libpkgs $pkg:$arch_deb"
		length=$(echo -n $pkg | sed 's/-dev$//g' | wc -c)
		[ $length -gt $longest_libpkg ] && longest_libpkg=$length
	done

	# Determine GNU toolchain architecture.
	case $arch in
		x86)	arch_gnu="i686-linux-gnu";;
		arm32)	arch_gnu="arm-linux-gnueabihf";;
		arm64)	arch_gnu="aarch64-linux-gnu";;
		*)	arch_gnu="$arch-linux-gnu";;
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
EOF
	cmake_flags_extra="$cmake_flags_extra -D CMAKE_TOOLCHAIN_FILE=toolchain.cmake"

	# Install or update dependencies.
	echo [-] Installing dependencies through apt
	sudo apt-get update
	DEBIAN_FRONTEND=noninteractive sudo apt-get -y install $pkgs $libpkgs
	sudo apt-get clean
fi

# Clean workspace.
echo [-] Cleaning workspace
try_make clean > /dev/null
rm -rf build
find . \( -name Makefile -o -name CMakeCache.txt -o -name CMakeFiles \) -exec rm -rf "{}" \; 2> /dev/null

# Determine ARCH to skip the arch_detect process.
case $arch in
	32 | x86)    cmake_flags_extra="$cmake_flags_extra -D ARCH=i386";;
	64 | x86_64) cmake_flags_extra="$cmake_flags_extra -D ARCH=x86_64";;
	ARM32 | arm32) cmake_flags_extra="$cmake_flags_extra -D ARCH=arm";;
	ARM64 | arm64) cmake_flags_extra="$cmake_flags_extra -D ARCH=arm64";;
	*) cmake_flags_extra="$cmake_flags_extra -D ARCH=$arch";;
esac

# Add git hash and copyright year.
git_hash="$(git rev-parse --short HEAD 2> /dev/null)"
[ ! -z "$git_hash" ] && cmake_flags_extra="$cmake_flags_extra -D EMU_GIT_HASH=\"$git_hash\""
cmake_flags_extra="$cmake_flags_extra -D EMU_COPYRIGHT_YEAR=\"$(date +%Y)\""

# Run CMake.
echo [-] Running CMake with flags [$cmake_flags $cmake_flags_extra]
eval cmake -G \"Unix Makefiles\" $cmake_flags $cmake_flags_extra .
status=$?
if [ $status -ne 0 ]
then
	echo [!] CMake failed with status [$status]
	exit 3
fi

# Run actual build.
echo [-] Running build
try_make
status=$?
if [ $status -ne 0 ]
then
	echo [!] Make failed with status [$status]
	exit 4
fi

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

	# Archive Discord Game SDK DLL from their CDN.
	discordarch=
	[ "$arch" = "32" ] && discordarch=x86
	[ "$arch" = "64" ] && discordarch=x86_64
	if [ ! -z "$discordarch" ]
	then
		[ ! -e "discord_game_sdk.zip" ] && wget -qOdiscord_game_sdk.zip https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip
		"$sevenzip" e -y -oarchive_tmp discord_game_sdk.zip lib/$discordarch/discord_game_sdk.dll
	fi

	# Archive other DLLs from local directory.
	cp -p /home/$project/dll$arch/* archive_tmp/

	# Archive executable.
	mv "$build_dir"/src/$project.exe archive_tmp/
	status=$?
elif is_mac
then
	# TBD
	:
else
	# Archive readme with library package versions.
	echo Libraries used to compile this $arch build of $project: > archive_tmp/README
	dpkg-query -f '${Package} ${Version}\n' -W $libpkgs | sed "s/-dev / /g" | while IFS=" " read pkg version
	do
		for i in $(seq $(expr $longest_libpkg - $(echo -n $pkg | wc -c)))
		do
			echo -n " " >> archive_tmp/README
		done
		echo $pkg $version >> archive_tmp/README
	done

	# Archive executable.
	mv "$build_dir"/src/$project archive_tmp/
	status=$?
fi

# Check if the executable move succeeded.
if [ $status -ne 0 ]
then
	echo [!] Executable move failed with status [$status]
	exit 6
fi

# Produce artifact archive.
echo [-] Creating artifact archive
cd archive_tmp
if is_windows
then
	# Create zip.
	"$sevenzip" a -y -mx9 "..\\$package_name.zip" *
	status=$?
elif is_mac
then
	# TBD
	:
else
	# Create binary tarball.
	VERBOSE=1 make_tar ../$package_name.tar
	status=$?
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

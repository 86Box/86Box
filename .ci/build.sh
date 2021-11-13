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

build() {
	# Create a line gap between builds.
	[ $first_build -eq 0 ] && echo
	first_build=0

	# Set argument and environment variables.
	local job_name=$JOB_BASE_NAME
	local build_type=$BUILD_TYPE
	local git_hash=$(echo $GIT_COMMIT | cut -c1-8)
	local arch=$1
	shift
	local cmake_flags=$*
	local cmake_flags_extra=

	# Check if at least the job name was received.
	if [ -z "$job_name" ]
	then
		echo [!] Missing environment variables: received JOB_BASE_NAME=[$JOB_BASE_NAME] BUILD_TYPE=[$BUILD_TYPE] BUILD_NUMBER=[$BUILD_NUMBER] GIT_COMMIT=[$GIT_COMMIT]
		return 1
	fi

	echo [-] Building [$job_name] [$build_type] [$build_qualifier] [$git_hash] for [$arch] with flags [$cmake_flags]

	# Switch to the correct directory.
	cd "$cwd"
	[ -e "build.sh" ] && cd ..

	# Perform platform-specific setup.
	if is_windows
	then
		# Switch into the correct MSYSTEM if required.
		local msys=MINGW$arch
		[ ! -d "/$msys" ] && local msys=CLANG$arch
		if [ -d "/$msys" ]
		then
			if [ "$MSYSTEM" != "$msys" ]
			then
				# Call build with the correct MSYSTEM.
				echo [-] Switching to MSYSTEM [$msys]
				cd "$cwd"
				CHERE_INVOKING=yes MSYSTEM="$msys" JOB_BASE_NAME="$JOB_BASE_NAME" BUILD_TYPE="$BUILD_TYPE" BUILD_NUMBER="$BUILD_NUMBER" GIT_COMMIT="$GIT_COMMIT" \
					bash -lc 'exec "'$0'" -b "'$arch'" '$cmake_flags && job_status=0 # make sure the main script exits cleanly on any success
				return $?
			fi
		else
			echo [!] No MSYSTEM for architecture [$arch]
			return 2
		fi
		echo [-] Using MSYSTEM [$MSYSTEM]
	elif is_mac
	then
		# macOS lacks nproc, but sysctl can do the same job.
		alias nproc='sysctl -n hw.logicalcpu'
	else
		# Determine Debian architecture.
		case $arch in
			x86)	local arch_deb="i386";;
			x86_64)	local arch_deb="amd64";;
			arm32)	local arch_deb="armhf";;
			*)	local arch_deb="$arch";;
		esac

		# Establish general and architecture-specific dependencies.
		local pkgs="cmake git tar xz-utils dpkg-dev rpm"
		if [ "$(dpkg --print-architecture)" = "$arch_deb" ]
		then
			local pkgs="$pkgs build-essential"
		else
			sudo dpkg --add-architecture $arch_deb
			local pkgs="$pkgs crossbuild-essential-$arch_deb"
		fi
		local libpkgs=""
		local longest_libpkg=0
		for pkg in libc6-dev linux-libc-dev libopenal-dev libfreetype6-dev libsdl2-dev libpng-dev
		do
			local libpkgs="$libpkgs $pkg:$arch_deb"
			local length=$(echo -n $pkg | sed 's/-dev$//g' | wc -c)
			[ $length -gt $longest_libpkg ] && longest_libpkg=$length
		done

		# Determine GNU toolchain architecture.
		case $arch in
			x86)	local arch_gnu="i686-linux-gnu";;
			arm32)	local arch_gnu="arm-linux-gnueabihf";;
			arm64)	local arch_gnu="aarch64-linux-gnu";;
			*)	local arch_gnu="$arch-linux-gnu";;
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
		local cmake_flags_extra="$cmake_flags_extra -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake"

		# Install or update dependencies.
		echo [-] Installing dependencies through apt
		sudo apt-get update
		DEBIAN_FRONTEND=noninteractive sudo apt-get -y install $pkgs $libpkgs
		sudo apt-get clean
	fi

	# Clean workspace.
	echo [-] Cleaning workspace
	try_make clean > /dev/null
	find . \( -name Makefile -o -name CMakeCache.txt -o -name CMakeFiles \) -exec rm -rf "{}" \; 2> /dev/null
	rm -rf build

	# Determine available dynarec types for this architecture, and
	# also specify ARCH right away to skip the arch_detect process.
	case $arch in
		# old dynarec available
		32 | x86)    local cmake_flags_extra="$cmake_flags_extra -D ARCH=i386";;
		64 | x86_64) local cmake_flags_extra="$cmake_flags_extra -D ARCH=x86_64";;
		# new dynarec only
		ARM32 | arm32) local cmake_flags_extra="$cmake_flags_extra -D NEW_DYNAREC=ON -D ARCH=arm";;
		ARM64 | arm64) local cmake_flags_extra="$cmake_flags_extra -D NEW_DYNAREC=ON -D ARCH=arm64";;
		# no dynarec
		*) local cmake_flags_extra="$cmake_flags_extra -D DYNAREC=OFF";;
	esac

	# Determine additional CMake flags.
	[ ! -z "$build_type" ] && local cmake_flags_extra="$cmake_flags_extra -D BUILD_TYPE=\"$build_type\""
	[ ! -z "$build_qualifier" ] && local cmake_flags_extra="$cmake_flags_extra -D EMU_BUILD=\"$build_qualifier\""
	[ ! -z "$build_number" ] && local cmake_flags_extra="$cmake_flags_extra -D EMU_BUILD_NUM=\"$build_number\""
	[ ! -z "$git_hash" ] && local cmake_flags_extra="$cmake_flags_extra -D EMU_GIT_HASH=\"$git_hash\""
	local cmake_flags_extra="$cmake_flags_extra -D EMU_COPYRIGHT_YEAR=\"$(date +%Y)\""

	# Run CMake.
	echo [-] Running CMake with flags [$cmake_flags $cmake_flags_extra]
	eval cmake -G \"Unix Makefiles\" $cmake_flags $cmake_flags_extra .
	local status=$?
	if [ $? -gt 0 ]
	then
		echo [!] CMake failed with status [$status]
		return 3
	fi

	# Run actual build.
	echo [-] Running build
	try_make
	local status=$?
	if [ $status -gt 0 ]
	then
		echo [!] Make failed with status [$status]
		return 4
	fi

	# Create temporary directory for archival.
	echo [-] Gathering archive files
	rm -rf archive_tmp
	mkdir archive_tmp
	if [ ! -d "archive_tmp" ]
	then
		echo [!] Archive directory creation failed
		return 5
	fi

	# Archive the executable and its dependencies.
	# The executable should always be archived last for the check after this block.
	local status=$?
	if is_windows
	then
		# Determine Program Files directory for Ghostscript and 7-Zip.
		# Manual checks because MSYS is bad at passing the ProgramFiles variables.
		local pf="/c/Program Files"
		local sevenzip="$pf/7-Zip/7z.exe"
		[ "$arch" = "32" -a -d "/c/Program Files (x86)" ] && pf="/c/Program Files (x86)"

		# Archive freetype from local MSYS installation.
		.ci/static2dll.sh -p freetype2 /$MSYSTEM/lib/libfreetype.a archive_tmp/libfreetype.dll

		# Archive Ghostscript DLL from local official distribution installation.
		for gs in "$pf"/gs/gs*.*.*
		do
			cp -p "$gs"/bin/gsdll*.dll archive_tmp/
		done

		# Archive Discord Game SDK DLL from their CDN.
		local discordarch=
		[ "$arch" = "32" ] && local discordarch=x86
		[ "$arch" = "64" ] && local discordarch=x86_64
		if [ ! -z "$discordarch" ]
		then
			[ ! -e "discord_game_sdk.zip" ] && wget -qOdiscord_game_sdk.zip https://dl-game-sdk.discordapp.net/2.5.6/discord_game_sdk.zip
			"$sevenzip" e -y -oarchive_tmp discord_game_sdk.zip lib/$discordarch/discord_game_sdk.dll
		fi

		# Archive other DLLs from local directory.
		cp -p /home/$project/dll$arch/* archive_tmp/

		# Archive executable.
		mv "$build_dir"/src/$project.exe archive_tmp/
		local status=$?
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
		local status=$?
	fi

	# Check if the executable move succeeded.
	if [ $status -gt 0 ]
	then
		echo [!] Executable move failed with status [$status]
		return 6
	fi

	# Produce artifact archive.
	echo [-] Creating artifact archive
	cd archive_tmp
	if is_windows
	then
		# Create zip.
		"$sevenzip" a -y -mx9 "..\\$job_name-Windows-$arch$build_fn.zip" *
		local status=$?
	elif is_mac
	then
		# TBD
		:
	else
		# Create binary tarball.
		make_tar ../$job_name-Linux-$arch$build_fn.tar
		local status=$?
	fi
	cd ..

	# Check if the archival succeeded.
	if [ $status -gt 0 ]
	then
		echo [!] Artifact archive creation failed with status [$status]
		return 7
	fi

	# All good.
	echo [-] Build of [$job_name] [$build_type] [$build_qualifier] [$git_hash] for [$arch] with flags [$cmake_flags] successful
	job_status=0
}

tarball() {
	# Create a line gap between builds.
	[ $first_build -eq 0 ] && echo
	first_build=0

	# Set argument and environment variables.
	local job_name=$JOB_BASE_NAME

	# Check if the job name was received.
	if [ -z "$job_name" ]
	then
		echo [!] Missing environment variable: received JOB_BASE_NAME=[$JOB_BASE_NAME]
		return 1
	fi

	echo [-] Making source tarball for [$job_name]

	# Switch to the correct directory.
	cd "$cwd"
	[ -e "build.sh" ] && cd ..

	# Clean local tree of gitignored files.
	git clean -dfX

	# Save current HEAD commit to VERSION.
	git log -1 > VERSION || rm -f VERSION

	# Archive source.
	make_tar $job_name-Source$build_fn.tar

	# Check if the archival succeeded.
	if [ $? -gt 0 ]
	then
		echo [!] Tarball creation failed with status [$status]
		return 2
	fi

	echo [-] Source tarball for [$job_name] created successfully
}

# Set common variables.
project=86Box
cwd=$(pwd)
first_build=1
job_status=1

# Parse arguments.
single_build=0
tarball=0
args=0
while [ $# -gt 0 ]
do
	case $1 in
		-b)
			# Execute single build.
			[ -z "$JOB_BASE_NAME" ] && JOB_BASE_NAME=$project-Custom
			single_build=1
			shift
			break
			;;

		-t)
			# Create tarball.
			[ -z "$JOB_BASE_NAME" ] && JOB_BASE_NAME=$project
			tarball=1
			shift
			;;

		*)
			# Allow for manually specifying Jenkins variables.
			if [ $args -eq 0 ]
			then
				JOB_BASE_NAME=$1
				args=1
			elif [ $args -eq 1 ]
			then
				BUILD_TYPE=$1
				args=2
			elif [ $args -eq 2 ]
			then
				BUILD_NUMBER=$1
				args=3
			elif [ $args -eq 3 ]
			then
				GIT_COMMIT=$1
				args=4
			fi
			shift
			;;
	esac
done

# Check if at least the job name was specified.
if [ -z "$JOB_BASE_NAME" ]
then
	echo [!] Manual usage: build.sh [{job_name} [{build_type} [{build_number'|"'build_qualifier'"'} [git_hash]]]] [-t] [-b {architecture} [cmake_flags...]]
	exit 100
fi

# Generate build information. Note that variable names are case sensitive.
build_number=$BUILD_NUMBER
if echo $build_number | grep -q " "
then
	# A full build qualifier was specified.
	build_qualifier="$build_number"
	build_fn="-"$(echo "$build_number" | rev | cut -f1 -d" " | rev | tr '\\/:*?"<>|' '_')
	build_number= # no build number
elif [ ! -z "$build_number" ]
then
	# A build number was specified.
	build_qualifier="build $build_number"
	build_fn="-b$build_number"
	build_number=$(echo "$build_number" | sed "s/[^0-9]//g") # remove non-numeric characters from build number
else
	# No build data was specified.
	build_number=
	build_qualifier=
	build_fn=
fi

# Make tarball if requested.
if [ $tarball -ne 0 ]
then
	tarball
	status=$?
	[ $single_build -eq 0 ] && exit $status
fi

# Run single build if requested.
if [ $single_build -ne 0 ]
then
	build $*
	exit $?
fi

# Run builds according to the Jenkins job name.
echo Temporarily disabled [$JOB_BASE_NAME]
exit 0
case $JOB_BASE_NAME in
	$project | $project-TestBuildPleaseIgnore)
		if is_windows
		then
			build 32 --preset=regular
			build 64 --preset=regular
		elif is_mac
		then
			build Universal --preset=regular
		else
			tarball
			build x86 --preset=regular
			build x86_64 --preset=regular
			build arm32 --preset=regular
			build arm64 --preset=regular
		fi
		;;

	$project-Debug)
		if is_windows
		then
			build 32 --preset=debug
			build 64 --preset=debug
		elif is_mac
		then
			build Universal --preset=debug
		else
			build x86 --preset=debug
			build x86_64 --preset=debug
			build arm32 --preset=debug
			build arm64 --preset=debug
		fi
		;;

	$project-Dev)
		if is_windows
		then
			build 32 --preset=experimental
			build 64 --preset=experimental
		elif is_mac
		then
			build Universal --preset=experimental
		else
			build x86 --preset=experimental
			build x86_64 --preset=experimental
			build arm32 --preset=experimental
			build arm64 --preset=experimental
		fi
		;;

	*)
		echo [!] Unknown job name $JOB_BASE_NAME
		exit 1
		;;
esac

echo
echo [-] Exiting with status [$job_status]
exit $job_status

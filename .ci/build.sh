#!/bin/sh
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          Jenkins build script.
#
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2021-2023 RichardG.
#

#
# While this script was made for our Jenkins infrastructure, you can run it
# to produce Jenkins-like builds on your local machine by following these notes:
#
# - Run build.sh without parameters to see its usage
# - Any boolean CMake definitions (-D ...=ON/OFF) must be ON or OFF to ensure correct behavior
# - For Windows (MSYS MinGW) builds:
#   - Packaging requires 7-Zip on Program Files
#   - Packaging the Ghostscript DLL requires 32-bit and/or 64-bit Ghostscript on Program Files
#   - Packaging the XAudio2 DLL for FAudio requires it to be at /home/86Box/dll32/xaudio2*.dll
#     and/or /home/86Box/dll64/xaudio2*.dll (for 32-bit and 64-bit builds respectively)
# - For Linux builds:
#   - Only Debian and derivatives are supported
#   - dpkg and apt-get are called through sudo to manage dependencies; make sure those
#     are configured as NOPASSWD in /etc/sudoers if you're doing unattended builds
# - For macOS builds:
#   - A standard MacPorts installation is required, with the following macports.conf settings:
#       buildfromsource always
#       build_arch x86_64 (or arm64)
#       universal_archs (blank)
#       ui_interactive no
#       macosx_deployment_target 10.13 (for x86_64, 10.14 for Qt Vulkan, or 11.0 for arm64)
#   - For universal building on Apple Silicon hardware, install native MacPorts on the default
#     /opt/local and Intel MacPorts on /opt/intel, then tell build.sh to build for "x86_64+arm64"
#   - Qt Vulkan support through MoltenVK requires 10.14 while we target 10.13. We deal with that
#     (at least for now) by abusing the x86_64h universal slice to branch Haswell and newer Macs
#     into a Vulkan-enabled but 10.14+ binary, with older ones opting for a 10.13-compatible,
#     non-Vulkan binary. With this approach, the only machines that miss out on Vulkan despite
#     supporting Metal are Ivy Bridge ones as well as GPU-upgraded Mac Pros. For building that
#     Vulkan binary, install another Intel MacPorts on /opt/x86_64h, then use the "x86_64h"
#     architecture when invoking build.sh (either standalone or as part of an universal build)
#   - port and sed are called through sudo to manage dependencies; make sure those are configured
#     as NOPASSWD in /etc/sudoers if you're doing unattended builds
#

# Define common functions.
alias is_windows='[ -n "$MSYSTEM" ]'
alias is_mac='uname -s | grep -q Darwin'

make_tar() {
	# Install dependencies.
	if ! which tar xz > /dev/null 2>&1
	then
		if which apt-get > /dev/null 2>&1
		then
			sudo apt-get update
			DEBIAN_FRONTEND=noninteractive sudo apt-get install -y tar xz-utils
			sudo apt-get clean
		elif which port > /dev/null 2>&1
		then
			sudo port selfupdate
			sudo port install gnutar xz
		fi
	fi

	# Use MacPorts gnutar (if installed) on macOS.
	local tar_cmd=tar
	which gnutar > /dev/null 2>&1 && local tar_cmd=gnutar

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
	[ -n "$VERBOSE" ] && local compression_flag="$compression_flag -v"

	# tar is notorious for having many diverging implementations. For instance,
	# the flags we use to strip UID/GID metadata can be --owner/group (GNU),
	# --uid/gid (bsdtar) or even none at all (MSYS2 bsdtar). Account for such
	# flag differences by checking if they're mentioned on the help text.
	local ownership_flags=
	local tar_help=$("$tar_cmd" --help 2>&1)
	if echo $tar_help | grep -q -- --owner
	then
		local ownership_flags="--owner=0 --group=0"
	elif echo $tar_help | grep -q -- --uid
	then
		local ownership_flags="--uid 0 --gid 0"
	fi

	# Run tar.
	"$tar_cmd" -c $compression_flag -f "$1$compression_ext" $ownership_flags *
	return $?
}

cache_dir="$HOME/86box-build-cache"
[ ! -d "$cache_dir" ] && mkdir -p "$cache_dir"
check_buildtag() {
	[ -z "$BUILD_TAG" -o "$BUILD_TAG" != "$(cat "$cache_dir/buildtag.$1" 2> /dev/null)" ]
	return $?
}
save_buildtag() {
	local contents="$BUILD_TAG"
	[ -n "$2" ] && local contents="$2"
	echo "$contents" > "$cache_dir/buildtag.$1"
	return $?
}

# Set common variables.
project=86Box
cwd=$(pwd)

# Parse arguments.
package_name=
arch=
tarball_name=
skip_archive=0
dep_report=0
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

		-n)
			shift
			skip_archive=1
			;;

		-p)
			shift

			# Check for lddtree and install it if required.
			which lddtree > /dev/null || DEBIAN_FRONTEND=noninteractive sudo apt-get -y install pax-utils

			# Default to main binary.
			binary="$1"
			[ -z "$binary" ] && binary="archive_tmp/usr/local/bin/$project"

			# Run lddtree with AppImage lib directories included in the search path.
			LD_LIBRARY_PATH=$(find "$(pwd)/archive_tmp" -type d -name lib -o -name lib64 | while read dir; do find "$dir" -type d; done | tr '\n' ':') \
				lddtree "$binary"
			exit $?
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
			# Consume remaining arguments as CMake flags.
			while [ $# -gt 0 ]
			do
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
			done
			;;
	esac
done
cmake_flags_extra=

# Check if mandatory arguments were specified.
if [ -z "$package_name" -a -z "$tarball_name" ] || [ -n "$package_name" -a -z "$arch" ]
then
	echo '[!] Usage: build.sh -b {package_name} {architecture} [-t] [cmake_flags...]'
	echo '           build.sh -s {source_tarball_name}'
	echo 'Dep. tree: build.sh -p [archive_tmp/path/to/binary]'
	exit 100
fi

# Switch to the repository root directory.
cd "$(dirname "$0")/.."

# Make source tarball if requested.
if [ -n "$tarball_name" ]
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
toolchain_prefix=flags-gcc
is_mac && toolchain_prefix=llvm-macos
case $arch in
	32 | x86)	toolchain="$toolchain_prefix-i686";;
	64 | x86_64*)	toolchain="$toolchain_prefix-x86_64";;
	ARM32 | arm32)	toolchain="$toolchain_prefix-armv7";;
	ARM64 | arm64)	toolchain="$toolchain_prefix-aarch64";;
	*)		toolchain="$toolchain_prefix-$arch";;
esac
[ ! -e "cmake/$toolchain.cmake" ] && toolchain=flags-gcc
toolchain_file="cmake/$toolchain.cmake"
toolchain_file_libs=

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
			args=
			[ $strip -ne 0 ] && args="-t $args"
			[ $skip_archive -ne 0 ] && args="-n $args"
			CHERE_INVOKING=yes MSYSTEM="$msys" bash -lc 'exec "'"$0"'" -b "'"$package_name"'" "'"$arch"'" '"$args""$cmake_flags"
			exit $?
		fi
	else
		echo [!] No MSYSTEM for architecture [$arch]
		exit 2
	fi
	echo [-] Using MSYSTEM [$MSYSTEM]

	# Install dependencies only if we're in a new build and/or architecture.
	if check_buildtag "$MSYSTEM"
	then
		# Update databases and keyring only if we're in a new build.
		if check_buildtag pacmansync
		then
			# Update keyring as well, since the package signing keys sometimes change.
			echo [-] Updating package databases and keyring
			pacman -Sy --needed --noconfirm msys2-keyring

			# Save build tag to skip pacman sync/keyring later.
			save_buildtag pacmansync
		else
			echo [-] Not updating package databases and keyring again
		fi

		# Establish general dependencies.
		pkgs="git"

		# Gather installed architecture-specific packages for updating.
		# This prevents outdated shared libraries, unmet dependencies
		# and potentially other issues caused by the fact pacman doesn't
		# update a package's dependencies unless explicitly told to.
		pkgs="$pkgs $(pacman -Quq | grep -E "^$MINGW_PACKAGE_PREFIX-")"

		# Establish architecture-specific dependencies.
		while read pkg rest
		do
			pkgs="$pkgs $MINGW_PACKAGE_PREFIX-$(echo "$pkg" | tr -d '\r')" # CR removal required
		done < .ci/dependencies_msys.txt

		# Install or update dependencies.
		echo [-] Installing dependencies through pacman
		if ! pacman -S --needed --noconfirm $pkgs
		then
			# Install packages individually if installing them all together failed.
			for pkg in $pkgs
			do
				pacman -S --needed --noconfirm "$pkg"
			done
		fi

		# Clean pacman cache when running under Jenkins to save disk space.
		[ "$CI" = "true" ] && rm -rf /var/cache/pacman/pkg

		# Save build tag to skip this later. Doing it here (once everything is
		# in place) is important to avoid potential issues with retried builds.
		save_buildtag "$MSYSTEM"
	else
		echo [-] Not installing dependencies again
	fi
elif is_mac
then
	# macOS lacks nproc, but sysctl can do the same job.
	alias nproc='sysctl -n hw.logicalcpu'

	# Handle universal building.
	if echo "$arch" | grep -q '+'
	then
		# Create temporary directory for merging app bundles.
		rm -rf archive_tmp_universal
		mkdir archive_tmp_universal

		# Build for each architecture.
		merge_src=
		for arch_universal in $(echo "$arch" | tr '+' ' ')
		do
			# Run build for the architecture.
			args=
			[ $strip -ne 0 ] && args="-t $args"
			zsh -lc 'exec "'"$0"'" -n -b "universal slice" "'"$arch_universal"'" '"$args""$cmake_flags"' '"$cmake_flags_extra"
			status=$?

			if [ $status -eq 0 ]
			then
				# Move app bundle to the temporary directory.
				app_bundle_name="archive_tmp/$(ls archive_tmp | grep '.app$')"
				mv "$app_bundle_name" "archive_tmp_universal/$arch_universal.app"
				status=$?

				# Merge app bundles.
				if [ -z "$merge_src" ]
				then
					# This is the first bundle, nothing to merge with.
					merge_src="$arch_universal"
				else
					# Merge previous bundle with this one.
					merge_dest="$merge_src+$arch_universal"
					echo [-] Merging app bundles [$merge_src] and [$arch_universal] into [$merge_dest]

					# Merge directory structures.
					(cd "archive_tmp_universal/$merge_src.app" && find . -type d && cd "../../archive_tmp_universal/$arch_universal.app" && find . -type d && cd ../..) | sort > "$cache_dir/universal_listing.txt"
					cat "$cache_dir/universal_listing.txt" | uniq | while IFS= read line
					do
						echo "> Directory: $line"
						mkdir -p "archive_tmp_universal/$merge_dest.app/$line"
					done

					# Create merged file listing.
					(cd "archive_tmp_universal/$merge_src.app" && find . -type f && cd "../../archive_tmp_universal/$arch_universal.app" && find . -type f && cd ../..) | sort > "$cache_dir/universal_listing.txt"

					# Copy files that only exist on one bundle.
					cat "$cache_dir/universal_listing.txt" | uniq -u | while IFS= read line
					do
						if [ -e "archive_tmp_universal/$merge_src.app/$line" ]
						then
							file_src="$merge_src"
						else
							file_src="$arch_universal"
						fi
						echo "> Only on [$file_src]: $line"
						cp -p "archive_tmp_universal/$file_src.app/$line" "archive_tmp_universal/$merge_dest.app/$line"
					done

					# Copy or lipo files that exist on both bundles.
					cat "$cache_dir/universal_listing.txt" | uniq -d | while IFS= read line
					do
						if cmp -s "archive_tmp_universal/$merge_src.app/$line" "archive_tmp_universal/$arch_universal.app/$line"
						then
							echo "> Identical: $line"
							cp -p "archive_tmp_universal/$merge_src.app/$line" "archive_tmp_universal/$merge_dest.app/$line"
						elif lipo -create -output "archive_tmp_universal/$merge_dest.app/$line" "archive_tmp_universal/$merge_src.app/$line" "archive_tmp_universal/$arch_universal.app/$line" 2> /dev/null
						then
							echo "> Merged: $line"
						else
							echo "> Copied from [$merge_src]: $line"
							cp -p "archive_tmp_universal/$merge_src.app/$line" "archive_tmp_universal/$merge_dest.app/$line"
						fi
					done

					# Merge symlinks.
					(cd "archive_tmp_universal/$merge_src.app" && find . -type l && cd "../../archive_tmp_universal/$arch_universal.app" && find . -type l && cd ../..) | sort > "$cache_dir/universal_listing.txt"
					cat "$cache_dir/universal_listing.txt" | uniq | while IFS= read line
					do
						# Get symlink destinations.
						other_link_dest=
						if [ -e "archive_tmp_universal/$merge_src.app/$line" ]
						then
							file_src="$merge_src"
							other_link_path="archive_tmp_universal/$arch_universal.app/$line"
							if [ -L "$other_link_path" ]
							then
								other_link_dest="$(readlink "$other_link_path")"
							elif [ -e "$other_link_path" ]
							then
								other_link_dest='[not a symlink]'
							fi
						else
							file_src="$arch_universal"
						fi
						link_dest="$(readlink "archive_tmp_universal/$file_src.app/$line")"

						# Warn if destinations differ.
						if [ -n "$other_link_dest" -a "$link_dest" != "$other_link_dest" ]
						then
							echo "> Symlink: $line => WARNING: different targets"
							echo ">> Using: [$merge_src] $link_dest"
							echo ">> Other: [$arch_universal] $other_link_dest"
						else
							echo "> Symlink: $line => $link_dest"
						fi
						ln -s "$link_dest" "archive_tmp_universal/$merge_dest.app/$line"
					done

					# Merge a subsequent bundle with this one.
					merge_src="$merge_dest"	
				fi
			fi

			if [ $status -ne 0 ]
			then
				echo [!] Aborting universal build: [$arch_universal] failed with status [$status]
				exit $status
			fi
		done

		# Rename final app bundle.
		rm -rf archive_tmp
		mkdir archive_tmp
		mv "archive_tmp_universal/$merge_src.app" "$app_bundle_name"

		# Sign final app bundle.
		arch -"$(uname -m)" codesign --force --deep -s - "$app_bundle_name"

		# Create zip.
		echo [-] Creating artifact archive
		cd archive_tmp
		zip --symlinks -r "$cwd/$package_name.zip" .
		status=$?

		# Check if the archival succeeded.
		if [ $status -ne 0 ]
		then
			echo [!] Artifact archive creation failed with status [$status]
			exit 7
		fi

		# All good.
		echo [-] Universal build of [$package_name] for [$arch] with flags [$cmake_flags] successful
		exit 0
	fi

	# Switch into the correct architecture if required.
	case $arch in
		x86_64*) arch_mac="i386"; arch_cmd="x86_64";;
		*)	 arch_mac="$arch"; arch_cmd="$arch";;
	esac
	if [ "$(arch)" != "$arch" -a "$(arch)" != "$arch_mac" ]
	then
		# Call build with the correct architecture.
		echo [-] Switching to architecture [$arch]
		cd "$cwd"
		args=
		[ $strip -ne 0 ] && args="-t $args"
		[ $skip_archive -ne 0 ] && args="-n $args"
		arch -"$arch_cmd" zsh -lc 'exec "'"$0"'" -b "'"$package_name"'" "'"$arch"'" '"$args""$cmake_flags"
		exit $?
	fi
	echo [-] Using architecture [$(arch)]

	# Locate the MacPorts prefix.
	macports="/opt/local"
	[ -e "/opt/$arch/bin/port" ] && macports="/opt/$arch"
	[ "$arch" = "x86_64" -a -e "/opt/intel/bin/port" ] && macports="/opt/intel"
	export PATH="$macports/bin:$macports/sbin:$macports/libexec/qt5/bin:$PATH"

	# Enable MoltenVK on x86_64h and arm64, but not on x86_64.
	# The rationale behind that is explained on the big comment up top.
	moltenvk=0
	if [ "$arch" != "x86_64" ]
	then
		moltenvk=1
		cmake_flags_extra="$cmake_flags_extra -D MOLTENVK=ON -D \"MOLTENVK_INCLUDE_DIR=$macports\""
	fi

	# Install dependencies only if we're in a new build and/or MacPorts prefix.
	if check_buildtag "$(basename "$macports")"
	then
		# Install dependencies.
		echo [-] Installing dependencies through MacPorts
		sudo "$macports/bin/port" selfupdate
		if [ $moltenvk -ne 0 ]
		then
			# Patch Qt to enable Vulkan support where supported.
			qt5_portfile="$macports/var/macports/sources/rsync.macports.org/macports/release/tarballs/ports/aqua/qt5/Portfile"
			sudo sed -i -e 's/-no-feature-vulkan/-feature-vulkan/g' "$qt5_portfile"
			sudo sed -i -e 's/configure.env-append MAKE=/configure.env-append VULKAN_SDK=${prefix} MAKE=/g' "$qt5_portfile"
		fi
		while :
		do
			# Attempt to install dependencies.
			sudo "$macports/bin/port" install $(cat .ci/dependencies_macports.txt) 2>&1 | tee macports.log

			# Check for port activation errors.
			stuck_dep=$(grep " cannot be built while another version of " macports.log | cut -d" " -f10)
			if [ -n "$stuck_dep" ]
			then
				# Deactivate the stuck dependency and try again.
				sudo "$macports/bin/port" -f deactivate "$stuck_dep"
				continue
			fi

			stuck_dep=$(grep " Please deactivate this port first, or " macports.log | cut -d" " -f5 | tr -d :)
			if [ -n "$stuck_dep" ]
			then
				# Activate the stuck dependency and try again.
				sudo "$macports/bin/port" -f activate "$stuck_dep"
				continue
			fi

			# Stop if no errors were found.
			break
		done

		# Remove MacPorts error detection log.
		rm -f macports.log

		# Save build tag to skip this later. Doing it here (once everything is
		# in place) is important to avoid potential issues with retried builds.
		save_buildtag "$(basename "$macports")"
	else
		echo [-] Not installing dependencies again

	fi
else
	# Determine Debian architecture.
	case $arch in
		x86)	arch_deb="i386";;
		x86_64)	arch_deb="amd64";;
		arm32)	arch_deb="armhf";;
		*)	arch_deb="$arch";;
	esac
        grep -q " bullseye " /etc/apt/sources.list || echo [!] WARNING: System not running the expected Debian version

	# Establish general dependencies.
	pkgs="cmake ninja-build pkg-config git wget p7zip-full extra-cmake-modules wayland-protocols tar gzip file appstream"
	if [ "$(dpkg --print-architecture)" = "$arch_deb" ]
	then
		pkgs="$pkgs build-essential"
	else
		# Add foreign architecture if required.
		if ! dpkg --print-foreign-architectures | grep -Fqx "$arch_deb"
		then
			sudo dpkg --add-architecture "$arch_deb"

			# Force an apt-get update.
			save_buildtag aptupdate "arch_$arch_deb"
		fi

		pkgs="$pkgs crossbuild-essential-$arch_deb"
	fi

	# Establish architecture-specific dependencies we don't want listed on the readme...
	pkgs="$pkgs linux-libc-dev:$arch_deb qttools5-dev:$arch_deb qtbase5-private-dev:$arch_deb"

	# ...and the ones we do want listed. Non-dev packages fill missing spots on the list.
	libpkgs=""
	longest_libpkg=0
	for pkg in libc6-dev libstdc++6 libopenal-dev libfreetype6-dev libx11-dev libsdl2-dev libpng-dev librtmidi-dev qtdeclarative5-dev libwayland-dev libevdev-dev libxkbcommon-x11-dev libglib2.0-dev libslirp-dev libfaudio-dev libaudio-dev libjack-jackd2-dev libpipewire-0.3-dev libsamplerate0-dev libsndio-dev libvdeplug-dev libfluidsynth-dev libsndfile1-dev
	do
		libpkgs="$libpkgs $pkg:$arch_deb"
		length=$(echo -n $pkg | sed 's/-dev$//' | sed "s/qtdeclarative/qt/" | wc -c)
		[ $length -gt $longest_libpkg ] && longest_libpkg=$length
	done

	# Determine toolchain architecture triplet.
	case $arch in
		x86)	arch_triplet="i686-linux-gnu";;
		arm32)	arch_triplet="arm-linux-gnueabihf";;
		arm64)	arch_triplet="aarch64-linux-gnu";;
		*)	arch_triplet="$arch-linux-gnu";;
	esac

	# Determine library directory name for this architecture.
	case $arch in
		x86)	libdir="i386-linux-gnu";;
		*)	libdir="$arch_triplet";;
	esac

	# Create CMake cross toolchain file.
	toolchain_file_new="$cache_dir/toolchain.$arch_deb.cmake"
	cat << EOF > "$toolchain_file_new"
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $arch)

set(CMAKE_AR $arch_triplet-ar)
set(CMAKE_ASM_COMPILER $arch_triplet-gcc)
set(CMAKE_C_COMPILER $arch_triplet-gcc)
set(CMAKE_CXX_COMPILER $arch_triplet-g++)
set(CMAKE_LINKER $arch_triplet-ld)
set(CMAKE_OBJCOPY $arch_triplet-objcopy)
set(CMAKE_RANLIB $arch_triplet-ranlib)
set(CMAKE_SIZE $arch_triplet-size)
set(CMAKE_STRIP $arch_triplet-strip)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/$libdir/pkgconfig:/usr/share/$libdir/pkgconfig:/usr/share/pkgconfig")

include("$(realpath "$toolchain_file")")
EOF
	toolchain_file="$toolchain_file_new"
	strip_binary="$arch_triplet-strip"

	# Create a separate toolchain file for library compilation without including
	# our own toolchain files, letting libraries set their own C(XX)FLAGS instead.
	# The file is saved on a fixed location, since running CMake again on a library
	# we've already built before will *not* update its toolchain file path; therefore,
	# we cannot point them to our working directory, which may change across builds.
	toolchain_file_libs="$cache_dir/toolchain.$arch_deb.libs.cmake"
	grep -Ev "^include\(" "$toolchain_file" > "$toolchain_file_libs"

	# Install dependencies only if we're in a new build and/or architecture.
	if check_buildtag "$arch_deb"
	then
		# Install or update dependencies.
		echo [-] Installing dependencies through apt
		if check_buildtag aptupdate
		then
			sudo apt-get update

			# Save build tag to skip apt-get update later, unless a new architecture
			# is added to dpkg, in which case, this saved tag file gets replaced.
			save_buildtag aptupdate
		fi
		DEBIAN_FRONTEND=noninteractive sudo apt-get -y install $pkgs $libpkgs
		sudo apt-get clean

		# Save build tag to skip this later. Doing it here (once everything is
		# in place) is important to avoid potential issues with retried builds.
		save_buildtag "$arch_deb"
	else
		echo [-] Not installing dependencies again
	fi
fi

# Point CMake to the toolchain file.
[ -e "$toolchain_file" ] && cmake_flags_extra="$cmake_flags_extra -D \"CMAKE_TOOLCHAIN_FILE=$toolchain_file\""

# Clean workspace.
echo [-] Cleaning workspace
rm -rf build

# Add ARCH to skip the arch_detect process.
case $arch in
	32 | x86)	cmake_flags_extra="$cmake_flags_extra -D ARCH=i386";;
	64 | x86_64*)	cmake_flags_extra="$cmake_flags_extra -D ARCH=x86_64";;
	ARM32 | arm32)	cmake_flags_extra="$cmake_flags_extra -D ARCH=arm -D NEW_DYNAREC=ON";;
	ARM64 | arm64)	cmake_flags_extra="$cmake_flags_extra -D ARCH=arm64 -D NEW_DYNAREC=ON";;
	*)		cmake_flags_extra="$cmake_flags_extra -D \"ARCH=$arch\"";;
esac

# Add git hash.
git_hash=$(git rev-parse --short HEAD 2> /dev/null)
if [ "$CI" = "true" ]
then
	# Backup strategy when running under Jenkins.
	[ -z "$git_hash" ] && git_hash=$(echo $GIT_COMMIT | cut -c 1-8)
elif [ -n "$git_hash" ]
then
	# Append + to denote a dirty tree.
	git diff --quiet 2> /dev/null || git_hash="$git_hash+"
fi
[ -n "$git_hash" ] && cmake_flags_extra="$cmake_flags_extra -D \"EMU_GIT_HASH=$git_hash\""

# Add copyright year.
year=$(date +%Y)
[ -n "$year" ] && cmake_flags_extra="$cmake_flags_extra -D \"EMU_COPYRIGHT_YEAR=$year\""

# Run CMake.
echo [-] Running CMake with flags [$cmake_flags $cmake_flags_extra]
eval cmake -G Ninja $cmake_flags $cmake_flags_extra -S . -B build
status=$?
if [ $status -ne 0 ]
then
	echo [!] CMake failed with status [$status]
	exit 3
fi

# Run actual build, unless we're running a dry build to precondition a node.
if [ "$BUILD_TAG" != "precondition" ]
then
	echo [-] Running build
	cmake --build build -j$(nproc)
	status=$?
	if [ $status -ne 0 ]
	then
		echo [!] Build failed with status [$status]
		exit 4
	fi
else
	# Copy dummy binary into place.
	echo [-] Preconditioning build node
	mkdir -p build/src
	if is_windows
	then
		cp "$(which cp)" "build/src/$project.exe"
	elif is_mac
	then
		: # Special check during app bundle generation.
	else
		cp "$(which cp)" "build/src/$project"
	fi
fi

# Download Discord Game SDK from their CDN if we're in a new build.
discord_version="3.2.1"
discord_zip="$cache_dir/discord_game_sdk-$discord_version.zip"
if [ ! -e "$discord_zip" ]
then
	# Download file.
	echo [-] Downloading Discord Game SDK
	rm -f "$cache_dir/discord_game_sdk"* # remove old versions
	wget -qO "$discord_zip" "https://dl-game-sdk.discordapp.net/$discord_version/discord_game_sdk.zip"
	status=$?
	if [ $status -ne 0 ]
	then
		echo [!] Discord Game SDK download failed with status [$status]
		rm -f "$discord_zip"
	fi
else
	echo [-] Not downloading Discord Game SDK again
fi

# Determine Discord Game SDK architecture.
case $arch in
	32)		arch_discord="x86";;
	64 | x86_64*)	arch_discord="x86_64";;
	arm64 | ARM64)	arch_discord="aarch64";;
	*)		arch_discord="$arch";;
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

	# Archive Ghostscript DLL from local official distribution installation.
	for gs in "$pf"/gs/gs*.*.*
	do
		cp -p "$gs"/bin/gsdll*.dll archive_tmp/
	done

	# Archive Discord Game SDK DLL.
	"$sevenzip" e -y -o"archive_tmp" "$discord_zip" "lib/$arch_discord/discord_game_sdk.dll"
	[ ! -e "archive_tmp/discord_game_sdk.dll" ] && echo [!] No Discord Game SDK for architecture [$arch_discord]

	# Archive XAudio2 DLL if required.
	grep -q "OPENAL:BOOL=ON" build/CMakeCache.txt || cp -p "/home/$project/dll$arch/xaudio2"* archive_tmp/

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
	# Archive app bundle with libraries.
	cmake_flags_install=
	[ $strip -ne 0 ] && cmake_flags_install="$cmake_flags_install --strip"
	cmake --install build --prefix "$(pwd)/archive_tmp" $cmake_flags_install
	status=$?

	if [ $status -eq 0 ]
	then
		# Archive Discord Game SDK library.
		unzip -j "$discord_zip" "lib/$arch_discord/discord_game_sdk.dylib" -d "archive_tmp/"*".app/Contents/Frameworks"
		[ ! -e "archive_tmp/"*".app/Contents/Frameworks/discord_game_sdk.dylib" ] && echo [!] No Discord Game SDK for architecture [$arch_discord]

		# Hack to convert x86_64 binaries to x86_64h when building that architecture.
		if [ "$arch" = "x86_64h" ]
		then
			find archive_tmp -type f | while IFS= read line
			do
				# Parse and patch a fat header (0xCAFEBABE, big endian) first.
				macho_offset=0
				if [ "$(dd if="$line" bs=1 count=4 status=none)" = "$(printf '\xCA\xFE\xBA\xBE')" ]
				then
					# Get the number of fat architectures.
					fat_archs=$(($(dd if="$line" bs=1 skip=4 count=4 status=none | rev | tr -d '\n' | od -An -vtu4)))

					# Go through fat architectures.
					fat_offset=8
					for fat_arch in $(seq 1 $fat_archs)
					do
						# Check CPU type.
						if [ "$(dd if="$line" bs=1 skip=$fat_offset count=4 status=none)" = "$(printf '\x01\x00\x00\x07')" ]
						then
							# Change CPU subtype in the fat header from ALL (0x00000003) to H (0x00000008).
							printf '\x00\x00\x00\x08' | dd of="$line" bs=1 seek=$((fat_offset + 4)) count=4 conv=notrunc status=none

							# Save offset for this architecture's Mach-O header.
							macho_offset=$(($(dd if="$line" bs=1 skip=$((fat_offset + 8)) count=4 status=none | rev | tr -d '\n' | od -An -vtu4)))

							# Stop looking for the x86_64 slice.
							break
						fi

						# Move on to the next architecture.
						fat_offset=$((fat_offset + 20))
					done
				fi

				# Now patch a 64-bit Mach-O header (0xFEEDFACF, little endian), either at
				# the beginning or as a sub-header within a fat binary as parsed above.
				if [ "$(dd if="$line" bs=1 skip=$macho_offset count=8 status=none)" = "$(printf '\xCF\xFA\xED\xFE\x07\x00\x00\x01')" ]
				then
					# Change CPU subtype in the Mach-O header from ALL (0x00000003) to H (0x00000008).
					printf '\x08\x00\x00\x00' | dd of="$line" bs=1 seek=$((macho_offset + 8)) count=4 conv=notrunc status=none
				fi
			done
		fi

		# Sign app bundle, unless we're in an universal build.
		[ $skip_archive -eq 0 ] && codesign --force --deep -s - "archive_tmp/"*".app"
	elif [ "$BUILD_TAG" = "precondition" ]
	then
		# Continue with no app bundle on a dry build.
		status=0
	fi
else
	cwd_root="$(pwd)"
	check_buildtag "libs.$arch_deb"

	if grep -q "OPENAL:BOOL=ON" build/CMakeCache.txt
	then
		# Build openal-soft 1.23.1 manually to fix audio issues. This is a temporary
		# workaround until a newer version of openal-soft trickles down to Debian repos.
		prefix="$cache_dir/openal-soft-1.23.1"
		if [ ! -d "$prefix" ]
		then
			rm -rf "$cache_dir/openal-soft-"* # remove old versions
			wget -qO - https://github.com/kcat/openal-soft/archive/refs/tags/1.23.1.tar.gz | tar zxf - -C "$cache_dir" || rm -rf "$prefix"
		fi

		# Patches to build with the old PipeWire version in Debian.
		sed -i -e 's/>=0.3.23//' "$prefix/CMakeLists.txt"
		sed -i -e 's/PW_KEY_CONFIG_NAME/"config.name"/g' "$prefix/alc/backends/pipewire.cpp"

		prefix_build="$prefix/build-$arch_deb"
		cmake -G Ninja -D "CMAKE_TOOLCHAIN_FILE=$toolchain_file_libs" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" -S "$prefix" -B "$prefix_build" || exit 99
		cmake --build "$prefix_build" -j$(nproc) || exit 99
		cmake --install "$prefix_build" || exit 99

		# Build SDL2 without sound systems.
		sdl_ss=OFF
	else
		# Build FAudio 22.03 manually to remove the dependency on GStreamer. This is a temporary
		# workaround until a newer version of FAudio trickles down to Debian repos.
		prefix="$cache_dir/FAudio-22.03"
		if [ ! -d "$prefix" ]
		then
			rm -rf "$cache_dir/FAudio-"* # remove old versions
			wget -qO - https://github.com/FNA-XNA/FAudio/archive/refs/tags/22.03.tar.gz | tar zxf - -C "$cache_dir" || rm -rf "$prefix"
		fi
		prefix_build="$prefix/build-$arch_deb"
		cmake -G Ninja -D "CMAKE_TOOLCHAIN_FILE=$toolchain_file_libs" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" -S "$prefix" -B "$prefix_build" || exit 99
		cmake --build "$prefix_build" -j$(nproc) || exit 99
		cmake --install "$prefix_build" || exit 99

		# Build SDL2 with sound systems.
		sdl_ss=ON
	fi

	# Build SDL2 with video systems (and dependencies) only if the SDL interface is used.
	sdl_ui=OFF
	grep -qiE "^QT:BOOL=ON" build/CMakeCache.txt || sdl_ui=ON

	# Build rtmidi without JACK support to remove the dependency on libjack, as
	# the Debian libjack is very likely to be incompatible with the system jackd.
	prefix="$cache_dir/rtmidi-4.0.0"
	if [ ! -d "$prefix" ]
	then
		rm -rf "$cache_dir/rtmidi-"* # remove old versions
		wget -qO - https://github.com/thestk/rtmidi/archive/refs/tags/4.0.0.tar.gz | tar zxf - -C "$cache_dir" || rm -rf "$prefix"
	fi
	prefix_build="$prefix/build-$arch_deb"
	cmake -G Ninja -D RTMIDI_API_JACK=OFF -D "CMAKE_TOOLCHAIN_FILE=$toolchain_file_libs" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" -S "$prefix" -B "$prefix_build" || exit 99
	cmake --build "$prefix_build" -j$(nproc) || exit 99
	cmake --install "$prefix_build" || exit 99

	# Build FluidSynth without sound systems to remove the dependencies on libjack
	# and other sound system libraries. We don't output audio through FluidSynth.
	prefix="$cache_dir/fluidsynth-2.3.0"
	if [ ! -d "$prefix" ]
	then
		rm -rf "$cache_dir/fluidsynth-"* # remove old versions
		wget -qO - https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.3.0.tar.gz | tar zxf - -C "$cache_dir" || rm -rf "$prefix"
	fi
	prefix_build="$prefix/build-$arch_deb"
	cmake -G Ninja -D enable-dbus=OFF -D enable-jack=OFF -D enable-oss=OFF -D enable-sdl2=OFF -D enable-pulseaudio=OFF -D enable-pipewire=OFF -D enable-alsa=OFF \
		-D "CMAKE_TOOLCHAIN_FILE=$toolchain_file_libs" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" \
		-S "$prefix" -B "$prefix_build" || exit 99
	cmake --build "$prefix_build" -j$(nproc) || exit 99
	cmake --install "$prefix_build" || exit 99

	# Build SDL2 for joystick and FAudio support, with most components
	# disabled to remove the dependencies on PulseAudio and libdrm.
	prefix="$cache_dir/SDL2-2.0.20"
	if [ ! -d "$prefix" ]
	then
		rm -rf "$cache_dir/SDL2-"* # remove old versions
		wget -qO - https://www.libsdl.org/release/SDL2-2.0.20.tar.gz | tar zxf - -C "$cache_dir" || rm -rf "$prefix"
	fi
	prefix_build="$cache_dir/SDL2-2.0.20-build-$arch_deb"
	cmake -G Ninja -D SDL_SHARED=ON -D SDL_STATIC=OFF \
		\
		-D SDL_AUDIO=$sdl_ss -D SDL_DUMMYAUDIO=$sdl_ss -D SDL_DISKAUDIO=OFF -D SDL_OSS=OFF -D SDL_ALSA=$sdl_ss -D SDL_ALSA_SHARED=$sdl_ss \
		-D SDL_JACK=$sdl_ss -D SDL_JACK_SHARED=$sdl_ss -D SDL_ESD=OFF -D SDL_ESD_SHARED=OFF -D SDL_PIPEWIRE=$sdl_ss \
		-D SDL_PIPEWIRE_SHARED=$sdl_ss -D SDL_PULSEAUDIO=$sdl_ss -D SDL_PULSEAUDIO_SHARED=$sdl_ss -D SDL_ARTS=OFF -D SDL_ARTS_SHARED=OFF \
		-D SDL_NAS=$sdl_ss -D SDL_NAS_SHARED=$sdl_ss -D SDL_SNDIO=$sdl_ss -D SDL_SNDIO_SHARED=$sdl_ss -D SDL_FUSIONSOUND=OFF \
		-D SDL_FUSIONSOUND_SHARED=OFF -D SDL_LIBSAMPLERATE=$sdl_ss -D SDL_LIBSAMPLERATE_SHARED=$sdl_ss \
		\
		-D SDL_VIDEO=$sdl_ui -D SDL_X11=$sdl_ui -D SDL_X11_SHARED=$sdl_ui -D SDL_WAYLAND=$sdl_ui -D SDL_WAYLAND_SHARED=$sdl_ui \
		-D SDL_WAYLAND_LIBDECOR=$sdl_ui -D SDL_WAYLAND_LIBDECOR_SHARED=$sdl_ui -D SDL_WAYLAND_QT_TOUCH=OFF -D SDL_RPI=OFF -D SDL_VIVANTE=OFF \
		-D SDL_VULKAN=OFF -D SDL_KMSDRM=$sdl_ui -D SDL_KMSDRM_SHARED=$sdl_ui -D SDL_OFFSCREEN=$sdl_ui -D SDL_RENDER=$sdl_ui \
		\
		-D SDL_JOYSTICK=ON -D SDL_HIDAPI_JOYSTICK=ON -D SDL_VIRTUAL_JOYSTICK=ON \
		\
		-D SDL_ATOMIC=OFF -D SDL_EVENTS=ON -D SDL_HAPTIC=OFF -D SDL_POWER=OFF -D SDL_THREADS=ON -D SDL_TIMERS=ON -D SDL_FILE=OFF \
		-D SDL_LOADSO=ON -D SDL_CPUINFO=ON -D SDL_FILESYSTEM=$sdl_ui -D SDL_DLOPEN=OFF -D SDL_SENSOR=OFF -D SDL_LOCALE=OFF \
		\
		-D "CMAKE_TOOLCHAIN_FILE=$toolchain_file_libs" -D "CMAKE_INSTALL_PREFIX=$cwd_root/archive_tmp/usr" \
		-S "$prefix" -B "$prefix_build" || exit 99
	cmake --build "$prefix_build" -j$(nproc) || exit 99
	cmake --install "$prefix_build" || exit 99

	# We rely on the host to provide Vulkan libs to sidestep any potential
	# dependency issues. While Qt expects libvulkan.so, at least Debian only
	# ships libvulkan.so.1 without a symlink, so make our own as a workaround.
	# The relative paths prevent appimage-builder from flattening the links.
	mkdir -p "archive_tmp/usr/lib/$libdir"
	relroot="../../../../../../../../../../../../../../../../../../../../../../../../../../../../.."
	ln -s "$relroot/usr/lib/libvulkan.so.1" "archive_tmp/usr/lib/libvulkan.so"
	ln -s "$relroot/usr/lib/$libdir/libvulkan.so.1" "archive_tmp/usr/lib/$libdir/libvulkan.so"

	# The FluidSynth packaged by Debian bullseye is ABI incompatible with
	# the newer version we compile, despite sharing a major version. Since we
	# don't run into the one breaking ABI change they made, just symlink it.
	ln -s "$(readlink "archive_tmp/usr/lib/libfluidsynth.so.3")" "archive_tmp/usr/lib/libfluidsynth.so.2"

	# Archive Discord Game SDK library.
	7z e -y -o"archive_tmp/usr/lib" "$discord_zip" "lib/$arch_discord/discord_game_sdk.so"
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

	# Archive metadata.
	project_id=$(ls src/unix/assets/*.*.xml | head -1 | grep -oP '/\K([^/]+)(?=\.[^\.]+\.[^\.]+$)')
	metainfo_base=archive_tmp/usr/share/metainfo
	mkdir -p "$metainfo_base"
	cp -p "src/unix/assets/$project_id."*".xml" "$metainfo_base/$project_id.appdata.xml"
	applications_base=archive_tmp/usr/share/applications
	mkdir -p "$applications_base"
	cp -p "src/unix/assets/$project_id.desktop" "$applications_base/"

	# Archive icons.
	icon_base=archive_tmp/usr/share/icons/hicolor
	for icon_size in src/unix/assets/[0-9]*x[0-9]*
	do
		icon_dir="$icon_base/$(basename "$icon_size")"
		mkdir -p "$icon_dir"
		cp -rp "$icon_size" "$icon_dir/apps"
	done
	project_icon=$(find "$icon_base/"[0-9]*x[0-9]*/* -type f -name '*.png' -o -name '*.svg' | head -1 | grep -oP '/\K([^/]+)(?=\.[^\.]+$)')

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

# Stop if artifact archive creation was disabled.
if [ $skip_archive -ne 0 ]
then
	echo [-] Skipping artifact archive creation
	exit 0
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
	# Create zip.
	cd archive_tmp
	zip --symlinks -r "$cwd/$package_name.zip" .
	status=$?
else
	# Determine AppImage runtime architecture.
	case $arch in
		x86)	arch_appimage="i686";;
		arm32)	arch_appimage="armhf";;
		arm64)	arch_appimage="aarch64";;
		*)	arch_appimage="$arch";;
	esac

	# Get version for AppImage metadata.
	project_version=$(grep -oP '#define\s+EMU_VERSION\s+"\K([^"]+)' "build/src/include/"*"/version.h" 2> /dev/null)
	[ -z "$project_version" ] && project_version=unknown
	build_num=$(grep -oP '#define\s+EMU_BUILD_NUM\s+\K([0-9]+)' "build/src/include/"*"/version.h" 2> /dev/null)
	[ -n "$build_num" -a "$build_num" != "0" ] && project_version="$project_version-b$build_num"

	# Generate modified AppImage metadata to suit build requirements.
	cat << EOF > AppImageBuilder-generated.yml
# This file is automatically generated by .ci/build.sh and will be
# overwritten if edited. Please edit .ci/AppImageBuilder.yml instead.
EOF
	while IFS= read line
	do
		# Skip blank or comment lines.
		echo "$line" | grep -qE '^(#|$)' && continue

		# Parse "# if OPTION:TYPE=VALUE" CMake condition lines.
		condition=$(echo "$line" | grep -oP '# if \K(.+)')
		if [ -n "$condition" ]
		then
			# Skip line if the condition is not matched.
			grep -qiE "^$condition" build/CMakeCache.txt || continue
		fi

		# Copy line.
		echo "$line" >> AppImageBuilder-generated.yml

		# Workaround for appimage-builder issues 272 and 283 (i686 and armhf are also missing)
		if [ "$arch_appimage" != "x86_64" -a "$line" = "  files:" ]
		then
			# Some mild arbitrary code execution with a dummy package...
			[ ! -d /runtime ] && sudo apt-get -y -o 'DPkg::Post-Invoke::=mkdir -p /runtime; chmod 777 /runtime' install libsixel1 > /dev/null 2>&1

			echo "    include:" >> AppImageBuilder-generated.yml
			for loader in "/lib/$libdir/ld-linux"*.so.*
			do
				for loader_copy in "$loader" "/lib/$(basename "$loader")"
				do
					if [ ! -e "/runtime/compat$loader_copy" ]
					then
						mkdir -p "/runtime/compat$(dirname "$loader_copy")"
						ln -s "$loader" "/runtime/compat$loader_copy"
					fi
					echo "    - /runtime/compat$loader_copy" >> AppImageBuilder-generated.yml
				done
			done
		fi
	done < .ci/AppImageBuilder.yml

	# Download appimage-builder if necessary.
	appimage_builder_url="https://github.com/AppImageCrafters/appimage-builder/releases/download/v1.1.0/appimage-builder-1.1.0-$(uname -m).AppImage"
	appimage_builder_binary="$cache_dir/$(basename "$appimage_builder_url")"
	if [ ! -e "$appimage_builder_binary" ]
	then
		rm -rf "$cache_dir/"*".AppImage" # remove old versions
		wget -qO "$appimage_builder_binary" "$appimage_builder_url"
	fi

	# Symlink appimage-builder binary and global cache directory.
	rm -rf appimage-builder.AppImage appimage-builder-cache "$project-"*".AppImage" # also remove any dangling AppImages which may interfere with the renaming process
	ln -s "$appimage_builder_binary" appimage-builder.AppImage
	chmod u+x appimage-builder.AppImage
	mkdir -p "$cache_dir/appimage-builder-cache"
	ln -s "$cache_dir/appimage-builder-cache" appimage-builder-cache

	# Run appimage-builder in extract-and-run mode for Docker compatibility.
	# --appdir is a workaround for appimage-builder issue 270 reported by us.
	for retry in 1 2 3 4 5
	do
		project="$project" project_id="$project_id" project_version="$project_version" project_icon="$project_icon" arch_deb="$arch_deb" \
			arch_appimage="$arch_appimage" appimage_path="$cwd/$package_name.AppImage" APPIMAGE_EXTRACT_AND_RUN=1 ./appimage-builder.AppImage \
			--recipe AppImageBuilder-generated.yml --appdir "$(grep -oP '^\s+path: \K(.+)' AppImageBuilder-generated.yml)"
		status=$?
		[ $status -eq 0 ] && break
		[ $status -eq 127 ] && rm -rf /tmp/appimage_extracted_*
	done

	# Remove appimage-builder binary on failure, just in case it's corrupted.
	[ $status -ne 0 ] && rm -f "$appimage_builder_binary"
fi

# Check if the archival succeeded.
if [ $status -ne 0 ]
then
	echo [!] Artifact archive creation failed with status [$status]
	exit 7
fi

# All good.
echo [-] Build of [$package_name] for [$arch] with flags [$cmake_flags] successful
exit 0

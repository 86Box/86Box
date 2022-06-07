#!/bin/sh
#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		Convenience script for changing the emulator's version.
#
#
# Authors:	RichardG, <richardg867@gmail.com>
#
#		Copyright 2022 RichardG.
#

# Parse arguments.
newversion="$1"
if [ -z "$(echo $newversion | grep '\.')" ]
then
	echo '[!] Usage: bumpversion.sh x.y[.z]'
	exit 1
fi
shift

# Extract version components.
newversion_maj=$(echo $newversion | cut -d. -f1)
newversion_min=$(echo $newversion | cut -d. -f2)
newversion_patch=$(echo $newversion | cut -d. -f3)
[ -z "$newversion_patch" ] && newversion_patch=0

base36() {
	if [ $1 -lt 10 ]
	then
		echo $1
	else
		printf '%b' $(printf '\\%03o' $((55 + $1)))
	fi
}
newversion_maj_base36=$(base36 $newversion_maj)
newversion_min_base36=$(base36 $newversion_min)
newversion_patch_base36=$(base36 $newversion_patch)

# Switch to the repository root directory.
cd "$(dirname "$0")"

get_latest_rom_release() {
    # get the latest ROM release from GitHub api
    curl --silent "https://api.github.com/repos/86Box/roms/releases/latest" |
        grep '"tag_name":' |
        sed -E 's/.*"([^"]+)".*/\1/'
}

pretty_date() {
    # Ensure we get the date in English
    LANG=en_US.UTF-8 date '+%a %b %d %Y'
}

# Patch files.
patch_file() {
	# Stop if the file doesn't exist.
	[ ! -e "$1" ] && return

	# Patch file.
	if sed -i -r -e "$3" "$1"
	then
		echo "[-] Patched $2 on $1"
	else
		echo "[!] Patching $2 on $1 failed"
	fi
}
patch_file CMakeLists.txt VERSION 's/^(\s*VERSION ).+/\1'"$newversion"'/'
patch_file CMakeLists.txt EMU_VERSION_EX 's/(\s*set\(EMU_VERSION_EX\s+")[^"]+/\1'"$newversion_maj_base36.$newversion_min_base36$newversion_patch_base36"'/'
patch_file vcpkg.json version-string 's/(^\s*"version-string"\s*:\s*")[^"]+/\1'"$newversion"'/'
patch_file src/include_make/*/version.h EMU_VERSION 's/(#\s*define\s+EMU_VERSION\s+")[^"]+/\1'"$newversion"'/'
patch_file src/include_make/*/version.h EMU_VERSION_EX 's/(#\s*define\s+EMU_VERSION_EX\s+")[^"]+/\1'"$newversion_maj_base36.$newversion_min_base36$newversion_patch_base36"'/'
patch_file src/include_make/*/version.h EMU_VERSION_MAJ 's/(#\s*define\s+EMU_VERSION_MAJ\s+)[0-9]+/\1'"$newversion_maj"'/'
patch_file src/include_make/*/version.h EMU_VERSION_MIN 's/(#\s*define\s+EMU_VERSION_MIN\s+)[0-9]+/\1'"$newversion_min"'/'
patch_file src/include_make/*/version.h EMU_VERSION_PATCH 's/(#\s*define\s+EMU_VERSION_PATCH\s+)[0-9]+/\1'"$newversion_patch"'/'
patch_file src/include_make/*/version.h COPYRIGHT_YEAR 's/(#\s*define\s+COPYRIGHT_YEAR\s+)[0-9]+/\1'"$(date +%Y)"'/'
patch_file src/include_make/*/version.h EMU_DOCS_URL 's/(#\s*define\s+EMU_DOCS_URL\s+"https:\/\/[^\/]+\/en\/v)[^\/]+/\1'"$newversion_maj.$newversion_min"'/'
patch_file src/unix/assets/*.spec Version 's/(Version:\s+)[0-9].+/\1'"$newversion"'/'
patch_file src/unix/assets/*.spec '%global romver' 's/(^%global\ romver\s+)[0-9]{8}/'"$(get_latest_rom_release)"'/'
patch_file src/unix/assets/*.spec 'changelog version' 's/(^[*]\s.*>\s+)[0-9].+/\1'"$newversion"-1'/'
patch_file src/unix/assets/*.spec 'changelog date' 's/(^[*]\s)[a-zA-Z]{3}\s[a-zA-Z]{3}\s[0-9]{2}\s[0-9]{4}/\1'"$(pretty_date)"'/'
patch_file src/unix/assets/*.metainfo.xml release 's/(<release version=")[^"]+(" date=")[^"]+/\1'"$newversion"'\2'"$(date +%Y-%m-%d)"'/'

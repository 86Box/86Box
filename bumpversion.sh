#!/bin/sh
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          Convenience script for changing the emulator's version.
#
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2022 RichardG.
#

# Parse arguments.
newversion="$1"
romversion="$2"

if [ -z "$(echo "$newversion" | grep '\.')" ]
then
	echo '[!] Usage: bumpversion.sh x.y[.z] [romversion]'
	exit 1
fi
shift

if [ -z "${romversion}" ]; then
	# Get the latest ROM release from the GitHub API.
	romversion=$(curl --silent "https://api.github.com/repos/86Box/roms/releases/latest" |
		grep '"tag_name":' |
		sed -E 's/.*"v([^"]+)".*/\1/')
fi

# Switch to the repository root directory.
cd "$(dirname "$0")" || exit

pretty_date() {
	# Ensure we get the date in English.
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
patch_file vcpkg.json version-string 's/(^\s*"version-string"\s*:\s*")[^"]+/\1'"$newversion"'/'
patch_file src/unix/assets/*.spec Version 's/(Version:\s+)[0-9].+/\1'"$newversion"'/'
patch_file src/unix/assets/*.spec '%global romver' 's/(^%global\ romver\s+)[0-9]{8}/\1'"$romversion"'/'
patch_file src/unix/assets/*.spec 'changelog version' 's/(^[*]\s.*>\s+)[0-9].+/\1'"$newversion"-1'/'
patch_file src/unix/assets/*.spec 'changelog date' 's/(^[*]\s)[a-zA-Z]{3}\s[a-zA-Z]{3}\s[0-9]{2}\s[0-9]{4}/\1'"$(pretty_date)"'/'
patch_file src/unix/assets/*.metainfo.xml release 's/(<release version=")[^"]+(" date=")[^"]+/\1'"$newversion"'\2'"$(date +%Y-%m-%d)"'/'
patch_file debian/changelog 'changelog date' 's/>  .+/>  '"$(date -R)"'/'
patch_file debian/changelog 'changelog version' 's/86box \(.+\)/86box \('"$newversion"'\)/'

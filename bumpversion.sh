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
	# Parse arguments.
	desc="$1"
	shift
	pattern="$1"
	shift

	# Patch the specified files.
	for file in "$@"
	do
		# Skip file if it doesn't exist.
		[ ! -e "$file" ] && continue

		# Patch file.
		if sed -i -r -e "$pattern" "$file"
		then
			echo "[-] Patched $desc in $file"
		else
			echo "[!] Patching $desc in $file failed"
		fi
	done
}
patch_file VERSION 's/^(\s*VERSION ).+/\1'"$newversion"'/' CMakeLists.txt
patch_file version-string 's/(^\s*"version-string"\s*:\s*")[^"]+/\1'"$newversion"'/' vcpkg.json
patch_file Version 's/(Version:\s+)[0-9].+/\1'"$newversion"'/' src/unix/assets/*.spec
patch_file '%global romver' 's/(^%global\ romver\s+)[^\s]+/\1'"$romversion"'/' src/unix/assets/*.spec
patch_file 'changelog version' 's/(^[*]\s.*>\s+)[0-9].+/\1'"$newversion"-1'/' src/unix/assets/*.spec
patch_file 'changelog date' 's/(^[*]\s)[a-zA-Z]{3}\s[a-zA-Z]{3}\s[0-9]{2}\s[0-9]{4}/\1'"$(pretty_date)"'/' src/unix/assets/*.spec
patch_file release 's/(<release version=")[^"]+(" date=")[^"]+/\1'"$newversion"'\2'"$(date +%Y-%m-%d)"'/' src/unix/assets/*.metainfo.xml
patch_file 'changelog date' 's/>  .+/>  '"$(date -R)"'/' debian/changelog
patch_file 'changelog version' 's/86box \(.+\)/86box \('"$newversion"'\)/' debian/changelog

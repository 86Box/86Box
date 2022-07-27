#!/bin/sh
#
# 86Box		A hypervisor and IBM PC system emulator that specializes in
#		running old operating systems and software designed for IBM
#		PC systems and compatibles from 1981 through fairly recent
#		system designs based on the PCI bus.
#
#		This file is part of the 86Box distribution.
#
#		Script for converting MinGW static libraries into a DLL.
#
#
# Authors:	RichardG, <richardg867@gmail.com>
#
#		Copyright 2021 RichardG.
#

def_file="static2dll.def"
seen_file="static2dll.seen"
libs_file="static2dll.libs"

find_lib() {
	# Try to find a static library's file.
	local msystem_lib="/$(echo $MSYSTEM | tr '[:upper:]' '[:lower:]')/lib/lib"
	if [ -e "$msystem_lib$1.a" ]
	then
		echo "$msystem_lib$1.a"
	elif [ -e "$msystem_lib$1.dll.a" ]
	then
		echo "$msystem_lib$1.dll.a"
	else
		# Return dynamic reference to the library.
		echo "-l$1"
		return 1
	fi
}

add_lib() {
	# Always make sure this lib is listed after the last lib that depends on it.
	old_libs=$(cat "$libs_file")
	rm -f "$libs_file"
	for lib in $old_libs
	do
		[ "$lib" != "$*" ] && echo "$lib" >> "$libs_file"
	done
	echo "$*" >> "$libs_file"

	# Add libstdc++ in the end if required.
	if echo "$*" | grep -q "/"
	then
		grep -Eq -- "__cxa_|__gxx_" "$1" 2> /dev/null && add_lib -static -lstdc++
	fi

	# Add libiconv for libintl.
	if echo "$*" | grep -q "libintl"
	then
		add_lib $(find_lib iconv)
	fi

	# Add libuuid for glib.
	if echo "$*" | grep -q "libglib"
	then
		add_lib $(find_lib uuid)
	fi
}

run_pkgconfig() {
	local cache_file="static2dll.$1.cache"
	if [ -e "$cache_file" ]
	then
		cat "$cache_file"
	else
		pkg-config --static --libs "$1" 2> /dev/null | tee "$cache_file"
	fi
}

parse_pkgconfig() {
	# Parse arguments.
	local layers=$1
	shift
	local input_lib_name=$1
	shift

	# Don't process the same file again.
	grep -q '^'$input_lib_name'$' "$seen_file" && return
	echo $input_lib_name >> "$seen_file"

	echo "$layers" parse_pkgconfig $input_lib_name

	# Parse pkg-config arguments.
	for arg in $*
	do
		local arg_base="$(echo $arg | cut -c1-2)"
		if [ "x$arg_base" = "x-l" ]
		then
			# Don't process the same lib again.
			local lib_name="$(echo $arg | cut -c3-)"
			[ "x$lib_name" == "x$input_lib_name" ] && continue

			# Add lib path.
			add_lib "$(find_lib $lib_name)"

			# Get this lib's dependencies through pkg-config.
			local pkgconfig="$(run_pkgconfig "$lib_name")"
			[ $? -eq 0 ] && parse_pkgconfig "$layers"'>' "$lib_name" $pkgconfig || echo $lib_name >> "$seen_file"
		elif [ "x$(echo $arg_base | cut -c1)" = "x-" ]
		then
			# Ignore other arguments.
			continue
		else
			# Add lib path.
			add_lib "$arg"
		fi
	done
}

# Parse arguments.
case $1 in
	-p) # -p pkg_config_name static_lib_path out_dll
		shift
		base_pkgconfig=$(run_pkgconfig "$1")
		base_path="$2"
		base_name="$1"
		;;

	*) # pc_path static_lib_path out_dll
		base_pkgconfig="$(grep ^Libs.private: $1 | cut -d: -f2-)"
		base_path="$2"
		base_name="$2"
		;;
esac

# Check arguments.
if [ -z "$base_pkgconfig" -o -z "$base_path" -o -z "$base_name" ]
then
	echo Usage:
	echo static2dll.sh -p {pkgconfig_package_name} {static_lib_path} {out_dll_name}
	echo static2dll.sh {pc_file_path} {static_lib_path} {out_dll_name}
	exit 1
fi

# Produce .def file.
echo LIBRARY $(basename "$3") > "$def_file"
echo EXPORTS >> "$def_file"
nm "$base_path" | grep " [TC] " | sed "/ _/s// /" | awk '{ print $3 }' >> "$def_file"

# Parse dependencies recursively.
rm -f "$seen_file" "$libs_file" "$libs_file.tmp"
touch "$seen_file" "$libs_file"
parse_pkgconfig '>' $base_name $base_pkgconfig

# Produce final DLL.
dllwrap --def "$def_file" -o "$3" -Wl,--allow-multiple-definition "$base_path" $(cat "$libs_file")
status=$?
[ $status -eq 0 ] && rm -f "$def_file" "$seen_file" "$libs_file" "static2dll.*.cache"

# Update final DLL timestamp.
touch -r "$base_path" "$3"

exit $status

#!/bin/sh
#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          Script for preparing PCI devices for VFIO passthrough as root.
#
#          Usage: vfio-setup <pid> <memlock bytes> <dddd:bb:dd.f>...
#
#          Binds the specified devices and any others in the same group
#          to vfio-pci, and also raises the locked memory limit for the
#          given PID to ensure DMA mapping works.
#
# Authors: RichardG, <richardg867@gmail.com>
#
#          Copyright 2026 RichardG.
#

set -u
PATH=/usr/sbin:/usr/bin:/sbin:/bin

uid=${PKEXEC_UID:-${SUDO_UID:-}}
fail=0

[ $# -ge 2 ] || { echo "Usage: $0 <pid> <memlock bytes> <PCI location>..." >&2; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo "This script must run as root." >&2; exit 1; }

pid=$1
memlock=$2
shift 2
case "$pid$memlock" in
    "" | *[!0-9]*) echo "Invalid arguments." >&2; exit 2 ;;
esac

drv() {
    if [ -e "$1/driver" ]; then basename "$(readlink -f "$1/driver")"; else echo none; fi
}

# Drivers which never claim DMA ownership, so they do not stop a group from being viable.
safe_drv() {
    case "$1" in
        none | vfio-pci | pci-stub | pcieport) return 0 ;;
    esac
    return 1
}

# Whether a device is one of the requested locations.
requested() {
    case "$locs" in
        *" $1 "*) return 0 ;;
    esac
    return 1
}

[ -d /sys/bus/pci/drivers/vfio-pci ] || modprobe vfio-pci || fail=1

locs=" $* "
groups=
for loc in "$@"; do
    # Only accept canonical locations to prevent injection.
    if ! echo "$loc" | grep -Eqx '[0-9a-f]{4}:[0-9a-f]{2}:[01][0-9a-f]\.[0-7]'; then
        echo "$loc: Invalid PCI device location" >&2
        fail=1
        continue
    fi
    dev=/sys/bus/pci/devices/$loc
    if [ ! -e "$dev/iommu_group" ]; then
        echo "$loc: No IOMMU group" >&2
        fail=1
        continue
    fi
    group=$(basename "$(readlink -f "$dev/iommu_group")")
    case " $groups " in
        *" $group "*) continue ;;
    esac
    groups="$groups $group"

    # Leave the group alone if the host would lose a host or ISA bridge.
    refuse=
    for member in "$dev"/iommu_group/devices/*; do
        m=$(basename "$member")
        safe_drv "$(drv "$member")" && continue
        case "$(cat "$member/class")" in
            0x0600* | 0x0601*) refuse="$refuse $m" ;;
        esac
    done
    if [ -n "$refuse" ]; then
        echo "IOMMU group $group:$refuse would be taken away from the host, leaving the group alone" >&2
        fail=1
        continue
    fi

    for member in "$dev"/iommu_group/devices/*; do
        m=$(basename "$member")
        d=$(drv "$member")
        # Leave alone what keeps the group viable, except requested devices with no driver.
        if safe_drv "$d" && { [ "$d" != none ] || ! requested "$m"; }; then
            continue
        fi
        case "$(cat "$member/class")" in
            0x0604*) continue ;; # PCI bridge
        esac

        echo vfio-pci > "$member/driver_override"
        [ "$d" = none ] || echo "$m" > "$member/driver/unbind"
        echo "$m" > /sys/bus/pci/drivers_probe
        if [ "$(drv "$member")" != vfio-pci ]; then
            echo "$m: Could not bind to vfio-pci" >&2
            fail=1
        fi
    done

    # The group node appears asynchronously, and udev would undo an earlier chown.
    if [ -n "$uid" ]; then
        command -v udevadm > /dev/null && udevadm settle --timeout=5
        i=0
        while [ ! -e "/dev/vfio/$group" ] && [ $i -lt 50 ]; do
            sleep 0.1 2> /dev/null || sleep 1
            i=$((i + 1))
        done
        chown "$uid" "/dev/vfio/$group" || fail=1
    fi
done

# Let the process lock guest memory for DMA, only ever raising the limit,
# unless it is gone, such as when this is run by hand after 86Box was closed.
if [ -d "/proc/$pid" ]; then
    cur=$(prlimit --pid "$pid" --memlock --output SOFT --noheadings --raw 2> /dev/null) || cur=0
    if [ "$cur" != unlimited ] && [ "$cur" -lt "$memlock" ] 2> /dev/null; then
        prlimit --pid "$pid" --memlock="$memlock:$memlock" || fail=1
    fi
fi

# Tell whoever ran this by hand what to do next. 86Box, which needs no telling,
# is the parent when it runs this, as pkexec replaces itself with the script.
if [ $fail -eq 0 ] && [ "$PPID" != "$pid" ]; then
    if [ -d "/proc/$pid" ]; then
        echo "Setup complete, you can now hard reset the machine."
    else
        echo "Setup complete, you can now restart 86Box."
    fi
fi

exit $fail

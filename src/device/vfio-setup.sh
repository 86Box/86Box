#!/bin/sh
# Prepare PCI devices for VFIO passthrough. 86Box runs this as root through
# pkexec, either installed (with its polkit policy) or embedded in the binary,
# when the devices it was configured to pass through are not ready.
#
# Usage: vfio-setup <pid> <memlock bytes> <dddd:bb:dd.f>...
#
# Binds each location, and every device sharing its IOMMU group which has a
# driver, to vfio-pci, except PCI bridges and drivers which leave DMA alone.
# Gives the group nodes to the user who authenticated, and raises the locked
# memory limit of process <pid> so it can map guest memory for DMA. A group is
# left alone if a host or ISA bridge in it would have to be taken away from
# the host.
#
# Exits with 0 on success, 1 if anything failed and 2 on bad arguments.
#
# 86Box feeds this to "sh -s" through standard input, which some shells read
# as they go, so nothing in here may read standard input.

set -u
PATH=/usr/sbin:/usr/bin:/sbin:/bin

uid=${PKEXEC_UID:-${SUDO_UID:-}}
fail=0

[ $# -ge 2 ] || { echo "usage: $0 <pid> <memlock bytes> <location>..." >&2; exit 2; }
# 86Box always runs this as root, but someone running it by hand might not.
[ "$(id -u)" -eq 0 ] || { echo "must be run as root" >&2; exit 1; }

pid=$1
memlock=$2
shift 2
case "$pid$memlock" in
    "" | *[!0-9]*) echo "bad arguments" >&2; exit 2 ;;
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
    # Only accept canonical locations, as this runs as root.
    if ! echo "$loc" | grep -Eqx '[0-9a-f]{4}:[0-9a-f]{2}:[01][0-9a-f]\.[0-7]'; then
        echo "$loc: invalid device location" >&2
        fail=1
        continue
    fi
    dev=/sys/bus/pci/devices/$loc
    if [ ! -e "$dev/iommu_group" ]; then
        echo "$loc: no IOMMU group" >&2
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
            echo "$m: could not be bound to vfio-pci" >&2
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

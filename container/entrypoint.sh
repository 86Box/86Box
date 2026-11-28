#!/bin/sh
#
# 86Box container entry point: runs the machine in /vm headless, with the
# automation socket on $EMU_AUTOMATION. Arguments are passed on to 86Box.
# See container/README.md.
#
#   EMU_VM               the VM directory (default /vm), holding 86box.cfg
#   EMU_AUTOMATION       automation endpoint (default unix:/run/86box/86box.sock);
#                        tcp:0.0.0.0:7086 to publish it with -p; "none" for none
#   EMU_TOKEN_FILE       TCP: where the token is written
#                        (default /run/86box/automation.token)
#   EMU_HOLD             1: start held, no emulated time until a client asks
#   EMU_SEED             repeatable random numbers (MACs, floppy weak bits)
#
set -eu

# 86Box keeps its global settings in $HOME/.config/86Box.
if ! [ -w "${HOME:-/nonexistent}" ]; then
    export HOME=/tmp/86box-home
    mkdir -p "$HOME"
fi

vm="${EMU_VM:-/vm}"
endpoint="${EMU_AUTOMATION:-unix:/run/86box/86box.sock}"
token_file="${EMU_TOKEN_FILE:-/run/86box/automation.token}"

if [ ! -f "$vm/86box.cfg" ]; then
    cat >&2 <<EOF
86box-entrypoint: no machine in $vm (86box.cfg is missing).
Mount a VM directory made with 86Box on a desktop, for example:
    docker run -v "\$PWD/myvm:/vm" ...
or write a 86box.cfg there; see container/README.md for a minimal one.
EOF
    exit 2
fi

set -- --headless --vmpath "$vm" "$@"

# ROMs: a set in /roms (mounted, or built in with WITH_ROMS=1) is searched
# first; the usual places (the VM's roms/, ~/.local/share/86Box/roms) follow.
if [ -n "$(ls -A /roms 2>/dev/null)" ]; then
    set -- --rompath /roms "$@"
fi

case "$endpoint" in
    none | "")
        ;;
    unix:*)
        # A socket left behind by a killed container would refuse the bind.
        rm -f "${endpoint#unix:}"
        set -- --automation "$endpoint" "$@"
        ;;
    tcp:*)
        set -- --automation "$endpoint" --automation-token-file "$token_file" "$@"
        # A port published with -p arrives on the container's interface, not on
        # its loopback, so the listener must take any address. Clients still
        # need the token, and the protocol is unencrypted: publish the port on
        # the host's loopback only (-p 127.0.0.1:7086:7086).
        hostport="${endpoint#tcp:}"
        case "$hostport" in
            *:*) host="${hostport%:*}" ;;
            *) host="127.0.0.1" ;;
        esac
        case "$host" in
            127.* | localhost | "[::1]" | ::1) ;;
            *) set -- --automation-allow-remote "$@" ;;
        esac
        ;;
    *)
        echo "86box-entrypoint: EMU_AUTOMATION must be unix:<path>, tcp:[host:]port or none, not '$endpoint'." >&2
        exit 2
        ;;
esac

if [ "${EMU_HOLD:-0}" = "1" ]; then
    set -- --automation-hold "$@"
fi
if [ -n "${EMU_SEED:-}" ]; then
    set -- --automation-seed "$EMU_SEED" "$@"
fi

exec 86Box "$@"

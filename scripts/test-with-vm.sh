#!/bin/bash
# Launch 86Box with the Windows 98 Low End test VM (configured with Voodoo card)
set -e

VM_PATH='/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy'

if [ ! -d "$VM_PATH" ]; then
    echo "Error: VM not found at $VM_PATH"
    exit 1
fi

if [ ! -f "build/src/86Box.app/Contents/MacOS/86Box" ]; then
    echo "Error: 86Box not built. Run ./scripts/clean-build-and-sign.sh first"
    exit 1
fi

echo "Launching 86Box with VM: Windows 98 Low End copy"
build/src/86Box.app/Contents/MacOS/86Box --vmpath "$VM_PATH"

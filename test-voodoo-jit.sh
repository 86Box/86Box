#!/bin/bash
# Test script for Voodoo ARM64 JIT validation
# Runs 86Box with terminal logging to capture JIT validation output

set -e

# Configuration
BUILD_DIR="build/src"
LOGFILE="voodoo-jit-test.log"
DEFAULT_VM='/Users/anthony/Library/Application Support/86Box/Virtual Machines/Windows 98 Low End copy'

# Use provided VM path or default
if [ -z "$1" ]; then
    echo "No VM path provided, using default: Windows 98 Low End copy"
    VM_PATH="$DEFAULT_VM"
else
    VM_PATH="$1"
fi

# Check if build exists
if [ ! -f "${BUILD_DIR}/86Box.app/Contents/MacOS/86Box" ]; then
    echo "Error: 86Box not found at ${BUILD_DIR}/86Box.app/Contents/MacOS/86Box"
    echo "Run ./scripts/build-and-sign.sh first"
    exit 1
fi

# Check if VM exists
if [ ! -d "$VM_PATH" ]; then
    echo "Error: VM path not found: $VM_PATH"
    exit 1
fi

echo "=========================================="
echo "Voodoo ARM64 JIT Validation Test"
echo "=========================================="
echo "VM path: $VM_PATH"
echo "Log file: $LOGFILE"
echo ""
echo "Look for these log patterns:"
echo "  - VOODOO JIT: GENERATE = JIT code being compiled"
echo "  - VOODOO JIT: EXECUTE  = JIT code being run"
echo "  - VOODOO JIT: cache HIT = JIT code being reused"
echo ""
echo "Starting 86Box... (press Ctrl+C to stop)"
echo "=========================================="
echo ""

# Run 86Box with logging
"${BUILD_DIR}/86Box.app/Contents/MacOS/86Box" -P "$VM_PATH" 2>&1 | tee "$LOGFILE"

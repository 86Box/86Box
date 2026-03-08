#!/bin/bash
set -e
echo "Cleaning build directory..."
rm -rf build/src/86Box.app
cmake --build build --target clean 2>&1
echo "Building fresh..."
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1
echo "Signing..."
codesign -s - --entitlements src/mac/entitlements.plist --force build/src/86Box.app 2>&1
echo "CLEAN BUILD + SIGN OK"

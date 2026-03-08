#!/bin/bash
set -e
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1
codesign -s - --entitlements src/mac/entitlements.plist --force build/src/86Box.app 2>&1
echo "BUILD + SIGN OK"

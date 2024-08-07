#!/bin/sh

URL="https://github.com/86Box/roms/archive/refs/tags/v4.2.zip"
TMP_FILE="/tmp/86Box-ROMS.zip"
EXTRACT_DIR="/tmp/86Box-ROMS-extracted"
DEFAULT_TARGET_DIR="$HOME/.local/share/86Box/roms/"
TARGET_DIR=${TARGET_DIR:-$DEFAULT_TARGET_DIR}

install_roms() {
  if [ -d "$TARGET_DIR" ] && [ "$(ls -A $TARGET_DIR)" ]; then
    echo "ROMS already installed in $TARGET_DIR"
    echo "To (re)install, please first remove ROMS with -r parameter"
    exit 1
  fi
  fetch -o "$TMP_FILE" "$URL"

  if [ $? -ne 0 ]; then
    echo "Failed to download the file from $URL"
    exit 1
  fi

  mkdir -p "$EXTRACT_DIR"
  unzip "$TMP_FILE" -d "$EXTRACT_DIR"

  if [ $? -ne 0 ]; then
    echo "Failed to decompress the file"
    rm "$TMP_FILE"
    exit 1
  fi

  mkdir -p "$TARGET_DIR"
  cd "$EXTRACT_DIR"
  TOP_LEVEL_DIR=$(find . -mindepth 1 -maxdepth 1 -type d)

  if [ -d "$TOP_LEVEL_DIR" ]; then
    mv "$TOP_LEVEL_DIR"/* "$TARGET_DIR"
  fi

  rm -rf "$TMP_FILE" "$EXTRACT_DIR"
  echo "ROMS installed successfully in $TARGET_DIR"
}

remove_roms() {
  if [ -d "$TARGET_DIR" ]; then
    rm -rf "$TARGET_DIR"
    echo "ROMS removed successfully from $TARGET_DIR"
  else
    echo "No ROMS directory found in $TARGET_DIR"
  fi
}

help() {
  echo ""
  echo "$0 [-h|-i|-r]"
  echo "  -h : this help"
  echo "  -i : install (this parameter can be omitted)"
  echo "  -r : remove the ROMS"
  echo ""
}

case "$1" in
  -h)
   help
   ;;
  -r)
    remove_roms
    ;;
  -i|*)
    install_roms
    ;;
esac

exit 0

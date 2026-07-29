#!/bin/bash
set -e

cd "$(dirname "${BASH_SOURCE[0]}")"
. build_common

if [[ $# != 1 ]]
then
    echo "Syntax: $0 <target>" >&2
    exit 1
fi

TARGET="$1"
ARCHIVE="$OUTPUT_DIR/scrcpy-auto-$TARGET-$VERSION.tar.gz"
ROOT="scrcpy-auto-$TARGET-$VERSION"

if [[ ! -f "$ARCHIVE" ]]
then
    echo "Missing archive: $ARCHIVE" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT
tar xzf "$ARCHIVE" -C "$TMP_DIR"

PACKAGE_DIR="$TMP_DIR/$ROOT"
test -x "$PACKAGE_DIR/scrcpy-auto"
test -x "$PACKAGE_DIR/adb"
test -s "$PACKAGE_DIR/scrcpy-auto-server"

# A portable binary resolves both adb and scrcpy-auto-server next to its real
# executable path. Keep all three files together in every published archive.
test -f "$PACKAGE_DIR/scrcpy-auto.1"
test -f "$PACKAGE_DIR/LICENSE"

echo "Verified portable archive: $ARCHIVE"

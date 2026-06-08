#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SDK_DIR=${1:-"$SCRIPT_DIR/../duo-buildroot-sdk"}
PATCH="$SCRIPT_DIR/patches/0001-cv1800b-duo-gc9a01-fbtft-framebuffer.patch"

if [ ! -d "$SDK_DIR/.git" ]; then
	echo "SDK directory is not a git checkout: $SDK_DIR" >&2
	exit 1
fi

cd "$SDK_DIR"
git apply --check "$PATCH"
git apply "$PATCH"
echo "Applied: $PATCH"

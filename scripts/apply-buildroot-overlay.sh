#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 PATH_TO_BUILDROOT_5a6d31c" >&2
	exit 2
fi

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tree=$1
[ -f "$tree/Makefile" ] || {
	echo "not a Buildroot source tree: $tree" >&2
	exit 1
}

cp -a "$repo/x11/buildroot-overlay/." "$tree/"
echo "Applied XD-B8600 Buildroot overlay to $tree"

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

ncurses_patch=$repo/x11/buildroot-tree-patches/0001-ncurses-handle-hex-terminfo-directories.patch
marker='Docker Desktop bind mounts can be case-insensitive'
if ! grep -Fq "$marker" "$tree/package/ncurses/ncurses.mk"; then
	patch -d "$tree" -p1 --forward < "$ncurses_patch"
fi
echo "Applied XD-B8600 Buildroot overlay to $tree"

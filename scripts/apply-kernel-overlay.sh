#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 PATH_TO_LINUX_6_1" >&2
	exit 2
fi

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tree=$1
[ -f "$tree/Makefile" ] || {
	echo "not a Linux source tree: $tree" >&2
	exit 1
}

cp -a "$repo/kernel/overlay/." "$tree/"
for kernel_patch in "$repo"/kernel/patches/*.patch; do
	[ -f "$kernel_patch" ] || continue
	patch -d "$tree" -p1 --dry-run < "$kernel_patch" >/dev/null
	patch -d "$tree" -p1 < "$kernel_patch"
done
echo "Applied XD-B8600 Linux 6.1 overlay to $tree"

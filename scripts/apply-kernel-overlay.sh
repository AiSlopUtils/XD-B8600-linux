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
echo "Applied XD-B8600 Linux 6.1 overlay to $tree"

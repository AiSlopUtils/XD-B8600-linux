#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tree=${1:-$repo/build/buildroot-2019.02.11}
url=https://gitlab.com/buildroot.org/buildroot.git
tag=2019.02.11
expected=5a6d31c87e1573bc83986471c194b944d7a365b7

[ "$#" -le 1 ] || {
	echo "usage: $0 [BUILDROOT_SOURCE_TREE]" >&2
	exit 2
}

if [ -e "$tree" ]; then
	[ -d "$tree/.git" ] || {
		echo "prepare-buildroot: existing path is not a Git checkout: $tree" >&2
		exit 1
	}
else
	mkdir -p "$(dirname "$tree")"
	git clone --branch "$tag" --depth 1 "$url" "$tree"
fi

actual=$(git -C "$tree" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
	echo "prepare-buildroot: expected $expected, found $actual in $tree" >&2
	exit 1
}

"$repo/scripts/apply-buildroot-overlay.sh" "$tree"

echo "Prepared Buildroot $tag ($expected)"
echo "  source: $tree"
echo "  config: $repo/x11/buildroot-2019-exword.defconfig"

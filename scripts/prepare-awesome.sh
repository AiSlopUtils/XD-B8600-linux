#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tree=${1:-$repo/build/awesome-v1.3-src}
url=https://github.com/awesomeWM/awesome.git
expected=d4f1b99c93c7da10af774500f3c007e77a765c5d
patch_file=$repo/x11/awesome/0001-exword-awesome-1.3.patch

[ "$#" -le 1 ] || {
	echo "usage: $0 [AWESOME_SOURCE_TREE]" >&2
	exit 2
}

if [ -e "$tree" ]; then
	if ! git -C "$tree" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		echo "prepare-awesome: existing path is not a Git checkout: $tree" >&2
		exit 1
	fi
else
	mkdir -p "$(dirname "$tree")"
	git clone --branch v1.3 --depth 1 "$url" "$tree"
fi

actual=$(git -C "$tree" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
	echo "prepare-awesome: expected $expected, found $actual in $tree" >&2
	exit 1
}

if git -C "$tree" apply --reverse --check "$patch_file" 2>/dev/null; then
	echo "Awesome EX-word patch is already applied"
elif git -C "$tree" apply --check "$patch_file"; then
	git -C "$tree" apply "$patch_file"
	echo "Applied Awesome EX-word patch"
else
	echo "prepare-awesome: patch does not apply cleanly to $tree" >&2
	exit 1
fi

echo "Prepared Awesome 1.3 source ($expected)"
echo "  source: $tree"
echo "  patch:  $patch_file"

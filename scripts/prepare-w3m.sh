#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
tree=${1:-$repo/build/w3m-src}
url=https://github.com/tats/w3m.git
expected=ee66aabc3987000c2851bce6ade4dcbb0b037d81

[ "$#" -le 1 ] || {
	echo "usage: $0 [W3M_SOURCE_TREE]" >&2
	exit 2
}

if [ -e "$tree" ]; then
	if ! git -C "$tree" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
		echo "prepare-w3m: existing path is not a Git checkout: $tree" >&2
		exit 1
	fi
else
	mkdir -p "$(dirname "$tree")"
	git init "$tree" >/dev/null
	git -C "$tree" remote add origin "$url"
	git -C "$tree" fetch --depth 1 origin "$expected"
	git -C "$tree" checkout --detach FETCH_HEAD >/dev/null
fi

actual=$(git -C "$tree" rev-parse HEAD)
[ "$actual" = "$expected" ] || {
	echo "prepare-w3m: expected $expected, found $actual in $tree" >&2
	exit 1
}

echo "Prepared w3m source ($expected)"
echo "  source: $tree"

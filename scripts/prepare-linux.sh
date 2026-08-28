#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
downloads=$repo/build/downloads
tree=$repo/build/linux-6.1
archive=$downloads/linux-6.1.tar.xz
url=https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz
expected=2ca1f17051a430f6fed1196e4952717507171acfd97d96577212502703b25deb

[ ! -e "$tree" ] || {
	echo "prepare-linux: $tree already exists" >&2
	exit 1
}
mkdir -p "$downloads"
if [ ! -f "$archive" ]; then
	curl -L --fail --output "$archive" "$url"
fi

if command -v sha256sum >/dev/null 2>&1; then
	actual=$(sha256sum "$archive" | sed 's/ .*//')
else
	actual=$(shasum -a 256 "$archive" | sed 's/ .*//')
fi
[ "$actual" = "$expected" ] || {
	echo "prepare-linux: SHA-256 mismatch for $archive" >&2
	exit 1
}

tar -xJf "$archive" -C "$repo/build"
"$repo/scripts/apply-kernel-overlay.sh" "$tree"

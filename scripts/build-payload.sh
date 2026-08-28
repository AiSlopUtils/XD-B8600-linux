#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
kernel=${1:-$repo/build/kernel/arch/sh/boot/zImage}
x11=${2:-$repo/artifacts/x11-xterm.sqfs}
output=${3:-$repo/build/LINUX.PAY}

[ -f "$kernel" ] || {
	echo "build-payload: kernel not found: $kernel" >&2
	exit 1
}
[ -f "$x11" ] || {
	echo "build-payload: X11 image not found: $x11" >&2
	exit 1
}
mkdir -p "$(dirname "$output")"
python3 "$repo/payload/pack_payload.py" \
	--append-blob "$x11" --append-offset 0x1b0000 \
	--append-capacity 0x350000 "$kernel" "$output"
if command -v sha256sum >/dev/null 2>&1; then
	sha256sum "$output"
else
	shasum -a 256 "$output"
fi

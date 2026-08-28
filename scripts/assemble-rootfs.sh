#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
output=${1:-$repo/build/rootfs}

[ ! -e "$output" ] || {
	echo "assemble-rootfs: output already exists: $output" >&2
	exit 1
}

mkdir -p "$output/bin"
cp -a "$repo/rootfs/overlay/." "$output/"
cp -p "$repo/artifacts/busybox" "$output/bin/busybox"
chmod 0755 "$output/bin/busybox"

while IFS= read -r link; do
	case "$link" in
		/*) ;;
		*) continue ;;
	esac
	if [ -e "$output$link" ] || [ -L "$output$link" ]; then
		continue
	fi
	mkdir -p "$output$(dirname "$link")"
	ln -s /bin/busybox "$output$link"
done < "$repo/rootfs/busybox.links"

ln -s bin/busybox "$output/init"
echo "Assembled initramfs root at $output"

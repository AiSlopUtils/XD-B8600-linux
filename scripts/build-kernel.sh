#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source_tree=$repo/build/linux-6.1
output=$repo/build/kernel
rootfs=$repo/build/rootfs
image=${EXWORD_SH4_IMAGE:-exword-sh4-linux:6.1}
jobs=${JOBS:-2}

[ -f "$source_tree/Makefile" ] || {
	echo "build-kernel: run scripts/prepare-linux.sh first" >&2
	exit 1
}
[ -x "$rootfs/bin/busybox" ] || {
	echo "build-kernel: run scripts/assemble-rootfs.sh first" >&2
	exit 1
}

mkdir -p "$output"
cp -p "$repo/kernel/exword_defconfig" "$output/.config"
"$source_tree/scripts/config" --file "$output/.config" --set-str \
	INITRAMFS_SOURCE \
	"/work/linux-work/build/rootfs /work/linux-work/rootfs/initramfs-devices.list"

docker build --platform linux/amd64 -t "$image" \
	-f "$repo/kernel/Dockerfile.sh4" "$repo/kernel"
docker run --rm --platform linux/amd64 \
	-v "$repo:/work/linux-work" -w /work/linux-work/build/linux-6.1 \
	"$image" sh -eu -c \
	'make O=/work/linux-work/build/kernel ARCH=sh CROSS_COMPILE=sh4-linux-gnu- olddefconfig
make O=/work/linux-work/build/kernel ARCH=sh CROSS_COMPILE=sh4-linux-gnu- -j"$1" zImage' \
	sh "$jobs"

zimage=$output/arch/sh/boot/zImage
size=$(wc -c < "$zimage")
[ "$size" -le 1769472 ] || {
	echo "build-kernel: zImage overlaps X slot at offset 0x1b0000" >&2
	exit 1
}
if command -v sha256sum >/dev/null 2>&1; then
	sha256sum "$zimage"
else
	shasum -a 256 "$zimage"
fi

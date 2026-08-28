#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source_tree=$repo/build/linux-6.1
output=${KERNEL_OUTPUT:-$repo/build/kernel}
rootfs=${ROOTFS:-$repo/build/rootfs}
image=${EXWORD_SH4_IMAGE:-exword-sh4-linux:6.1}
jobs=${JOBS:-2}

case $output in
	/*) ;;
	*) output=$repo/$output ;;
esac
case $rootfs in
	/*) ;;
	*) rootfs=$repo/$rootfs ;;
esac

case $output in
	"$repo"/*) output_rel=${output#"$repo"/} ;;
	*)
		echo "build-kernel: KERNEL_OUTPUT must be inside $repo" >&2
		exit 2
		;;
esac

case $jobs in
	''|*[!0-9]*|0)
		echo "build-kernel: JOBS must be a positive integer" >&2
		exit 2
		;;
esac

command -v docker >/dev/null 2>&1 || {
	echo "build-kernel: Docker is required" >&2
	exit 1
}
case $rootfs in
	"$repo"/*) rootfs_rel=${rootfs#"$repo"/} ;;
	*)
		echo "build-kernel: ROOTFS must be inside $repo" >&2
		exit 2
		;;
esac

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
	"/work/linux-work/$rootfs_rel /work/linux-work/rootfs/initramfs-devices.list"

docker build --platform linux/amd64 -t "$image" \
	-f "$repo/kernel/Dockerfile.sh4" "$repo/kernel"
docker run --rm --platform linux/amd64 \
	--user "$(id -u):$(id -g)" -e HOME=/tmp \
	-v "$repo:/work/linux-work" -w /work/linux-work/build/linux-6.1 \
	"$image" sh -eu -c \
	'make O="/work/linux-work/$2" ARCH=sh CROSS_COMPILE=sh4-linux-gnu- olddefconfig
make O="/work/linux-work/$2" ARCH=sh CROSS_COMPILE=sh4-linux-gnu- -j"$1" zImage' \
	sh "$jobs" "$output_rel"

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

#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
source_tree=$repo/build/buildroot-2019.02.11
sqfs=$repo/build/x11-xterm.sqfs
image=${EXWORD_BUILDROOT_IMAGE:-exword-buildroot-x11:2019.02.11}
jobs=${JOBS:-2}

case $jobs in
	''|*[!0-9]*|0)
		echo "build-x11: JOBS must be a positive integer" >&2
		exit 2
		;;
esac

command -v docker >/dev/null 2>&1 || {
	echo "build-x11: Docker is required" >&2
	exit 1
}

uid=$(id -u)
gid=$(id -g)
[ "$uid" -ne 0 ] || {
	echo "build-x11: run this script as an ordinary user, not root" >&2
	exit 1
}

"$repo/scripts/prepare-buildroot.sh" "$source_tree"

docker build -t "$image" \
	-f "$repo/x11/Dockerfile.buildroot-x11" "$repo/x11"

# Buildroot and package build directories must be on a case-sensitive Linux
# filesystem.  Docker Desktop bind mounts inherit the macOS filesystem's
# case folding; ncurses and other packages legitimately change their output
# layout when they detect that.  A named volume also keeps the multi-gigabyte
# toolchain cache out of the source checkout.
image_arch=$(docker image inspect "$image" --format '{{.Architecture}}')
repo_key=$(printf '%s\n' "$repo" | cksum | awk '{print $1}')
volume=${EXWORD_BUILDROOT_VOLUME:-exword-x11-2019-02-11-$image_arch-$repo_key}
docker volume create "$volume" >/dev/null

# Buildroot intentionally refuses to build as root.  Make the new volume
# writable by the invoking user, then retain those IDs for the actual build.
docker run --rm -v "$volume:/out" "$image" \
	sh -eu -c 'chown "$1:$2" /out' sh "$uid" "$gid"

docker run --rm \
	--user "$uid:$gid" -e HOME=/tmp \
	-v "$repo:/work" \
	-v "$volume:/work/build/buildroot-output-x11" \
	-w /work \
	"$image" sh -eu -c \
	'out=/work/build/buildroot-output-x11
	mkdir -p "$out"
	cp -p /work/x11/buildroot-2019-exword.defconfig "$out/.config"
	make -C /work/build/buildroot-2019.02.11 \
		O="$out" BR2_DL_DIR="$out/dl" olddefconfig
	make -C /work/build/buildroot-2019.02.11 \
		O="$out" BR2_DL_DIR="$out/dl" -j"$1"
	rm -rf "$out/x11-stage"
	rm -f "$out/x11-xterm.sqfs" /work/build/x11-xterm.sqfs
	X11_CONFIG_ROOT=/work/x11/config /work/x11/package-x11.sh \
		"$out/target" "$out/host" \
		"$out/x11-stage" "$out/x11-xterm.sqfs"
	cp -p "$out/x11-xterm.sqfs" /work/build/x11-xterm.sqfs' \
	sh "$jobs"

[ -f "$sqfs" ] || {
	echo "build-x11: packager did not create $sqfs" >&2
	exit 1
}

echo "X11 image: $sqfs"
echo "Buildroot cache volume: $volume"
echo "To use it in a freshly built payload:"
echo "  ./scripts/build-payload.sh build/kernel/arch/sh/boot/zImage build/x11-xterm.sqfs"

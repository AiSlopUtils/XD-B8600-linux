#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
downloads=$repo/build/downloads
busybox_version=1.37.0
musl_version=1.2.5
busybox_archive=$downloads/busybox-$busybox_version.tar.bz2
musl_archive=$downloads/musl-$musl_version.tar.gz
busybox_tree=$repo/build/busybox-$busybox_version
musl_tree=$repo/build/musl-$musl_version
musl_prefix=$repo/build/sh4-musl
toolchain_marker=$musl_prefix/.exword-toolchain-ready
sdk_dir=${DEVKITSH4_DIR:-$repo/build/loader-sdk}
output=${1:-$repo/build/busybox}
image=${EXWORD_SH4_IMAGE:-exword-sh4-linux:6.1}
jobs=${JOBS:-2}

busybox_url=https://busybox.net/downloads/busybox-$busybox_version.tar.bz2
busybox_archive_sha=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
busybox_binary_sha=1e47eade4a74227f7df3e49be3818b5496f7cfed9aed8fa9735e0b342881b9cb
musl_url=https://musl.libc.org/releases/musl-$musl_version.tar.gz
musl_archive_sha=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4

case $jobs in
	''|*[!0-9]*|0)
		echo "build-busybox: JOBS must be a positive integer" >&2
		exit 2
		;;
esac

for command in curl docker tar; do
	command -v "$command" >/dev/null 2>&1 || {
		echo "build-busybox: required command not found: $command" >&2
		exit 1
	}
done

sha256_file()
{
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | awk '{print $1}'
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$1" | awk '{print $1}'
	else
		echo "build-busybox: sha256sum or shasum is required" >&2
		exit 1
	fi
}

fetch()
{
	url=$1
	archive=$2
	expected=$3

	if [ ! -f "$archive" ]; then
		curl -L --fail --output "$archive" "$url"
	fi
	actual=$(sha256_file "$archive")
	[ "$actual" = "$expected" ] || {
		echo "build-busybox: SHA-256 mismatch for $archive" >&2
		echo "expected: $expected" >&2
		echo "actual:   $actual" >&2
		exit 1
	}
}

[ -f "$repo/build/linux-6.1/Makefile" ] || "$repo/scripts/prepare-linux.sh"
DEVKITSH4_DIR=$sdk_dir "$repo/scripts/prepare-loader-sdk.sh"

mkdir -p "$downloads"
fetch "$busybox_url" "$busybox_archive" "$busybox_archive_sha"
fetch "$musl_url" "$musl_archive" "$musl_archive_sha"

if [ -e "$busybox_tree" ]; then
	[ -f "$busybox_tree/Makefile" ] || {
		echo "build-busybox: existing path is not a BusyBox tree: $busybox_tree" >&2
		exit 1
	}
else
	mkdir -p "$busybox_tree"
	tar -xjf "$busybox_archive" -C "$busybox_tree" --strip-components=1
fi

if [ -e "$musl_tree" ]; then
	[ -x "$musl_tree/configure" ] || {
		echo "build-busybox: existing path is not a musl tree: $musl_tree" >&2
		exit 1
	}
else
	mkdir -p "$musl_tree"
	tar -xzf "$musl_archive" -C "$musl_tree" --strip-components=1
fi

if [ -e "$musl_prefix" ] && [ ! -f "$toolchain_marker" ]; then
	echo "build-busybox: incomplete toolchain directory: $musl_prefix" >&2
	echo "Remove that ignored build directory and rerun this script." >&2
	exit 1
fi

docker build --platform linux/amd64 -t "$image" \
	-f "$repo/kernel/Dockerfile.sh4" "$repo/kernel"

uid=$(id -u)
gid=$(id -g)
docker run --rm --platform linux/amd64 \
	--user "$uid:$gid" -e HOME=/tmp \
	-v "$repo:/work" -v "$sdk_dir/devkitPro:/opt/devkitPro:ro" \
	-w /work "$image" sh -eu -c '
	musl=/work/build/musl-1.2.5
	prefix=/work/build/sh4-musl
	busybox=/work/build/busybox-1.37.0
	header_build=/work/build/linux-headers-6.1
	sdk=/opt/devkitPro/devkitSH4/lib/gcc/sh-elf/8.3.0

	if [ ! -f "$prefix/.exword-toolchain-ready" ]; then
		cd "$musl"
		if [ -f config.mak ]; then
			make distclean
		fi
		./configure --target=sh4-linux-musl \
			CROSS_COMPILE=sh4-linux-gnu- CC=sh4-linux-gnu-gcc \
			CFLAGS="-mb -m4a-nofpu -Os" \
			--prefix="$prefix" --disable-shared
		make -j"$1"
		make install

		mkdir -p "$header_build"
		make -C /work/build/linux-6.1 O="$header_build" ARCH=sh \
			INSTALL_HDR_PATH="$prefix" headers_install

		cp "$sdk/crtbeginS.o" "$prefix/lib/"
		cp "$sdk/crtendS.o" "$prefix/lib/"
		cp "$sdk/libgcc.a" "$prefix/lib/libgcc-sdk.a"
		sh4-linux-gnu-nm -g --defined-only "$prefix/lib/libgcc-sdk.a" \
			2>/dev/null |
			awk '\''NF == 3 && substr($3, 1, 3) == "___" {
				print $3, substr($3, 2)
			}'\'' | sort -u > "$prefix/lib/libgcc-symbol-map"
		sh4-linux-gnu-objcopy \
			--redefine-syms="$prefix/lib/libgcc-symbol-map" \
			"$prefix/lib/libgcc-sdk.a" "$prefix/lib/libgcc.a"

		sed -i '\''2c exec "${REALGCC:-sh4-linux-gnu-gcc}" -mb -m4a-nofpu -Wl,-EB -B/work/build/sh4-musl/lib/ "$@" -specs /work/build/sh4-musl/lib/musl-gcc.specs'\'' \
			"$prefix/bin/musl-gcc"

		echo "int main(void) { return 0; }" |
			"$prefix/bin/musl-gcc" -static -xc -o /tmp/exword-musl-probe -
		header=$(sh4-linux-gnu-readelf -h /tmp/exword-musl-probe)
		echo "$header" | grep -q "Class:.*ELF32"
		echo "$header" | grep -q "Data:.*big endian"
		echo "$header" | grep -Eq "Machine:.*(Renesas|SuperH)"
		! sh4-linux-gnu-readelf -l /tmp/exword-musl-probe |
			grep -q "Requesting program interpreter"
		touch "$prefix/.exword-toolchain-ready"
	fi

	cd "$busybox"
	make CC="$prefix/bin/musl-gcc" clean
	cp /work/rootfs/busybox-1.37.0.config .config
	SOURCE_DATE_EPOCH=1787829971 \
		make CC="$prefix/bin/musl-gcc" -j"$1" busybox

	header=$(sh4-linux-gnu-readelf -h busybox)
	echo "$header" | grep -q "Class:.*ELF32"
	echo "$header" | grep -q "Data:.*big endian"
	echo "$header" | grep -Eq "Machine:.*(Renesas|SuperH)"
	! sh4-linux-gnu-readelf -l busybox |
		grep -q "Requesting program interpreter"
	' sh "$jobs"

built_sha=$(sha256_file "$busybox_tree/busybox")
if [ "${SKIP_BUSYBOX_ARTIFACT_CHECK:-0}" != 1 ] && \
	[ "$built_sha" != "$busybox_binary_sha" ]; then
	echo "build-busybox: result does not match the verified release binary" >&2
	echo "expected: $busybox_binary_sha" >&2
	echo "actual:   $built_sha" >&2
	echo "Set SKIP_BUSYBOX_ARTIFACT_CHECK=1 only for intentional config changes." >&2
	exit 1
fi

mkdir -p "$(dirname "$output")"
cp -p "$busybox_tree/busybox" "$output"
chmod 0755 "$output"
echo "$built_sha  $output"
echo "BusyBox binary: $output"

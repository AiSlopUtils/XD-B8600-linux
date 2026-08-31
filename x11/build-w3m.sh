#!/bin/sh
set -eu

if [ "$#" -lt 4 ] || [ "$#" -gt 5 ]; then
	echo "usage: $0 SOURCE BUILD_DIR BUILDROOT_TARGET BUILDROOT_HOST [JOBS]" >&2
	exit 2
fi

source_tree=${1%/}
build_dir=${2%/}
target=${3%/}
host=${4%/}
jobs=${5:-2}
script_dir=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
tool=$host/bin/sh4aeb-buildroot-linux-musl
sysroot=$host/sh4aeb-buildroot-linux-musl/sysroot

case $jobs in
	''|*[!0-9]*|0)
		echo "build-w3m: JOBS must be a positive integer" >&2
		exit 2
		;;
esac
case $build_dir in
	''|/)
		echo "build-w3m: refusing unsafe build directory: $build_dir" >&2
		exit 2
		;;
esac

[ -x "$source_tree/configure" ]
[ -x "$tool-gcc" ]
[ -f "$sysroot/usr/include/gc.h" ]
[ -f "$script_dir/w3m/host-gc/gc.h" ]

rm -rf "$build_dir"
mkdir -p "$build_dir"
cd "$build_dir"

build_triplet=$(cc -dumpmachine)
env \
	CC="$tool-gcc" \
	AR="$tool-gcc-ar" \
	RANLIB="$tool-gcc-ranlib" \
	CFLAGS="-Os -flto -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables" \
	LDFLAGS="-flto -Wl,--gc-sections" \
	ac_cv_func_setpgrp_void=yes \
	"$source_tree/configure" \
		--host=sh4aeb-buildroot-linux-musl \
		--build="$build_triplet" \
		--prefix=/usr \
		--disable-m17n --disable-nls --disable-image --disable-xface \
		--disable-mouse --disable-menu --disable-history --disable-alarm \
		--disable-cookie --disable-nntp --disable-gopher --disable-dict \
		--disable-help-cgi --disable-external-uri-loader \
		--disable-w3mmailer --disable-ipv6 --disable-digest-auth \
		--without-ssl --with-termlib=ncurses --with-gc="$sysroot/usr"

# Upstream's table generator is executed during the build.  First make its
# target objects, then replace the un-runnable SH-4 executable with the same
# generator built for the Docker host.
make -j"$jobs" mktable AR="$tool-gcc-ar" RANLIB="$tool-gcc-ranlib"
cc -O2 -I. -I"$source_tree" -I"$script_dir/w3m/host-gc" \
	-DHAVE_CONFIG_H -DDUMMY -o mktable \
	"$source_tree/mktable.c" "$source_tree/Str.c" \
	"$source_tree/hash.c" "$source_tree/myctype.c" \
	"$source_tree/entity.c"

make -j"$jobs" AR="$tool-gcc-ar" RANLIB="$tool-gcc-ranlib"
mkdir -p "$target/usr/bin"
install -m 0755 w3m "$target/usr/bin/w3m"
"$tool-strip" "$target/usr/bin/w3m"

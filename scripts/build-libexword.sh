#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_root="$repo_root/tools/libexword"
output_dir=${LIBEXWORD_OUTPUT_DIR:-"$repo_root/build/libexword"}
data_root=${EXWORD_DATA_ROOT:-"$repo_root/build/exword-data"}

if ! command -v cc >/dev/null 2>&1 || ! command -v pkg-config >/dev/null 2>&1; then
    echo "error: a C compiler and pkg-config are required" >&2
    echo "macOS: brew install pkg-config libusb readline" >&2
    echo "Debian/Ubuntu: sudo apt install build-essential pkg-config libusb-1.0-0-dev libreadline-dev" >&2
    exit 1
fi

if ! pkg-config --exists libusb-1.0; then
    echo "error: libusb-1.0 development files were not found by pkg-config" >&2
    echo "macOS: brew install libusb" >&2
    echo "Debian/Ubuntu: sudo apt install libusb-1.0-0-dev" >&2
    exit 1
fi

usb_cflags=$(pkg-config --cflags libusb-1.0)
usb_libs=$(pkg-config --libs libusb-1.0)
platform_cflags=
platform_ldflags=

case $(uname -s) in
    Darwin)
        if ! command -v brew >/dev/null 2>&1; then
            echo "error: this macOS build expects Homebrew libusb and readline" >&2
            echo "Install Homebrew, then run: brew install pkg-config libusb readline" >&2
            exit 1
        fi
        readline_prefix=$(brew --prefix readline)
        platform_cflags="-I$readline_prefix/include"
        platform_ldflags="-L$readline_prefix/lib -lreadline -liconv -framework IOKit -framework CoreServices -framework CoreFoundation"
        ;;
    Linux)
        platform_ldflags="-lreadline"
        ;;
    *)
        echo "error: supported native hosts are macOS and Linux" >&2
        exit 1
        ;;
esac

mkdir -p "$output_dir" "$data_root"

# -fsigned-char is required by the old OBEX implementation: several protocol
# buffers use plain char and must behave identically on every host compiler.
# The source list is explicit so building the transfer CLI does not depend on
# the project's historical Python-2/SWIG Autotools path.
cc -std=gnu11 -O2 -Wall -Wno-pointer-sign -Wno-deprecated-declarations \
    -fsigned-char \
    -I"$source_root/src" \
    $usb_cflags $platform_cflags \
    "$source_root/src/exword.c" \
    "$source_root/src/crypt.c" \
    "$source_root/src/obex.c" \
    "$source_root/src/databuffer.c" \
    "$source_root/src/main.c" \
    "$source_root/src/content.c" \
    "$source_root/src/util.c" \
    $usb_libs $platform_ldflags \
    -o "$output_dir/exword"

cp "$source_root/models.txt" "$data_root/models.txt"

echo "libexword command-line client built at $output_dir/exword"
echo "Run it through scripts/run-libexword.sh so its private data directory is set."

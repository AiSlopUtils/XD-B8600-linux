#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
download_dir=${EXWORD_DOWNLOAD_DIR:-"$repo_root/build/downloads"}
sdk_dir=${DEVKITSH4_DIR:-"$repo_root/build/loader-sdk"}
sdk_url=${DEVKITSH4_URL:-"https://github.com/MaxSignal/buildscripts/releases/download/Linux/devkitPro.tar.gz"}
expected_sha256=1609b719e62224521243fea086ea492759fd9ee4116e1450d8b5e69fccfc7555

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "error: sha256sum or shasum is required" >&2
        exit 1
    fi
}

if [ -n "${DEVKITSH4_ARCHIVE:-}" ]; then
    archive=$DEVKITSH4_ARCHIVE
    if [ ! -f "$archive" ]; then
        echo "error: DEVKITSH4_ARCHIVE is not a file: $archive" >&2
        exit 1
    fi
else
    archive="$download_dir/devkitPro-linux.tar.gz"
    mkdir -p "$download_dir"
    if [ ! -f "$archive" ]; then
        if ! command -v curl >/dev/null 2>&1; then
            echo "error: curl is required to download devkitSH4" >&2
            exit 1
        fi
        partial="$archive.part.$$"
        trap 'rm -f "$partial"' EXIT HUP INT TERM
        echo "Downloading the pinned devkitSH4 SDK archive..."
        curl --fail --location --retry 3 --output "$partial" "$sdk_url"
        downloaded_sha256=$(sha256_file "$partial")
        if [ "$downloaded_sha256" != "$expected_sha256" ]; then
            echo "error: downloaded SDK checksum mismatch" >&2
            echo "expected: $expected_sha256" >&2
            echo "actual:   $downloaded_sha256" >&2
            exit 1
        fi
        mv "$partial" "$archive"
        trap - EXIT HUP INT TERM
    fi
fi

actual_sha256=$(sha256_file "$archive")
if [ "$actual_sha256" != "$expected_sha256" ]; then
    echo "error: SDK archive checksum mismatch: $archive" >&2
    echo "expected: $expected_sha256" >&2
    echo "actual:   $actual_sha256" >&2
    exit 1
fi

if [ -x "$sdk_dir/devkitPro/devkitSH4/bin/sh-elf-gcc" ] \
    && [ -x "$sdk_dir/devkitPro/tools/bin/elf2d01" ] \
    && [ -f "$sdk_dir/devkitPro/libdataplus/lib/libdataplus.a" ]; then
    echo "Loader SDK is ready at $sdk_dir/devkitPro"
    exit 0
fi

if [ -e "$sdk_dir" ]; then
    echo "error: SDK destination exists but is incomplete: $sdk_dir" >&2
    echo "Move it aside and rerun this script." >&2
    exit 1
fi

mkdir -p "$(dirname -- "$sdk_dir")"
temporary_dir="$sdk_dir.tmp.$$"
trap 'rm -rf "$temporary_dir"' EXIT HUP INT TERM
mkdir "$temporary_dir"
tar -xzf "$archive" -C "$temporary_dir"

if [ ! -x "$temporary_dir/devkitPro/devkitSH4/bin/sh-elf-gcc" ] \
    || [ ! -x "$temporary_dir/devkitPro/tools/bin/elf2d01" ] \
    || [ ! -f "$temporary_dir/devkitPro/libdataplus/lib/libdataplus.a" ]; then
    echo "error: verified archive does not contain the expected SDK layout" >&2
    exit 1
fi

mv "$temporary_dir" "$sdk_dir"
trap - EXIT HUP INT TERM
echo "Loader SDK is ready at $sdk_dir/devkitPro"

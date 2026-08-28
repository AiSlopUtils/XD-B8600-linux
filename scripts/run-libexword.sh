#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
binary=${LIBEXWORD_BINARY:-"$repo_root/build/libexword/exword"}
data_root=${EXWORD_DATA_ROOT:-"$repo_root/build/exword-data"}

if [ ! -x "$binary" ]; then
    echo "error: libexword has not been built: $binary" >&2
    echo "Run ./scripts/build-libexword.sh first." >&2
    exit 1
fi

mkdir -p "$data_root"
if [ ! -f "$data_root/models.txt" ]; then
    cp "$repo_root/tools/libexword/models.txt" "$data_root/models.txt"
fi

EXWORD_DATA_DIR="$data_root"
export EXWORD_DATA_DIR
exec "$binary" "$@"

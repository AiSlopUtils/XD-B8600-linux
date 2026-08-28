#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
artifact="$repo_root/artifacts/lnx03-casiotxt-loader-20260827.d01"
data_root=${EXWORD_DATA_ROOT:-"$repo_root/build/exword-data"}
expected_sha256=fa037d2e364845e19983aa617471870da669776994feb148970cf50315617a45

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

actual_sha256=$(sha256_file "$artifact")
if [ "$actual_sha256" != "$expected_sha256" ]; then
    echo "error: checked-in loader artifact failed its checksum" >&2
    echo "expected: $expected_sha256" >&2
    echo "actual:   $actual_sha256" >&2
    exit 1
fi

for region in ja cn; do
    source_html="$repo_root/loader/html/$region"
    destination="$data_root/$region/LNX03"
    mkdir -p "$destination"
    cp "$artifact" "$destination/lnx03.d01"
    for source_file in "$source_html"/*.htm; do
        output_file="$destination/${source_file##*/}"
        sed \
            -e 's/@APPTITLE/Linux Loader Startup Test/g' \
            -e 's/@APPID/LNX03/g' \
            -e 's/@APPMOD/lnx03.d01/g' \
            "$source_file" > "$output_file"
    done
    : > "$destination/fileinfo.cji"
done

echo "Known-good loader package staged at:"
echo "  $data_root/ja/LNX03"
echo "  $data_root/cn/LNX03"

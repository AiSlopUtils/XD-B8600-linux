#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk_dir=${DEVKITSH4_DIR:-"$repo_root/build/loader-sdk"}
data_root=${EXWORD_DATA_ROOT:-"$repo_root/build/exword-data"}
image_name=${EXWORD_LOADER_IMAGE:-"xd-b8600-loader:ubuntu-22.04"}
expected_loader_sha256=fa037d2e364845e19983aa617471870da669776994feb148970cf50315617a45

if ! command -v docker >/dev/null 2>&1; then
    echo "error: Docker is required to run the Linux/x86-64 devkitSH4 SDK" >&2
    exit 1
fi

DEVKITSH4_DIR="$sdk_dir" "$repo_root/scripts/prepare-loader-sdk.sh"

docker build \
    --platform linux/amd64 \
    --file "$repo_root/loader/Dockerfile" \
    --tag "$image_name" \
    "$repo_root/loader"

docker run --rm \
    --platform linux/amd64 \
    --user "$(id -u):$(id -g)" \
    --volume "$repo_root:/work" \
    --volume "$sdk_dir/devkitPro:/opt/devkitPro:ro" \
    --env DEVKITPRO=/opt/devkitPro \
    --env DEVKITSH4=/opt/devkitPro/devkitSH4 \
    --env PATH=/opt/devkitPro/tools/bin:/opt/devkitPro/devkitSH4/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    "$image_name" \
    sh -ec 'make -C /work/loader clean && make -C /work/loader app'

ja_binary="$repo_root/loader/build/ja/LNX03/lnx03.d01"
cn_binary="$repo_root/loader/build/cn/LNX03/lnx03.d01"
release_binary="$repo_root/artifacts/lnx03-casiotxt-loader-20260827.d01"

if ! cmp -s "$ja_binary" "$cn_binary"; then
    echo "error: Japanese and Chinese loader binaries differ" >&2
    exit 1
fi

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

built_sha256=$(sha256_file "$ja_binary")
if [ "${SKIP_LOADER_ARTIFACT_CHECK:-0}" != 1 ]; then
    if [ "$built_sha256" != "$expected_loader_sha256" ] \
        || ! cmp -s "$ja_binary" "$release_binary"; then
        echo "error: loader build does not match the checked-in release artifact" >&2
        echo "expected: $expected_loader_sha256" >&2
        echo "actual:   $built_sha256" >&2
        echo "Set SKIP_LOADER_ARTIFACT_CHECK=1 only while intentionally changing loader source." >&2
        exit 1
    fi
fi

for region in ja cn; do
    source_dir="$repo_root/loader/build/$region/LNX03"
    destination="$data_root/$region/LNX03"
    mkdir -p "$destination"
    cp -R "$source_dir/." "$destination/"
done

echo "Loader build complete (SHA-256 $built_sha256)."
echo "Installable packages:"
echo "  $data_root/ja/LNX03"
echo "  $data_root/cn/LNX03"

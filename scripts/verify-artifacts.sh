#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
manifest=$repo/artifacts/SHA256SUMS

if command -v sha256sum >/dev/null 2>&1; then
	sha256()
	{
		sha256sum "$1" | awk '{print $1}'
	}
elif command -v shasum >/dev/null 2>&1; then
	sha256()
	{
		shasum -a 256 "$1" | awk '{print $1}'
	}
else
	echo "verify-artifacts: sha256sum or shasum is required" >&2
	exit 1
fi

[ -f "$manifest" ] || {
	echo "verify-artifacts: missing $manifest" >&2
	exit 1
}

while read -r expected relative; do
	[ -n "$expected" ] || continue
	case "$relative" in
		/* | *../*)
			echo "verify-artifacts: unsafe manifest path: $relative" >&2
			exit 1
			;;
	esac
	file=$repo/artifacts/$relative
	[ -f "$file" ] || {
		echo "verify-artifacts: missing artifact: $relative" >&2
		exit 1
	}
	actual=$(sha256 "$file")
	[ "$actual" = "$expected" ] || {
		echo "verify-artifacts: SHA-256 mismatch: $relative" >&2
		echo "  expected $expected" >&2
		echo "  actual   $actual" >&2
		exit 1
	}
	printf 'OK  %s\n' "$relative"
done < "$manifest"

command -v python3 >/dev/null 2>&1 || {
	echo "verify-artifacts: python3 is required for the payload check" >&2
	exit 1
}
command -v cmp >/dev/null 2>&1 || {
	echo "verify-artifacts: cmp is required for the payload check" >&2
	exit 1
}

verify_tmp=$(mktemp -d "${TMPDIR:-/tmp}/exword-payload-verify.XXXXXX")
trap 'rm -rf -- "$verify_tmp"' EXIT HUP INT TERM

python3 "$repo/payload/pack_payload.py" \
	--append-blob "$repo/artifacts/x11-xterm.sqfs" \
	--append-offset 0x1b0000 --append-capacity 0x350000 \
	"$repo/artifacts/zImage" "$verify_tmp/LINUX.PAY" >/dev/null

cmp "$repo/artifacts/LINUX.PAY" "$verify_tmp/LINUX.PAY"
echo "OK  deterministic EXWPAY1 repack"

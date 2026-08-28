#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

for script in \
	"$repo"/scripts/*.sh \
	"$repo"/kernel/*.sh \
	"$repo"/rootfs/*.sh \
	"$repo"/x11/*.sh
do
	[ -f "$script" ] || continue
	first_line=$(sed -n '1p' "$script")
	case "$first_line" in
		*bash*) bash -n "$script" ;;
		*) sh -n "$script" ;;
	esac
	printf 'OK  syntax %s\n' "${script#"$repo"/}"
done

for source in "$repo"/payload/*.py; do
	[ -f "$source" ] || continue
	python3 -c \
		'import ast, pathlib, sys; ast.parse(pathlib.Path(sys.argv[1]).read_text(), filename=sys.argv[1])' \
		"$source"
	printf 'OK  syntax %s\n' "${source#"$repo"/}"
done

# Loader metadata must retain CRLF exactly. Casio's installer consumes these
# files, and a user's core.autocrlf setting must not silently rewrite them.
for source in "$repo"/loader/html/ja/*.htm "$repo"/loader/html/cn/*.htm; do
	python3 -c '
import pathlib, sys
data = pathlib.Path(sys.argv[1]).read_bytes()
if b"\r\n" not in data or b"\n" in data.replace(b"\r\n", b""):
    raise SystemExit(f"loader HTML is not pure CRLF: {sys.argv[1]}")
' "$source"
	printf 'OK  CRLF %s\n' "${source#"$repo"/}"
done

# The compatibility alias may have its explanatory header, but its actual
# settings must remain identical to the canonical Buildroot defconfig.
alias_tmp=$(mktemp "${TMPDIR:-/tmp}/exword-defconfig-alias.XXXXXX")
trap 'rm -f -- "$alias_tmp"' EXIT HUP INT TERM
sed '1,3d' "$repo/x11/buildroot-x11-exword.defconfig" > "$alias_tmp"
cmp "$repo/x11/buildroot-2019-exword.defconfig" "$alias_tmp"
echo "OK  Buildroot defconfig alias"

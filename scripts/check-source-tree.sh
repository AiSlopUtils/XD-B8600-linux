#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)

for script in \
	"$repo"/scripts/*.sh \
	"$repo"/kernel/*.sh \
	"$repo"/rootfs/*.sh \
	"$repo"/x11/*.sh \
	"$repo"/x11/config/usr/bin/*
do
	[ -f "$script" ] || continue
	first_line=$(sed -n '1p' "$script")
	case "$first_line" in
		*bash*) bash -n "$script" ;;
		*) sh -n "$script" ;;
	esac
	printf 'OK  syntax %s\n' "${script#"$repo"/}"
done

for required in \
	"$repo/x11/apps/exdesk.c" \
	"$repo/x11/apps/file-lite.c" \
	"$repo/x11/apps/holostatus.c" \
	"$repo/x11/apps/snake.c" \
	"$repo/x11/apps/subpad-mouse.c" \
	"$repo/x11/apps/sublcd-test.c" \
	"$repo/x11/apps/touchdiag.c" \
	"$repo/x11/apps/x11-start-gate.c" \
	"$repo/x11/assets/custom-wallpaper-528x320.rgb565" \
	"$repo/x11/config/usr/share/X11/xkb/symbols/inet" \
	"$repo/x11/w3m/host-gc/gc.h" \
	"$repo/x11/w3m/COPYING" \
	"$repo/x11/awesome/0001-exword-awesome-1.3.patch" \
	"$repo/x11/buildroot-overlay/package/xterm/0001-linux-use-unix98-pty-session.patch"
do
	[ -s "$required" ] || {
		echo "missing X11 source input: ${required#"$repo"/}" >&2
		exit 1
	}
done

for kernel_patch in "$repo"/kernel/patches/*.patch; do
	[ -e "$kernel_patch" ] || continue
	[ -s "$kernel_patch" ] || {
		echo "missing kernel patch: ${kernel_patch#"$repo"/}" >&2
		exit 1
	}
	git apply --numstat "$kernel_patch" >/dev/null
done
echo "OK  optional kernel patches"

python3 -c '
import hashlib, pathlib, sys
path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
expected = "007623bb0f215d2e6ce893a81a86e0c76ff13e0214256f1c48614e035f6a5f0e"
if len(data) != 337920 or hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit(f"invalid custom wallpaper asset: {path}")
' "$repo/x11/assets/custom-wallpaper-528x320.rgb565"
echo "OK  custom wallpaper size and SHA-256"

# Parse the two upstream patches even when their downloaded source trees are
# absent.  This catches a truncated/corrupt patch in an offline clean clone.
git apply --numstat \
	"$repo/x11/awesome/0001-exword-awesome-1.3.patch" >/dev/null
git apply --numstat \
	"$repo/x11/buildroot-overlay/package/xterm/0001-linux-use-unix98-pty-session.patch" >/dev/null
echo "OK  X11 pinned patches and helper sources"

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

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
	echo "usage: $0 BUILDROOT_TARGET BUILDROOT_HOST STAGE OUTPUT.sqfs" >&2
	exit 2
fi

target=${1%/}
host=${2%/}
stage=${3%/}
output=$4
readelf=$host/bin/sh4aeb-buildroot-linux-musl-readelf
config_root=${X11_CONFIG_ROOT:-/work/x11-root}

[[ -d $target && -x $readelf ]]
[[ ! -e $stage ]] || {
	echo "package-x11: staging path already exists: $stage" >&2
	exit 1
}
[[ ! -e $output ]] || {
	echo "package-x11: output already exists: $output" >&2
	exit 1
}

declare -a queue=()
declare -A seen=()

add_rel()
{
	local rel=$1

	# An absolute symlink in the target tree (musl's loader is the notable
	# example) is intentionally broken when inspected under the Buildroot
	# prefix, even though its referent exists inside that same target tree.
	[[ $rel == /* && ( -e $target$rel || -L $target$rel ) ]] || {
		echo "package-x11: missing closure member: $rel" >&2
		exit 1
	}
	if [[ ! ${seen[$rel]+present} ]]; then
		seen[$rel]=1
		queue+=("$rel")
	fi
}

copy_link_chain()
{
	local rel=$1
	local src=$target$rel
	local link next current_rel rewritten

	while [[ -L $src ]]; do
		current_rel=${src#"$target"}
		mkdir -p "$stage$(dirname "$current_rel")"
		link=$(readlink "$src")
		if [[ $link == /* ]]; then
			# The tree is relocated from / to /opt/x11 at runtime.  Rewrite
			# root-absolute links (notably musl's loader -> /lib/libc.so)
			# so they stay inside the mounted image.
			rewritten=$(realpath -m \
				--relative-to="$(dirname "$current_rel")" "$link")
			ln -s "$rewritten" "$stage$current_rel"
			next=$target$link
		else
			cp -a "$src" "$stage$current_rel"
			next=$(dirname "$src")/$link
		fi
		# Normalize dot components without dereferencing the next symlink.  A
		# few SONAME aliases (notably libXaw.so.7 -> libXaw7.so.7 -> the real
		# file) have more than one hop, and every hop must be copied.
		src=$(realpath -ms "$next")
		[[ $src == "$target"/* && -e $src ]] || {
			echo "package-x11: unsafe or broken link for $rel" >&2
			exit 1
		}
	done

	current_rel=${src#"$target"}
	mkdir -p "$stage$(dirname "$current_rel")"
	cp -a "$src" "$stage$current_rel"
}

add_rel /usr/bin/Xfbdev
add_rel /usr/bin/twm
add_rel /usr/bin/xkbcomp
add_rel /usr/bin/xterm

for ((index = 0; index < ${#queue[@]}; index++)); do
	rel=${queue[index]}
	src=$target$rel
	# Resolve symlinks as though BUILDROOT_TARGET were /.  Following an
	# absolute target-tree link directly would otherwise escape to the build
	# container's own root (and makes readelf fail on musl's loader link).
	while [[ -L $src ]]; do
		link=$(readlink "$src")
		if [[ $link == /* ]]; then
			src=$target$link
		else
			src=$(readlink -m "$(dirname "$src")/$link")
		fi
	done
	[[ $src == "$target"/* && -e $src ]] || {
		echo "package-x11: unsafe or broken ELF link for $rel" >&2
		exit 1
	}

	while IFS= read -r needed; do
		found=
		for libdir in /lib /usr/lib; do
			if [[ -e $target$libdir/$needed || -L $target$libdir/$needed ]]; then
				found=$libdir/$needed
				break
			fi
		done
		[[ -n $found ]] || {
			echo "package-x11: $rel needs missing library $needed" >&2
			exit 1
		}
		add_rel "$found"
	done < <(
		"$readelf" -d "$src" 2>/dev/null |
			sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'
	)

	interp=$(
		"$readelf" -l "$src" 2>/dev/null |
			sed -n 's@.*Requesting program interpreter: \(.*\)]@\1@p'
	)
	[[ -z $interp ]] || add_rel "$interp"
done

mkdir -p "$stage"
for rel in "${queue[@]}"; do
	copy_link_chain "$rel"
done

ln -s Xfbdev "$stage/usr/bin/X"
mkdir -p "$stage/usr/share/X11/xkb" "$stage/etc/X11"

# Xfbdev is fixed to evdev/pc105/us on this appliance.  This is the complete
# recursive include closure for that keymap; compiling and decompiling it with
# xkbcomp yields the same keymap as the full 3.5 MiB xkeyboard-config tree.
xkb_files=(
	rules/evdev
	keycodes/evdev keycodes/aliases
	types/complete types/basic types/mousekeys types/pc types/iso9995
	types/level5 types/extra types/numpad
	compat/complete compat/basic compat/iso9995 compat/mousekeys
	compat/accessx compat/misc compat/xfree86 compat/level5 compat/caps
	compat/ledcaps compat/lednum compat/ledscroll
	symbols/pc symbols/srvr_ctrl symbols/keypad symbols/altwin symbols/us
	symbols/inet
	geometry/pc
)
for rel in "${xkb_files[@]}"; do
	src=$target/usr/share/X11/xkb/$rel
	[[ -f $src ]] || {
		echo "package-x11: missing XKB closure member: $rel" >&2
		exit 1
	}
	mkdir -p "$stage/usr/share/X11/xkb/$(dirname "$rel")"
	cp -a "$src" "$stage/usr/share/X11/xkb/$rel"
done

# Retain Xlib's small C-locale/error/color databases at their compiled path.
cp -a "$target/usr/share/X11/locale" "$stage/usr/share/X11/"
cp -a "$target/usr/share/X11/XErrorDB" "$stage/usr/share/X11/"
cp -a "$target/usr/share/X11/Xcms.txt" "$stage/usr/share/X11/"

# Xterm links against ncurses to select a valid TERM value.  Keep only the
# single entry used by this appliance; startx points TERMINFO at this relocated
# directory so no copy or symlink outside /opt/x11 is needed.
xterm_terminfo=$target/usr/share/terminfo/x/xterm
[[ -f $xterm_terminfo ]] || {
	echo "package-x11: missing xterm terminfo entry: $xterm_terminfo" >&2
	exit 1
}
mkdir -p "$stage/usr/share/terminfo/x"
cp -a "$xterm_terminfo" "$stage/usr/share/terminfo/x/xterm"

cp -a "$config_root/etc/X11/twmrc" "$stage/etc/X11/twmrc"

expected_interp=
for binary in Xfbdev twm xkbcomp xterm; do
	file=$stage/usr/bin/$binary
	header=$("$readelf" -h "$file")
	grep -q 'Class:.*ELF32' <<<"$header"
	grep -q 'Data:.*big endian' <<<"$header"
	grep -Eq 'Machine:.*(Renesas|SuperH)' <<<"$header"
	interp=$(
		"$readelf" -l "$file" |
			sed -n 's@.*Requesting program interpreter: \(.*\)]@\1@p'
	)
	[[ $interp == /lib/ld-musl-*.so.1 && -e $stage$interp ]]
	if [[ -z $expected_interp ]]; then
		expected_interp=$interp
	else
		[[ $interp == "$expected_interp" ]]
	fi
done

for rel in "${queue[@]}"; do
	file=$stage$rel
	if "$readelf" -d "$file" 2>/dev/null | grep -q '(TEXTREL)'; then
		echo "package-x11: unexpected TEXTREL in $rel" >&2
		exit 1
	fi
	while IFS= read -r needed; do
		[[ -e $stage/lib/$needed || -e $stage/usr/lib/$needed ]] || {
			echo "package-x11: $rel still needs $needed" >&2
			exit 1
		}
	done < <(
		"$readelf" -d "$file" 2>/dev/null |
			sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p'
	)
done

printf 'X11 ELF closure (%d files):\n' "${#queue[@]}"
printf '  %s\n' "${queue[@]}"
du -sh "$stage"

mksquashfs "$stage" "$output" \
	-noappend -all-root -comp xz -b 65536 -Xdict-size 65536 \
	-always-use-fragments -no-xattrs -no-exports -no-progress \
	-mkfs-time 0 -all-time 0

image_size=$(stat -c %s "$output")
image_capacity=$((0x350000))
if ((image_size > image_capacity)); then
	echo "package-x11: image is $image_size bytes; 0x350000-byte payload slot permits $image_capacity" >&2
	exit 1
fi

unsquashfs -s "$output"
sha256sum "$output"

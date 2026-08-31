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
cc=$host/bin/sh4aeb-buildroot-linux-musl-gcc
strip=$host/bin/sh4aeb-buildroot-linux-musl-strip
config_root=${X11_CONFIG_ROOT:-/work/x11-root}
repo=${X11_SOURCE_ROOT:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}
apps_root=$repo/x11/apps
assets_root=$repo/x11/assets
awesome_source=${AWESOME_SOURCE:-$repo/build/awesome-v1.3-src}
w3m_source=${W3M_SOURCE:-$repo/build/w3m-src}
w3m_build=${W3M_BUILD:-$(dirname "$stage")/w3m-build}
w3m_jobs=${W3M_JOBS:-2}
sysroot=$host/sh4aeb-buildroot-linux-musl/sysroot
wallpaper=$assets_root/custom-wallpaper-528x320.rgb565
wallpaper_sha256=007623bb0f215d2e6ce893a81a86e0c76ff13e0214256f1c48614e035f6a5f0e

[[ -d $target && -x $readelf && -x $cc && -x $strip ]]
[[ -d $apps_root && -f $awesome_source/Makefile && -x $w3m_source/configure ]] || {
	echo "package-x11: helper, Awesome, or w3m source is not prepared" >&2
	exit 1
}
[[ -f $wallpaper && $(stat -c %s "$wallpaper") -eq 337920 &&
	$(sha256sum "$wallpaper" | awk '{print $1}') == "$wallpaper_sha256" ]] || {
	echo "package-x11: custom wallpaper is missing or corrupt" >&2
	exit 1
}
[[ ! -e $stage ]] || {
	echo "package-x11: staging path already exists: $stage" >&2
	exit 1
}
[[ ! -e $output ]] || {
	echo "package-x11: output already exists: $output" >&2
	exit 1
}

build_exword_programs()
{
	local pkg_config=$host/bin/pkg-config
	local common_cflags
	local pkg_env

	[[ -x $pkg_config ]] || {
		echo "package-x11: target pkg-config is missing" >&2
		exit 1
	}
	mkdir -p "$target/usr/bin"
	common_cflags="-std=gnu99 -Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables"
	pkg_env=(
		env
		"PATH=$host/bin:$PATH"
		"PKG_CONFIG=$pkg_config"
		"PKG_CONFIG_SYSROOT_DIR=$sysroot"
		"PKG_CONFIG_LIBDIR=$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"
	)

	# Awesome 1.3 is intentionally rebuilt from the pinned, locally patched
	# checkout.  -B prevents stale objects in an incremental cache from hiding
	# a source/configuration change.
	(
		cd "$awesome_source"
		"${pkg_env[@]}" make -B awesome CC="$cc"
	)
	cp -p "$awesome_source/awesome" "$target/usr/bin/awesome"

	"$cc" $common_cflags -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/exdesk" "$apps_root/exdesk.c" \
		$("${pkg_env[@]}" "$pkg_config" --cflags --libs x11)
	"$cc" $common_cflags -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/snake" "$apps_root/snake.c" \
		$("${pkg_env[@]}" "$pkg_config" --cflags --libs x11)
	"$cc" $common_cflags -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/holostatus" "$apps_root/holostatus.c"
	"$cc" $common_cflags -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/file" "$apps_root/file-lite.c"
	"$cc" $common_cflags -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/subpad-mouse" "$apps_root/subpad-mouse.c" \
		$("${pkg_env[@]}" "$pkg_config" --cflags --libs xcb xcb-xtest)
	"$cc" $common_cflags -static -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/Xfbdev.gate" "$apps_root/x11-start-gate.c"
	"$cc" $common_cflags -static -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/sublcd-test" "$apps_root/sublcd-test.c"
	"$cc" $common_cflags -static -Wl,--gc-sections,-z,noexecstack \
		-o "$target/usr/bin/touchdiag" "$apps_root/touchdiag.c"

	"$strip" "$target/usr/bin/awesome" \
		"$target/usr/bin/exdesk" \
		"$target/usr/bin/snake" \
		"$target/usr/bin/holostatus" \
		"$target/usr/bin/file" \
		"$target/usr/bin/subpad-mouse" \
		"$target/usr/bin/Xfbdev.gate" \
		"$target/usr/bin/sublcd-test" \
		"$target/usr/bin/touchdiag"
}

build_exword_programs
"$repo/x11/build-w3m.sh" \
	"$w3m_source" "$w3m_build" "$target" "$host" "$w3m_jobs"

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
add_rel /usr/bin/xkbcomp
add_rel /usr/bin/xterm
add_rel /usr/bin/xcalc
add_rel /usr/bin/xedit
add_rel /usr/bin/xmessage
add_rel /usr/bin/xkill
add_rel /usr/bin/nano
add_rel /usr/bin/less
add_rel /usr/bin/bc
add_rel /usr/bin/file
add_rel /usr/bin/w3m
add_rel /usr/bin/awesome
add_rel /usr/bin/exdesk
add_rel /usr/bin/snake
add_rel /usr/bin/holostatus
add_rel /usr/bin/subpad-mouse
add_rel /usr/bin/Xfbdev.gate
add_rel /usr/bin/sublcd-test
add_rel /usr/bin/touchdiag

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
mkdir -p "$stage/usr/share/awesome"
install -m 0644 "$wallpaper" \
	"$stage/usr/share/awesome/holo-wallpaper-528x320.rgb565"
install -D -m 0644 "$repo/x11/w3m/COPYING" \
	"$stage/usr/share/licenses/w3m/COPYING"

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
# The full inet table is almost entirely laptop multimedia keys and costs
# precious low-memory SquashFS space.  The EX-word and a standard USB HID
# keyboard use the normal pc/us symbols; retain an empty evdev include so the
# stock rules file still compiles cleanly.
install -m 0644 "$config_root/usr/share/X11/xkb/symbols/inet" \
	"$stage/usr/share/X11/xkb/symbols/inet"

# Retain Xlib's small C-locale/error/color databases at their compiled path.
cp -a "$target/usr/share/X11/locale" "$stage/usr/share/X11/"
# XErrorDB only expands diagnostic error numbers into prose.  Omitting it
# preserves normal X operation and saves enough space for 128 KiB blocks.
cp -a "$target/usr/share/X11/Xcms.txt" "$stage/usr/share/X11/"

# Athena applications keep their layouts in app-default files.  rcS exposes
# the relocated X11 data directory at /usr/share/X11, so these retain their
# normal compiled path without per-application environment hacks.
mkdir -p "$stage/usr/share/X11/app-defaults"
for resource in XCalc Xedit Xmessage Xmessage-color; do
	if [[ -f $target/usr/share/X11/app-defaults/$resource ]]; then
		cp -a "$target/usr/share/X11/app-defaults/$resource" \
			"$stage/usr/share/X11/app-defaults/$resource"
	fi
done
for resource in XCalc Xedit Xmessage; do
	[[ -f $stage/usr/share/X11/app-defaults/$resource ]] || {
		echo "package-x11: missing app-default resource: $resource" >&2
		exit 1
	}
done
if [[ -d $target/usr/share/X11/xedit ]]; then
	cp -a "$target/usr/share/X11/xedit" "$stage/usr/share/X11/"
fi

# Xterm, nano, and less use ncurses.  Keep the entries needed both under X and
# on the Linux framebuffer console; the tiny wrappers below point TERMINFO at
# this relocated directory.
for term in xterm linux; do
	first=${term:0:1}
	hex=$(printf '%02x' "'$first")
	terminfo=$target/usr/share/terminfo/$first/$term
	[[ -f $terminfo ]] || terminfo=$target/usr/share/terminfo/$hex/$term
	[[ -f $terminfo ]] || {
		echo "package-x11: missing $term terminfo entry" >&2
		exit 1
	}
	mkdir -p "$stage/usr/share/terminfo/$first"
	cp -a "$terminfo" "$stage/usr/share/terminfo/$first/$term"
done

expected_interp=
for binary in Xfbdev xkbcomp xterm xcalc xedit xmessage xkill \
	nano less bc file w3m awesome exdesk snake holostatus subpad-mouse; do
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

for binary in Xfbdev.gate sublcd-test touchdiag; do
	file=$stage/usr/bin/$binary
	header=$("$readelf" -h "$file")
	grep -q 'Class:.*ELF32' <<<"$header"
	grep -q 'Data:.*big endian' <<<"$header"
	grep -Eq 'Machine:.*(Renesas|SuperH)' <<<"$header"
	! "$readelf" -l "$file" | grep -q 'Requesting program interpreter'
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

# Preserve the Buildroot binaries behind small launch/supervision wrappers.
# startx still invokes the historical `twm` name so old rootfs payloads remain
# compatible; in this image it resolves to the Awesome supervisor.
mv "$stage/usr/bin/Xfbdev" "$stage/usr/bin/Xfbdev.real"
mv "$stage/usr/bin/xterm" "$stage/usr/bin/xterm.real"
mv "$stage/usr/bin/nano" "$stage/usr/bin/nano.real"
mv "$stage/usr/bin/less" "$stage/usr/bin/less.real"
mv "$stage/usr/bin/w3m" "$stage/usr/bin/w3m.real"
install -m 0755 "$config_root/usr/bin/Xfbdev" "$stage/usr/bin/Xfbdev"
install -m 0755 "$config_root/usr/bin/subpad-awesome" \
	"$stage/usr/bin/subpad-awesome"
install -m 0755 "$config_root/usr/bin/xterm" "$stage/usr/bin/xterm"
install -m 0755 "$config_root/usr/bin/nano" "$stage/usr/bin/nano"
install -m 0755 "$config_root/usr/bin/less" "$stage/usr/bin/less"
install -m 0755 "$config_root/usr/bin/w3m" "$stage/usr/bin/w3m"
install -m 0755 "$config_root/usr/bin/screenfetch" \
	"$stage/usr/bin/screenfetch"
install -m 0755 "$config_root/usr/bin/notify" "$stage/usr/bin/notify"
install -m 0644 "$config_root/etc/X11/twmrc" "$stage/etc/X11/twmrc"

rm -f "$stage/usr/bin/X" "$stage/usr/bin/twm" \
	"$stage/usr/bin/exmenu" "$stage/usr/bin/exfile" \
	"$stage/usr/bin/xclock" "$stage/usr/bin/xeyes"
ln -s Xfbdev "$stage/usr/bin/X"
ln -s subpad-awesome "$stage/usr/bin/twm"
ln -s exdesk "$stage/usr/bin/exmenu"
ln -s exdesk "$stage/usr/bin/exfile"
ln -s exdesk "$stage/usr/bin/xclock"
ln -s exdesk "$stage/usr/bin/xeyes"

for script in Xfbdev subpad-awesome xterm nano less w3m screenfetch notify; do
	sh -n "$stage/usr/bin/$script"
done
[[ $(readlink "$stage/usr/bin/X") == Xfbdev ]]
[[ $(readlink "$stage/usr/bin/twm") == subpad-awesome ]]
for alias in exmenu exfile xclock xeyes; do
	[[ $(readlink "$stage/usr/bin/$alias") == exdesk ]]
done

for binary in Xfbdev.real awesome exdesk snake holostatus subpad-mouse xkbcomp \
	xterm.real xcalc xedit xmessage xkill nano.real less.real bc file; do
	# w3m is checked separately below only to keep this long list readable.
	file=$stage/usr/bin/$binary
	interp=$(
		"$readelf" -l "$file" |
			sed -n 's@.*Requesting program interpreter: \(.*\)]@\1@p'
	)
	[[ $interp == "$expected_interp" && -e $stage$interp ]]
done

file=$stage/usr/bin/w3m.real
interp=$(
	"$readelf" -l "$file" |
		sed -n 's@.*Requesting program interpreter: \(.*\)]@\1@p'
)
[[ $interp == "$expected_interp" && -e $stage$interp ]]

for required in \
	usr/bin/Xfbdev usr/bin/Xfbdev.gate usr/bin/Xfbdev.real \
	usr/bin/awesome usr/bin/subpad-awesome usr/bin/subpad-mouse \
	usr/bin/exdesk usr/bin/exmenu usr/bin/exfile usr/bin/xclock usr/bin/xeyes \
	usr/bin/snake usr/bin/holostatus usr/bin/notify \
	usr/bin/xterm usr/bin/xterm.real usr/bin/screenfetch \
	usr/bin/xcalc usr/bin/xedit usr/bin/xmessage usr/bin/xkill \
	usr/bin/nano usr/bin/nano.real usr/bin/less usr/bin/less.real \
	usr/bin/bc usr/bin/file \
	usr/bin/w3m usr/bin/w3m.real \
	usr/share/licenses/w3m/COPYING \
	usr/bin/sublcd-test usr/bin/touchdiag etc/X11/twmrc \
	usr/share/X11/app-defaults/XCalc usr/share/X11/app-defaults/Xedit \
	usr/share/X11/app-defaults/Xmessage \
	usr/share/X11/app-defaults/Xmessage-color usr/share/terminfo/l/linux \
	usr/share/awesome/holo-wallpaper-528x320.rgb565
do
	[[ -e $stage/$required || -L $stage/$required ]] || {
		echo "package-x11: final image is missing $required" >&2
		exit 1
	}
done

printf 'X11 ELF closure (%d files):\n' "${#queue[@]}"
printf '  %s\n' "${queue[@]}"
du -sh "$stage"

mksquashfs "$stage" "$output" \
	-noappend -all-root -comp xz -b 131072 -Xdict-size 131072 \
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

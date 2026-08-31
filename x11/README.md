# Rebuilding the X11 image

The device runs a deliberately small, read-only v9 X11 userspace containing
X.Org `Xfbdev` 1.19.7, patched AwesomeWM 1.3, patched `xterm`, `xkbcomp`,
their runtime library closure, one keymap, and one terminfo entry. Buildroot
is only a build tool; its complete target filesystem is not placed in the
payload.

The desktop is intentionally small:

- one workspace named `Desktop`;
- a flat, dark Holo-style bottom bar with one menu button and no workspace or
  quick-launch buttons;
- the `exdesk` binary exposed as `exmenu`, `exfile`, `xclock`, and `xeyes`,
  using the same lightweight Holo palette;
- the selected desktop wallpaper, preconverted to the display's native 528x320
  big-endian RGB565 format;
- terminal-backed Task Manager, System Information, and Disk Information;
- XCalc, Xedit, Xmessage, and Xkill in the desktop menu;
- compact `nano`, `less`, `bc`, and `file` console utilities;
- text-only `w3m` for local HTML and plain HTTP (no TLS or images);
- a small native Holo-styled Snake game;
- a live RAM/swap/SD/clock bar with transient file-backed notifications;
- a text-only screenfetch-style summary; and
- a six-button mouse pad drawn on the XD-B8600 secondary display.

## Requirements

- Git
- Docker with Linux containers enabled
- A POSIX shell
- Roughly 5 GiB of free Docker storage for the Buildroot output and downloads
- Internet access for the pinned Buildroot checkout and upstream packages

Run the build as an ordinary user from the repository root:

```sh
./scripts/build-x11.sh
```

`JOBS` controls parallelism and defaults to 2.  For example:

```sh
JOBS=8 ./scripts/build-x11.sh
```

The script performs the whole path from a clean clone:

1. clones Buildroot tag `2019.02.11` and verifies commit
   `5a6d31c87e1573bc83986471c194b944d7a365b7`;
2. clones AwesomeWM at commit
   `d4f1b99c93c7da10af774500f3c007e77a765c5d`, verifies it, and applies the
   source-controlled AwesomeWM 1.3 patch;
3. fetches and verifies w3m commit
   `ee66aabc3987000c2851bce6ade4dcbb0b037d81`;
4. copies the source-controlled Buildroot compatibility, Xfbdev, and xterm
   patches into the Buildroot tree;
5. installs the exact defconfig in an out-of-tree Docker volume;
6. downloads packages from the official Buildroot source mirror first (with
   Buildroot's recorded hashes), then builds the cross toolchain, X packages,
   patched AwesomeWM, and EX-word GUI helpers in the supplied Ubuntu 22.04
   container; and
7. installs the checked Holo wallpaper asset, computes the ELF/library
   closure, and creates `build/x11-xterm.sqfs`.

The custom programs include `exdesk`, the timerless Xfbdev startup gate,
secondary-LCD and touch diagnostics, Snake, the status producer, and the
supervised touch-to-X pointer helper. The packager also installs the Xfbdev,
AwesomeWM, notification, and one-instance
xterm wrappers plus the required symlinks.

The final packager rejects the image unless its target programs are ELF32,
big-endian SuperH executables with the expected static or musl-dynamic
linkage. It also rejects missing libraries, unexpected text relocations, and
images larger than the payload's `0x350000`-byte X11 slot.

The image uses a 128 KiB SquashFS/XZ block. A focused libc-only `file`
implementation and reduced optional X diagnostic/color/multimedia-key data
keep w3m and its conservative garbage collector inside the fixed payload
memory map without the severe startup latency of 512 KiB blocks. Avoid
running w3m and X applications together when memory is low.

To combine the rebuilt image with the rebuilt kernel:

```sh
./scripts/build-payload.sh \
  build/kernel/arch/sh/boot/zImage build/x11-xterm.sqfs
```

The generated payload is `build/LINUX.PAY`. Build paths and timestamps in
old upstream packages mean a clean rebuild is not promised to be byte-for-byte
identical to the checked-in artifact. The format, architecture, dependency,
and size checks are the compatibility guarantees.

All generated Buildroot files, downloaded archives, and the temporary X11
staging tree live in a named Docker volume on a case-sensitive Linux
filesystem. This is required on macOS: Docker bind mounts inherit the host
filesystem's case folding, which changes ncurses terminfo paths and can affect
other older build systems. Only the finished `build/x11-xterm.sqfs` is copied
back into the checkout.

The script prints the volume name when it finishes.  It reuses a stable volume
for the same checkout and container architecture, making later builds much
faster.  Set `EXWORD_BUILDROOT_VOLUME` to select a different cache or start a
completely clean build.

The container uses the host's native Docker architecture. This has been used
on both arm64 macOS/Docker Desktop and amd64 Linux-style containers.  Set
Docker's standard `DOCKER_DEFAULT_PLATFORM` variable if the local installation
needs an explicit platform override.

## Runtime design and controls

Run `startx` from the text console. Xfbdev starts first, the secondary LCD is
drawn once, the touch helper is placed under the window-manager wrapper's
supervision, and AwesomeWM then opens the single desktop. Startup is slow on
16 MiB of RAM; do not repeatedly launch programs while the bar is appearing.

Click the menu button at the bottom-left to open File Manager, Calculator,
Text Editor, Message Box, Kill Window, Command Prompt, Task Manager, System
Information, Disk Information, Snake, Clock, or Eyes. The menu
also works with Up, Down, Enter, and Escape. AwesomeWM's task bar deliberately
has no workspace or application-launch buttons beyond the menu.

This is a visual adaptation of the Holo theme from awesome-copycats commit
`affb71fa9ea69460208590f90383b3b0e8bab9f0`, not an installation of its
modern AwesomeWM configuration. No Lua, `lain`, Cairo, Pango, PNG decoder, or
wallpaper daemon is added to the target. The patched AwesomeWM 1.3 loads the
preconverted wallpaper directly into one root pixmap in small strips and
falls back to the solid Holo background if the geometry, pixel format, file,
or allocation is unsuitable. Flat drawing, cached bar geometry, fewer X
round trips, a core bitmap font, and one-instance xterm supervision keep the
desktop practical within 16 MiB.

The Holo visual source and wallpaper derivative are CC BY-SA 4.0. Attribution,
hashes, and the exact conversion are recorded in
[`assets/README.md`](assets/README.md).

Only one xterm may run at once. The wrapper uses an atomic lock under `/tmp`,
discards the old xterm's repeated shutdown diagnostics, and records the final
status in `/tmp/xterm.log`. If a terminal-backed Start item does not open,
close the existing xterm first. This protects the machine from a rapid series
of terminal launches exhausting RAM and PTYs.

The secondary display shows six regions:

| Top row | Bottom row |
| --- | --- |
| Left, Up, Right | Left click, Down, Right click |

The touch helper converts those regions into XTEST pointer events. The Power
key independently toggles the keyboard pointer mode; in that mode arrows move
and Enter/Zoom/Back click left/middle/right. Turn keyboard pointer mode off
and press Back+Q to leave AwesomeWM.

Run `w3m file:///path/to/page.html` for local pages, or `w3m http://host/`
once networking exists. This build intentionally omits TLS, images, mouse
support, cookies, history, and internationalization to fit the 16 MiB system.
It cannot open `https://` URLs. Press `q`, then confirm, to quit. Running w3m
from the text console after leaving X provides the most free memory.

## Secondary-hardware safety

The secondary LCD and resistive touch panel are a userspace implementation,
not a generic Linux display/input driver. The helpers reuse controller state
left by Casio firmware and access the measured XD-B8600 LCD, ADC, clock, and
PFC addresses through `/dev/mem`. They require root, check the main
framebuffer identity and geometry, serialize access with a lock, and restore
the touch hardware when X exits.

This path is for the XD-B8600 only. Do not run it on another EX-word model.
Do not combine it with the abandoned experimental kernel `subpad_mode`
interface: the userspace helper deliberately refuses to run when that
interface exists. Run the raw `sublcd-test` and `touchdiag` operations only
from the text console, never from xterm/X, and only when following a specific
hardware-diagnostic procedure.

## Separate prepare step

For inspection or configuration testing without starting the long build:

```sh
./scripts/prepare-buildroot.sh
./scripts/prepare-awesome.sh
./scripts/prepare-w3m.sh
```

Those commands produce `build/buildroot-2019.02.11`,
`build/awesome-v1.3-src`, and `build/w3m-src`. The defconfig remains at
`x11/buildroot-2019-exword.defconfig`; `build-x11.sh` copies it into the named
volume. Running either prepare step again is safe when its tree is still based
on the pinned commit; each helper verifies the revision before applying the
version-controlled changes.

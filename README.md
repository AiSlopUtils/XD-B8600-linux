# Linux on the Casio EX-word XD-B8600

An experimental Linux 6.1 port for the Casio EX-word XD-B8600 (DATAPLUS 6):
Renesas SH7724/SH-4A, big-endian, 16 MiB RAM, and a 528x320 RGB565 main LCD.

> [!CAUTION]
> This is hardware-specific research software. It has only been developed on
> an XD-B8600. Do not assume that addresses, clocks, displays, or storage are
> compatible with another EX-word model. The current recovery payload keeps
> removable storage deliberately unbound. Boot with the SD card removed and
> do not manually bind the SDHI driver or run `sdswap prepare erase`.

## Current status (2026-08-28)

Confirmed on hardware:

- Linux 6.1 boots from a Casio add-in without replacing the Casio firmware.
- Main 528x320 framebuffer and accelerated console scrolling.
- EX-word keyboard input, a software Ctrl key, and keypad-driven pointer.
- BusyBox shell and low-memory utilities.
- X.Org Xfbdev and `twm`.

Integrated in the latest recovery candidate:

- `xterm`, `screenfetch`, BusyBox `fdisk`, `blkid`, `blockdev`, `partprobe`,
  and a small `lsblk` implementation.
- A guarded SD swap helper. It performs no write unless the exact command
  `sdswap prepare erase` is used.
- Read-only `sdhwinfo` diagnostics.

The latest candidate was uploaded and read back byte-for-byte over USB. Its
post-SDHI-fix boot had not yet been confirmed when this repository was
archived. The secondary keyboard display is unsupported and disabled.

## Clean-clone quick start

The repository now includes top-level build targets, dependency checks, pinned
download scripts, Docker recipes, and install/readback instructions:

```sh
git clone https://github.com/AiSlopUtils/XD-B8600-linux.git
cd XD-B8600-linux
make verify       # offline artifact and source checks
make doctor       # Docker/full-build prerequisite check
make ready        # native USB client + known-good loader package
make payload      # rebuild Linux using supplied BusyBox and X image
JOBS=4 make all   # complete pinned payload and loader build
```

`make all` produces `build/LINUX-from-source.PAY` and installable LNX03
packages under `build/exword-data`. It downloads checksum/revision-pinned
upstream inputs and needs Docker, internet access, and about 10 GiB of free
space. The host USB client has small native Homebrew or Debian dependencies.
See [docs/BUILDING.md](docs/BUILDING.md) for setup and
[docs/INSTALLING.md](docs/INSTALLING.md) for safe device installation.

## Why SD support is disabled

Linux currently reconstructs the inherited SH7724 clock tree as 0 Hz. Earlier
SDHI code consequently entered an infinite clock-divider loop during boot.
The current kernel registers SDHI1 with a nonmatching `driver_override`, so it
cannot probe automatically, and also rejects a zero-rate SDHI clock.

Run `sdhwinfo` only after reaching the shell. It reads the inherited PFC and
clock-generator state without touching either storage controller. Clock,
PTW pin-mux, card power, and card-detect behavior must be validated before
enabling SDHI or MMCIF. See [docs/SDHI-STATUS.md](docs/SDHI-STATUS.md).

## Booting the supplied payload

The loader searches for `linux.pay`/`LINUX.PAY` in `CASIOTXT` and at the root
of internal storage, then on the SD card. The recommended route is internal
storage with the SD card removed.

Build the included patched `tools/libexword` command-line client with
`make usb-tool`, create a separate readback directory, and start the client:

```sh
mkdir -p build/readback
./scripts/run-libexword.sh
```

Then use:

```text
connect text ja
send artifacts/LINUX.PAY
get build/readback/LINUX.PAY
disconnect
exit
```

Compare `build/readback/LINUX.PAY` with
[artifacts/LINUX.PAY](artifacts/LINUX.PAY), exit USB mode, and launch the
LNX03 add-in. Release the keys, press Enter to inspect the payload, press
Enter once more to arm it, then press Enter at the ARMED screen to boot.
The exact artifact hashes are in
[artifacts/SHA256SUMS](artifacts/SHA256SUMS).
Use a separate readback directory so `get` cannot overwrite the source
artifact; the exact commands are in [docs/INSTALLING.md](docs/INSTALLING.md).

The loader never writes NOR flash. Linux uses a RAM initramfs; the X image is
staged in reserved RAM and mounted read-only as SquashFS.

## Controls

Console key map:

| EX-word key | Linux input |
| --- | --- |
| SYMBOL | Space |
| BACK | Ctrl |
| F1-F8 | 1-8 |
| Page Up / Page Down | 9 / 0 |
| History / Zoom / Jump / Audio | `-` / `=` / `/` / `.` |
| Power | Toggle pointer mode |

In pointer mode, arrows move the cursor and Enter/Zoom/Back click the
left/middle/right mouse buttons. Type `startx` to start Xfbdev and twm. Quit
from the twm root menu or turn pointer mode off and press Back+Q.

## Repository layout

- `kernel/overlay/` — the complete 16-file Linux 6.1 EX-word port overlay.
- `kernel/exword_defconfig` — exact configuration used for the latest kernel.
- `rootfs/` — exact BusyBox configuration and custom initramfs overlay.
- `x11/` — Buildroot pin, overlay, Xfbdev patches, twm config, and packager.
- `loader/` — source for the LNX03 Casio add-in payload loader.
- `payload/` — test payloads and the EXWPAY1 packer.
- `tools/libexword/` — pinned upstream transfer client with the local fixes.
- `experiments/` — early hello-world, hardware-info, and init work.
- `artifacts/` — final readback-verified payload and useful milestones.
- `scripts/` and the top-level `Makefile` — clean-clone preparation, build,
  validation, loader staging, and host-transfer entry points.

The original 4.5 GiB work directory contained downloaded upstream trees and
generated output. Those caches are intentionally not committed. Exact
versions, hashes, and upstream locations are recorded in
[docs/SOURCES.md](docs/SOURCES.md), and build instructions are in
[docs/BUILDING.md](docs/BUILDING.md).

## Known limitations

- SD, persistent storage, swap on SD, networking, and audio are not operational.
- The SH clock model and timer calibration remain provisional.
- Only 16 MiB of physical RAM is available; X applications can trigger OOM.
- `top` uses manual Space/Enter refresh because timed refresh is unreliable.
- There is BusyBox `fdisk`, not full ncurses `cfdisk`.
- The secondary keyboard display is disabled.

## Memory layout

| Region | Address / size |
| --- | --- |
| Payload load and entry | P2 `0xac800000` (physical `0x0c800000`) |
| Main framebuffer | physical `0x0c200000`, `0x53000` bytes |
| X11 SquashFS start | payload offset `0x1b0000`, physical `0x0c9b0000` |
| X11 slot | `0x350000` bytes, ending at physical `0x0cd00000` |
| Compressed kernel relocation | physical `0x0cd00000` |
| End of RAM | physical `0x0d000000` |

## Licensing

This repository combines original work with modified GPL and other upstream
projects. Copyright and SPDX notices in individual files remain authoritative.
See [THIRD_PARTY.md](THIRD_PARTY.md) before redistributing binaries.

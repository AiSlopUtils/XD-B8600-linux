# Building

The checked-in artifacts are provided so hardware work does not depend on
rebuilding the entire stack. The scripts below recreate the kernel/rootfs and
payload from pinned inputs. Docker is used because the SH-4 cross compiler is
not normally available on macOS.

## Kernel and initramfs

Requirements: Docker, `curl`, `tar`, Python 3, and a POSIX shell.

```sh
./scripts/prepare-linux.sh
./scripts/assemble-rootfs.sh
./scripts/build-kernel.sh
./scripts/build-payload.sh
```

`prepare-linux.sh` verifies the official Linux 6.1 archive and copies the
complete EX-word overlay into `build/linux-6.1`. `assemble-rootfs.sh` uses the
checked-in, verified static BusyBox binary plus the source-controlled overlay.
`build-kernel.sh` mounts the repository at `/work/linux-work`, matching the
paths embedded in the original build configuration.

The resulting files are:

```text
build/kernel/arch/sh/boot/zImage
build/LINUX.PAY
```

To rebuild BusyBox rather than use the checked-in binary, download BusyBox
1.37.0, verify the hash in `SOURCES.md`, copy
`rootfs/busybox-1.37.0.config` to `.config`, and build statically with
`sh4-linux-gnu-`. `rootfs/configure-busybox-minimal.sh` documents how the
configuration was generated.

## Xfbdev, twm, and xterm

The X userspace was built from Buildroot 2019.02.11 at commit
`5a6d31c87e1573bc83986471c194b944d7a365b7`.

```sh
git clone https://gitlab.com/buildroot.org/buildroot.git build/buildroot
git -C build/buildroot checkout 5a6d31c87e1573bc83986471c194b944d7a365b7
./scripts/apply-buildroot-overlay.sh build/buildroot
cp x11/buildroot-2019-exword.defconfig build/buildroot/.config
```

Build it with the packages from `x11/Dockerfile.buildroot-x11`. Once
Buildroot has populated `output/target` and `output/host`, package the runtime
closure using:

```sh
X11_CONFIG_ROOT="$PWD/x11/config" ./x11/package-x11.sh \
  build/buildroot/output/target build/buildroot/output/host \
  build/x11-stage build/x11-xterm.sqfs
```

The packager verifies that Xfbdev, twm, xkbcomp, and xterm are ELF32 SH
big-endian binaries, follows their library/interpreter closure, keeps only the
needed XKB/terminfo data, builds a deterministic XZ SquashFS, and enforces the
`0x350000`-byte payload slot.

## Payload format

`payload/pack_payload.py` emits a 32-byte big-endian EXWPAY1 header followed by
the zImage, zero padding to offset `0x1b0000`, and the X SquashFS. Load and
entry addresses are both `0xac800000`; the body has a CRC32 checked by LNX03.

Equivalent final command:

```sh
python3 payload/pack_payload.py \
  --append-blob artifacts/x11-xterm.sqfs \
  --append-offset 0x1b0000 --append-capacity 0x350000 \
  build/kernel/arch/sh/boot/zImage build/LINUX.PAY
```

The Ubuntu base image and apt repositories in the Dockerfiles are not pinned
by digest, so the procedure is repeatable but is not claimed to be bit-for-bit
reproducible indefinitely.

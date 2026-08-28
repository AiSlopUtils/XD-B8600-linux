# Building from a clean clone

The repository contains every XD-B8600-specific source file, configuration,
patch, packager, and Docker recipe needed to rebuild the device-side
software. Large upstream source trees and compilers are downloaded on demand
at recorded versions instead of being committed as generated files.

There are two useful build paths:

- `make payload` rebuilds Linux and the initramfs around the supplied,
  verified BusyBox and X11 images. This is the faster recovery/development
  path.
- `make all` rebuilds Xfbdev/twm/xterm, BusyBox, the initramfs, Linux payload,
  and LNX03 application using pinned sources and the checksum-pinned historical
  SDK. This is the clean-clone completeness test; the SDK contains prebuilt
  Casio support libraries and is not itself rebuilt.

## Supported build hosts and prerequisites

The target builds run in supplied Docker containers and have been exercised
from macOS on Apple silicon. An x86-64 Linux host with Docker also works.
Docker Desktop supplies the amd64 emulation needed on Apple silicon. An ARM
Linux host must have x86-64 QEMU/binfmt support enabled because the kernel,
BusyBox, and loader builders use `linux/amd64`; the historical devkitSH4 SDK
contains x86-64 host tools.

Allow at least 10 GiB of free space for Docker images, downloaded sources,
and the Buildroot output. The first complete build downloads and compiles a
cross toolchain, so it takes substantially longer than later incremental
builds.

On macOS:

1. Install and start Docker Desktop.
2. Install Apple's command-line developer tools if `make` and `cc` are not
   already present.
3. Install the optional native USB-client dependencies:

   ```sh
   brew install git python xz pkg-config libusb readline
   ```

On Debian or Ubuntu:

```sh
sudo apt update
sudo apt install \
  bash build-essential ca-certificates curl docker.io git make patch python3 \
  tar xz-utils bzip2 pkg-config libusb-1.0-0-dev libreadline-dev
```

Start Docker and arrange permission to use its socket as your ordinary user
(sign out and back in after changing group membership):

```sh
sudo systemctl enable --now docker
sudo usermod -aG docker "$USER"
```

On ARM Debian/Ubuntu, also enable x86-64 user-mode emulation and verify that
Docker can start an amd64 container:

```sh
sudo apt install qemu-user-static binfmt-support
docker run --rm --platform linux/amd64 ubuntu:22.04 true
```

Buildroot deliberately refuses to run as root. Then check the host:

```sh
make doctor
make verify
```

`make verify` is offline: it checks shell/Python syntax, every hash in
`artifacts/SHA256SUMS`, and a byte-for-byte deterministic repack of the
supplied `LINUX.PAY`.

## Fast kernel/payload rebuild

```sh
JOBS=4 make payload
```

This performs the following operations:

1. downloads Linux 6.1 from kernel.org and verifies its SHA-256;
2. applies all 16 files under `kernel/overlay`;
3. assembles `build/rootfs` with the supplied static BusyBox plus the
   source-controlled rootfs overlay;
4. builds the big-endian SH-4A kernel in `kernel/Dockerfile.sh4`; and
5. combines the zImage with the supplied X11 SquashFS.

The result is `build/LINUX.PAY`. Its kernel was rebuilt, so build metadata can
make its hash differ from `artifacts/LINUX.PAY`; the packer still enforces the
same addresses and size limits.

The equivalent individual commands are:

```sh
./scripts/prepare-linux.sh
./scripts/assemble-rootfs.sh
./scripts/build-kernel.sh
./scripts/build-payload.sh
```

## Complete pinned device-side build

```sh
JOBS=4 make all
```

The dependency order is intentional:

1. `scripts/build-x11.sh` clones pinned Buildroot 2019.02.11, applies the
   checked-in compatibility/Xfbdev overlay, builds the big-endian musl SH-4A
   toolchain and X packages, and writes `build/x11-xterm.sqfs`.
2. `scripts/build-busybox.sh` reconstructs the release toolchain from pinned
   musl 1.2.5, Linux 6.1 userspace headers, Ubuntu's SH compiler, and the
   checksum-pinned devkitSH4 `libgcc`, then builds a static ELF32 big-endian
   BusyBox at `build/busybox`. The result must match `artifacts/busybox`
   byte-for-byte.
3. The source-built BusyBox is assembled into
   `build/rootfs-from-source`, then Linux is built at
   `build/kernel-from-source`.
4. The packer writes `build/LINUX-from-source.PAY` using the source-built
   kernel and X image.
5. `scripts/build-loader.sh` downloads the checksum-pinned Linux/amd64
   devkitSH4 SDK, rebuilds LNX03, requires it to match the supplied loader
   byte-for-byte, and stages installable packages under
   `build/exword-data/{ja,cn}/LNX03`.

Do not substitute Ubuntu's ordinary `sh4-linux-gnu` userspace libraries for
step 2. That compiler can emit big-endian objects, but its packaged libc and
final-link path are little-endian. The wrapper deliberately builds static
musl and imports the SDK's big-endian `libgcc`; its architecture, linkage,
and exact output hash are checked before the rootfs is assembled.

See [../x11/README.md](../x11/README.md),
[../rootfs/README.md](../rootfs/README.md), and
[../loader/README.md](../loader/README.md) for each subsystem's validation
and output details.

## Host USB transfer tool

The host client is separate from `make all` because it links to native macOS
or Linux USB/readline libraries:

```sh
make usb-tool
```

This directly compiles the pinned patched libexword source and writes
`build/libexword/exword`. It deliberately avoids the upstream optional
Python-2/SWIG Autotools path. Start it with:

```sh
./scripts/run-libexword.sh
```

On Linux, install the included udev rule if the client cannot open USB device
`07cf:6101`, then reconnect the dictionary:

```sh
sudo install -m 0644 tools/udev/60-exword.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

`make ready` runs repository verification, builds this host client, and
stages the supplied known-good loader package. Installation and payload
readback instructions are in [INSTALLING.md](INSTALLING.md).

## Payload format and limits

`payload/pack_payload.py` emits a 32-byte, big-endian EXWPAY1 header followed
by the zImage, zero padding to offset `0x1b0000`, and the X SquashFS. Load and
entry addresses are both `0xac800000`; LNX03 validates the body length and
CRC32 before transferring control.

The kernel must end before offset `0x1b0000`. The X image has a fixed
`0x350000`-byte slot ending at physical `0x0cd00000`, where the compressed
kernel relocation region begins. Both build scripts reject an overlap.

## Reproducibility scope

- Supplied artifacts are immutable and hash-verified. Repacking the supplied
  zImage and X image reproduces `artifacts/LINUX.PAY` exactly.
- A clean loader source build is required to reproduce the supplied 7,020-byte
  D01 exactly.
- Kernel and X11 userspace source builds are compatibility-reproducible, not
  promised bit-for-bit across dates or host architectures. Old upstream
  build systems embed build paths, timestamps, or host-tool details.
- Source archives and the devkitSH4 SDK are checksum-pinned. Buildroot is
  pinned to an exact Git commit, tries the official Buildroot source mirror
  first, and verifies its own package downloads.
- Ubuntu base tags and apt repositories in the Dockerfiles are not frozen by
  snapshot date. Build scripts compensate with target architecture, static
  linkage, closure, size, CRC, and (for the loader) exact-artifact checks.

Exact versions, hashes, revisions, and upstream URLs are recorded in
[SOURCES.md](SOURCES.md).

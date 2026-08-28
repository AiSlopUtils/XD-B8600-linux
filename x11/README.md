# Rebuilding the X11 image

The device runs a deliberately small, read-only X11 userspace containing
X.Org `Xfbdev` 1.19.7, `twm`, `xterm`, `xkbcomp`, their runtime library
closure, one keymap, and one terminfo entry.  Buildroot is only a build tool;
its complete target filesystem is not placed in the payload.

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
2. copies the source-controlled compatibility and Xfbdev patches into it;
3. installs the exact defconfig in an out-of-tree Docker volume;
4. downloads packages from the official Buildroot source mirror first (with
   Buildroot's recorded hashes), then builds the cross toolchain and X
   packages in the supplied Ubuntu 22.04 container; and
5. computes the ELF/library closure and creates `build/x11-xterm.sqfs`.

The final packager rejects the image unless all four programs are ELF32,
big-endian SuperH executables using the included musl interpreter.  It also
rejects missing libraries, text relocations, and images larger than the
payload's `0x350000`-byte X11 slot.

To combine the rebuilt image with the rebuilt kernel:

```sh
./scripts/build-payload.sh \
  build/kernel/arch/sh/boot/zImage build/x11-xterm.sqfs
```

The generated payload is `build/LINUX.PAY`.  Build paths and timestamps in
old upstream packages mean a clean rebuild is not promised to be byte-for-byte
identical to the checked-in artifact.  The format, architecture, dependency,
and size checks are the compatibility guarantees.

All generated Buildroot files, downloaded archives, and the temporary X11
staging tree live in a named Docker volume on a case-sensitive Linux
filesystem.  This is required on macOS: Docker bind mounts inherit the host
filesystem's case folding, which changes ncurses terminfo paths and can affect
other older build systems.  Only the finished `build/x11-xterm.sqfs` is copied
back into the checkout.

The script prints the volume name when it finishes.  It reuses a stable volume
for the same checkout and container architecture, making later builds much
faster.  Set `EXWORD_BUILDROOT_VOLUME` to select a different cache or start a
completely clean build.

The container uses the host's native Docker architecture.  This has been used
on both arm64 macOS/Docker Desktop and amd64 Linux-style containers.  Set
Docker's standard `DOCKER_DEFAULT_PLATFORM` variable if the local installation
needs an explicit platform override.

## Separate prepare step

For inspection or configuration testing without starting the long build:

```sh
./scripts/prepare-buildroot.sh
```

That produces `build/buildroot-2019.02.11`.  The defconfig remains at
`x11/buildroot-2019-exword.defconfig`; `build-x11.sh` copies it into the named
volume.  Running the prepare step again is safe when the tree is still based
on the pinned commit; it reapplies the version-controlled overlay.

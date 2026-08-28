# BusyBox initramfs

`overlay/`, `busybox.links`, `initramfs-devices.list`, and
`busybox-1.37.0.config` are the complete source-controlled inputs for the
initramfs.  The normal recovery build uses the verified static binary in
`artifacts/busybox`:

```sh
./scripts/assemble-rootfs.sh
```

To rebuild BusyBox from source:

```sh
./scripts/build-busybox.sh
./scripts/assemble-rootfs.sh build/rootfs-from-source build/busybox
```

The builder downloads BusyBox 1.37.0 and musl 1.2.5, verifies both archives,
installs the pinned Linux 6.1 userspace headers, and prepares the Linux/amd64
devkitSH4 SDK used by the loader.  It combines Ubuntu's SH compiler, static
musl, and the SDK's big-endian `libgcc`, then builds BusyBox with the fixed
source timestamp used by the release.  The result is checked as a static
ELF32 big-endian Renesas SH executable and must reproduce
`artifacts/busybox` exactly unless `SKIP_BUSYBOX_ARTIFACT_CHECK=1` is set.

This wrapper is necessary because Ubuntu's SH cross libc and linker are
little-endian-only even though its compiler can emit big-endian object files.
All downloaded inputs have pinned SHA-256 values; the SDK itself remains an
external download because it has separate redistribution terms.

`configure-busybox-minimal.sh` documents and regenerates the configuration.
It accepts a BusyBox source directory as its first argument and uses
`build/linux-6.1/scripts/config` by default.  `KERNEL_TREE` and
`CROSS_COMPILE` can override those defaults.

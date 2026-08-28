# LNX03 loader

LNX03 is a normal DATAPLUS add-in that validates `LINUX.PAY`, copies it into
the XD-B8600's RAM, and transfers control to it. It does not replace or write
the Casio firmware.

## Stage the checked-in loader (no compiler needed)

The fastest clean-clone path is:

```sh
./scripts/stage-loader-artifact.sh
```

This verifies `artifacts/lnx03-casiotxt-loader-20260827.d01` and creates the
complete Japanese and Chinese add-in directories at:

```text
build/exword-data/ja/LNX03
build/exword-data/cn/LNX03
```

Build and run the USB client with `./scripts/build-libexword.sh` and
`./scripts/run-libexword.sh`. In library mode, an already-authenticated user
can install the staged Japanese package with `dict install LNX03`. Do not use
`dict reset` casually: libexword documents that it deletes installed add-ons.

## Rebuild from source

Requirements are Docker, `curl`, `tar`, and either `sha256sum` or `shasum`.
Run:

```sh
./scripts/build-loader.sh
```

The script downloads the Linux/x86-64 devkitSH4 SDK distributed by
[MaxSignal/buildscripts](https://github.com/MaxSignal/buildscripts), verifies
SHA-256
`1609b719e62224521243fea086ea492759fd9ee4116e1450d8b5e69fccfc7555`,
and runs it in a Linux/amd64 container. The SDK contains GCC 8.3.0,
`elf2d01`, and the libdataplus libraries needed by this source.
The corresponding libdataplus source baseline is upstream commit
`fd4681fe0873700acfcb7128af18622d4f5e70a5` at
<https://github.com/brijohn/libdataplus>.

The clean build has been verified to reproduce the checked-in loader exactly:

```text
fa037d2e364845e19983aa617471870da669776994feb148970cf50315617a45
```

Outputs are left in `loader/build/{ja,cn}/LNX03` and copied to
`build/exword-data/{ja,cn}/LNX03`. The compiler SDK is cached in
`build/loader-sdk`, which is ignored by Git. To use a previously downloaded
archive, set `DEVKITSH4_ARCHIVE=/absolute/path/devkitPro.tar.gz`.

The toolchain archive is not committed because it is about 31 MiB compressed
and has separate redistribution terms. Its exact URL and checksum are pinned;
the build refuses an archive with different contents.

# Source provenance

Generated build trees and downloaded archives are not committed. These are the
exact baselines used by the port.

| Component | Version / revision | SHA-256 or revision |
| --- | --- | --- |
| Linux | 6.1 | `2ca1f17051a430f6fed1196e4952717507171acfd97d96577212502703b25deb` |
| BusyBox | 1.37.0 | `3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4` |
| musl (exact BusyBox rebuild) | 1.2.5 | `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| musl (Buildroot X11 toolchain) | 1.1.22 | `8b0941a48d2f980fd7036cfbd24aa1d414f03d9a0652ecbd5ec5c7ff1bee29e3` |
| X.Org server | 1.19.7 | `7112f7128a4f5b06ceb8bba1bdc5e5c9e0fae682a42d35218bc12ba693f4c80c` |
| xterm | 327 | `66fb2f6c35b342148f549c276b12a3aa3fb408e27ab6360ddec513e14376150b` |
| w3m | Git revision | `ee66aabc3987000c2851bce6ade4dcbb0b037d81` |
| Boehm-Demers-Weiser GC | 8.0.0 | Buildroot 2019.02.11 package and recorded hash |
| AwesomeWM | 1.3 development head | `d4f1b99c93c7da10af774500f3c007e77a765c5d` plus the local patch under `x11/awesome` |
| awesome-copycats Holo visual reference and wallpaper | pinned Git revision | `affb71fa9ea69460208590f90383b3b0e8bab9f0`; device derivative details under `x11/assets` |
| Buildroot | 2019.02.11 | `5a6d31c87e1573bc83986471c194b944d7a365b7` |
| libexword | `2.0-dev` | `d186e356d942ea83c89480dc9d2110c1dbe520ef` plus the local changes in `tools/libexword` |
| devkitSH4 SDK archive | GCC 8.3.0, Linux x86-64 | `1609b719e62224521243fea086ea492759fd9ee4116e1450d8b5e69fccfc7555` |
| libdataplus source reference | `master` | `fd4681fe0873700acfcb7128af18622d4f5e70a5` |

Primary upstream locations:

- Linux: <https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz>
- BusyBox: <https://busybox.net/downloads/busybox-1.37.0.tar.bz2>
- musl 1.2.5: <https://musl.libc.org/releases/musl-1.2.5.tar.gz>
- musl 1.1.22: <https://musl.libc.org/releases/musl-1.1.22.tar.gz>
- X.Org: <https://www.x.org/releases/individual/xserver/xorg-server-1.19.7.tar.bz2>
- xterm: <https://invisible-mirror.net/archives/xterm/xterm-327.tgz>
- w3m: <https://github.com/tats/w3m>
- Boehm GC: <https://www.hboehm.info/gc/>
- AwesomeWM: <https://github.com/awesomeWM/awesome>
- awesome-copycats: <https://github.com/lcpz/awesome-copycats>
- Buildroot: <https://gitlab.com/buildroot.org/buildroot>
- libexword: <https://github.com/brijohn/libexword>
- devkitSH4 SDK: <https://github.com/MaxSignal/buildscripts/releases/download/Linux/devkitPro.tar.gz>
- libdataplus: <https://github.com/brijohn/libdataplus>

The checksum-pinned devkitSH4 archive contains the prebuilt libdataplus used
for the exact LNX03 rebuild. The libdataplus revision above records the
upstream source checkout inspected during development; it is not a claim that
the archive exposes build metadata proving its embedded libraries came from
that exact commit.

`scripts/prepare-awesome.sh` clones AwesomeWM only at the revision recorded
above, verifies the resulting commit, and then applies the source-controlled
EX-word patch. The patch adapts the historical 1.3 code to the Buildroot
libraries, removes the Xft dependency, and implements the one-desktop Holo
visual adaptation used by v9. Holo is referenced at awesome-copycats commit
`affb71fa9ea69460208590f90383b3b0e8bab9f0`; this port reimplements its
palette and flat presentation for AwesomeWM 1.3 and includes a center-cropped,
RGB565 derivative of its reef wallpaper. It does not import the modern Lua,
`lain`, Cairo, or Pango runtime. The original and device-ready wallpaper
hashes, conversion, attribution, and CC BY-SA 4.0 terms are recorded in
[`../x11/assets/README.md`](../x11/assets/README.md). The `exdesk`,
secondary-pad, startup-gate, and wrapper sources are project-original files
stored directly under `x11/`; they are not downloaded during the build.

`scripts/prepare-w3m.sh` fetches only the recorded w3m revision and rejects a
different checkout. The cross-build is text-only and uses the Boehm GC from
the pinned Buildroot release; no w3m source or generated build tree is
committed.

The Docker recipes currently use Ubuntu 22.04 tags. Those tags and apt
repositories are not content-addressed inputs, so target source builds are
validated for architecture, linkage, closure, and size rather than promised
to be bit-for-bit identical indefinitely. LNX03 is the exception: its build
must match the checked-in D01 exactly or the script fails.

Hardware reference: Renesas SH7724 Group Hardware Manual. The manual is not
redistributed here; obtain it from Renesas.

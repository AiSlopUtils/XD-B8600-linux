# Source provenance

Generated build trees and downloaded archives are not committed. These are the
exact baselines used by the port.

| Component | Version / revision | SHA-256 or revision |
| --- | --- | --- |
| Linux | 6.1 | `2ca1f17051a430f6fed1196e4952717507171acfd97d96577212502703b25deb` |
| BusyBox | 1.37.0 | `3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4` |
| musl | 1.2.5 | `a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4` |
| X.Org server | 1.19.7 | `7112f7128a4f5b06ceb8bba1bdc5e5c9e0fae682a42d35218bc12ba693f4c80c` |
| Buildroot | 2019.02.11 | `5a6d31c87e1573bc83986471c194b944d7a365b7` |
| libexword | `2.0-dev` | `d186e35` plus the local changes in `tools/libexword` |

Primary upstream locations:

- Linux: <https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz>
- BusyBox: <https://busybox.net/downloads/busybox-1.37.0.tar.bz2>
- musl: <https://musl.libc.org/releases/musl-1.2.5.tar.gz>
- X.Org: <https://www.x.org/releases/individual/xserver/xorg-server-1.19.7.tar.bz2>
- Buildroot: <https://gitlab.com/buildroot.org/buildroot>
- libexword: <https://github.com/brijohn/libexword>

Hardware reference: Renesas SH7724 Group Hardware Manual. The manual is not
redistributed here; obtain it from Renesas.

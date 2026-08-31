# Third-party code and binary notice

This research repository contains or builds modified third-party software.
Individual source headers and upstream projects define the applicable terms.
Project-original material not carrying another license notice is available
under the repository's [Zero-Clause BSD License](LICENSE). That license does
not replace or weaken any third-party license listed below.

- Linux kernel overlay: Linux 6.1, GPL-2.0-only. The overlay plus the pinned
  upstream archive and exact config constitute the source changes for the
  included kernel binary.
- BusyBox: BusyBox 1.37.0, GPL-2.0-or-later. The exact config, version, source
  hash, and unmodified upstream source location are recorded here.
- X.Org, xterm, musl, X libraries, and Buildroot packages retain their
  respective upstream licenses. The exact Buildroot overlay and inputs are
  included. The local xterm controlling-terminal fix remains under xterm's
  upstream permissive notices.
- w3m is built from commit
  `ee66aabc3987000c2851bce6ade4dcbb0b037d81` under its upstream permissive
  notice, which requires preservation of its copyright notice. Boehm GC
  8.0.0 and libatomic_ops are supplied by Buildroot and retain their upstream
  notices. Their generated binaries are not covered by the project's 0BSD
  license. The required w3m notice is preserved verbatim in
  [`x11/w3m/COPYING`](x11/w3m/COPYING) and in the packaged image.
- AwesomeWM 1.3 is built from commit
  `d4f1b99c93c7da10af774500f3c007e77a765c5d` and is licensed
  GPL-2.0-or-later by its upstream authors. The repository patch modifies
  that GPL-covered program and does not place it under the project's 0BSD
  license.
- The Holo visual design and reef wallpaper are adapted from Luca CPZ's
  `lcpz/awesome-copycats` at commit
  `affb71fa9ea69460208590f90383b3b0e8bab9f0`. The 528x320 RGB565 wallpaper
  derivative and adapted visual material are distributed under Creative
  Commons Attribution-ShareAlike 4.0 International, not 0BSD. See
  [`x11/assets/README.md`](x11/assets/README.md) for source and derivative
  hashes, conversion details, attribution, and the license link. The modern
  awesome-copycats Lua/`lain`/Cairo/Pango runtime is not incorporated.
- The `exdesk`, X startup gate, secondary-LCD/touch helpers, and shell
  wrappers are project-original code under 0BSD unless an individual file
  says otherwise. Their use of X11 libraries does not change the licenses of
  those libraries.
- `tools/libexword` derives from Brian Johnson's GPL-2.0-or-later libexword at
  commit `d186e35` and preserves its copyright notices.
- Loader/experiment code depends on the separately developed devkitSH4 and
  libdataplus projects. Verify their redistribution terms before repackaging
  those SDK components; the SDK/toolchain itself is not committed here.

No Casio firmware image, dictionary data, user authentication file, or Renesas
hardware-manual PDF is included.

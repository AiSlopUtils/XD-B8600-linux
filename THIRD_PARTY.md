# Third-party code and binary notice

This research repository contains or builds modified third-party software.
Individual source headers and upstream projects define the applicable terms.

- Linux kernel overlay: Linux 6.1, GPL-2.0-only. The overlay plus the pinned
  upstream archive and exact config constitute the source changes for the
  included kernel binary.
- BusyBox: BusyBox 1.37.0, GPL-2.0-or-later. The exact config, version, source
  hash, and unmodified upstream source location are recorded here.
- X.Org, twm, xterm, musl, X libraries, and Buildroot packages retain their
  respective upstream licenses. The exact Buildroot overlay and inputs are
  included.
- `tools/libexword` derives from Brian Johnson's GPL-2.0-or-later libexword at
  commit `d186e35` and preserves its copyright notices.
- Loader/experiment code depends on the separately developed devkitSH4 and
  libdataplus projects. Verify their redistribution terms before repackaging
  those SDK components; the SDK/toolchain itself is not committed here.

No Casio firmware image, dictionary data, user authentication file, or Renesas
hardware-manual PDF is included.

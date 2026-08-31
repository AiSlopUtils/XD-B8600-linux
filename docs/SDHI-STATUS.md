# SD/MMC status: disabled

SD/MMC is disabled in the current XD-B8600 build. The tested unit's card slot
needs hardware repair, and an experimental SDHI0 registration reported a
zero-rate input clock before a subsequent init stall.

The boot-safe configuration therefore has all three safeguards:

- `CONFIG_MMC` is unset in `kernel/exword_defconfig`;
- `mach-exword/setup.c` registers no SDHI or MMCIF platform device; and
- init launches no SD-card mount or swap helper.

Consequently the kernel does not probe or access the card slot. Internal Casio
storage and NOR flash also remain unmodified. The system uses its bounded zram
device as the only swap path.

The read-only `sdhwinfo` utility remains available for future diagnostics, but
do not write PFC/clock registers or manually bind a storage driver. SD support
should only return after the physical slot and inherited SH7724 clock/power
state have been validated independently on working hardware.

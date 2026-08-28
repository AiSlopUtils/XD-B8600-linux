# SD/MMC status

The XD-B8600 firmware leaves enough hardware initialized for Linux to run, but
Linux 6.1's SH7724 legacy clock model currently reports the CPU and peripheral
clock tree at 0 Hz. Boot logs show this independently as a preset loops-per-
jiffy value of zero.

The earlier SDHI1 platform device used the correct SH7724 resources
(`0x04cf0000`, event `0x4e0`, device ID 1), but the Renesas SDHI divider loop
shifted a zero clock forever. The latest source therefore has two safeguards:

1. `mach-exword/setup.c` registers SDHI1 with
   `driver_override=exword-storage-disabled`, preventing automatic binding.
2. `renesas_sdhi_core.c` refuses a zero-rate input and guards the divider loop.

The read-only `sdhwinfo` command captures:

- PSELA, PSELE, PWCR, and HIZCRC pin-function registers.
- FRQCRA, FRQCRB, PLLCR, FLLFRQ, and MSTPCR2 clock registers.
- The SDHI1 platform override and driver-binding state.

According to the SH7724 manual, SDHI1 uses PTW0-PTW7 and requires PSELA bit 13
set with PSELE bits 13:12 equal to `01`. Zero in those fields indicates the
alternative MMCIF mux. Card power and card detect also require validation.

Do not guess PLL values, write pin-control registers, or bind the storage
driver until the inherited values have been decoded. Even after the clock
tree is fixed, verify the correct controller and voltage/power path before any
write. Never remove an SD card while it is active swap.

One cleanup remains before treating the defensive zero-clock path as general
upstream-quality code: balance the already-enabled card-detect clock when that
error path returns. The platform override means that path is not entered in
the current recovery payload.

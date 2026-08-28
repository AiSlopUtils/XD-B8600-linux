# EX-word Linux low-memory review

Target: Casio EX-word XD-B8600 (DATAPLUS 6), Renesas SH-4A, 16 MiB RAM.

## Result

- Final payload: `linux-lowmem-zram-tmu100.pay`
- Payload size: 1,237,056 bytes (32-byte EXWPAY header plus 1,237,024-byte zImage)
- Payload SHA-256: `3b635fffc10bf98d494c5c1c8bdb621d3a1ccb405b32947551e58bb989d9c5d6`
- Payload CRC32: `ee68dbe8`
- Preserved fallback: `linux-working-pre-lowmem-20260827.pay`
- Fallback SHA-256: `fbf14f8cb7b7813bc779d5c910bb35ab9369c5fcd270fa17de336519a8acbe66`

The payload header, declared length, load/entry address (`0xac800000`), CRC32,
flags, and byte-for-byte zImage body were independently verified.

## Measured size change

| Measurement | Working build | Low-memory build | Change |
| --- | ---: | ---: | ---: |
| Persistent kernel sections | 2,513,740 B | 1,583,772 B | -929,968 B (-37.0%) |
| All measured vmlinux sections | 2,826,323 B | 1,870,459 B | -955,864 B (-33.8%) |
| Compressed zImage | 1,753,120 B | 1,237,024 B | -516,096 B (-29.4%) |
| Static BusyBox | 300,096 B | 304,192 B | +4,096 B |

The persistent-section figure is the best static estimate of RAM recovered
after init memory is released. Zram has a small fixed overhead and allocates
compressed pages only as swap is used.

## Memory-management changes

- SLOB allocator and FLATMEM for the single contiguous 16 MiB RAM bank.
- Compaction retained because SH uses order-1 (8 KiB) kernel stacks;
  proactive background compaction is disabled while direct compaction remains.
- 8 MiB logical zram swap using LZO-RLE, with a hard 4 MiB physical-memory cap.
- `vm.swappiness=150`, `vm.page-cluster=0`, allocating-task OOM selection, and
  `panic_on_oom=0`.
- Separate demand-allocated `/tmp` tmpfs limited to 1 MiB and 128 inodes.
- Root dentry/inode hash tables capped at 256 entries each.
- SLOB, smaller printk buffers, 8x8-only font, CRC32 Sarwate, BASE_SMALL, and
  removal of unused KALLSYMS/debug, HID/serio, pinctrl/GPIO, power-management,
  firmware-loader, watcher, decompressor, and compatibility machinery.
- SH perf/hardware breakpoints are retained because SH ptrace single-step
  requires them.

## Timer correction

The stock SH7724 TMU0 resource (`0xffd80000`) did not point at the XD-B8600
timer. The EX-word build now maps physical `0x04490000`, which becomes the
known-working uncached alias `0xa4490000`. It uses a periodic 100 Hz tick with
NO_HZ and high-resolution/one-shot timers disabled. This is required for
reclaim waits, `sleep`, and `top` to make progress reliably.

## Swap limit

The approximately 445 MiB Casio storage and microSD slot are not exposed as a
Linux block device by this kernel. A swapfile on `/` or `/tmp` would therefore
be a file in RAM-backed tmpfs and would reduce, not increase, available RAM.
Zram is the safe current swap path. A large persistent swap area requires a
separately validated storage driver.

## First-device validation

Before intentional memory pressure:

1. Confirm the banner reports `8 MiB zram (4 MiB RAM cap)`.
2. Run `free` and `cat /proc/swaps`.
3. Read `cat /proc/uptime`, wait ten real seconds, and read it again. It should
   advance by about ten seconds.
4. Run `sleep 2`; the prompt should return after about two seconds.
5. Run `cat /proc/interrupts` twice ten seconds apart. The TMU interrupt count
   should rise by about 1,000 at 100 Hz.
6. Run `top`; use `s` for its detailed memory view and `q` or Ctrl-C to exit.
7. Inspect `cat /sys/block/zram0/mm_stat` after normal use.

Do not run `swapoff /dev/zram0` under pressure: that requires inflating all
swapped pages back into physical RAM.

## Known follow-up

Earlier boot logs reported `lpj=0`, so CPU-clock reporting and very short
`udelay()` calibration still need hardware validation. The TMU correction is
kept separate from an unverified PLL/divider change. If elapsed time is wrong,
capture the observed uptime ratio and the PLL/FRQCR registers before changing
the clock tree.

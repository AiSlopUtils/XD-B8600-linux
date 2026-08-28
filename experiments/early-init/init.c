/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Minimal, freestanding first userspace for the EX-word Linux bring-up.
 *
 * The standard Debian SH cross-libc is little-endian only, while this device
 * runs SH-4A big-endian.  Keeping PID 1 freestanding also makes the first boot
 * much smaller and easier to audit: it only writes to the console and sleeps.
 */

typedef unsigned long size_t;

extern long linux_write(int descriptor, const void *buffer, size_t count);
extern long linux_pause(void);
extern void linux_exit(int status) __attribute__((noreturn));

static const char banner[] =
	"\033[2J\033[H"
	"\n"
	"  EX-WORD LINUX\n"
	"  =============\n"
	"\n"
	"  Linux userspace reached successfully.\n"
	"\n"
	"  CPU:  Renesas SH-4A (big-endian)\n"
	"  RAM:  16 MiB\n"
	"  Root: built-in read-only initramfs\n"
	"\n"
	"  No NOR flash or Casio firmware was modified.\n"
	"  Use the hardware RESET button to return to Casio firmware.\n";

int init_main(void)
{
	/* The kernel opens /dev/console as descriptors 0, 1, and 2 for PID 1. */
	(void)linux_write(1, banner, sizeof(banner) - 1);
	(void)linux_write(2, "EX-word: PID 1 is alive\n", 25);

	/* PID 1 must never return.  pause(2) is enough for this first milestone. */
	for (;;)
		(void)linux_pause();
}

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __sh__
#error "sublcd-test must be built for SuperH"
#endif

#ifndef __SH4A__
#error "sublcd-test must be built for SH-4A"
#endif

#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "sublcd-test must be built big-endian"
#endif

#define SUBLCD_WIDTH 244U
#define SUBLCD_HEIGHT 99U
#define SUBLCD_UI_WIDTH 240U
#define SUBLCD_UI_HEIGHT 96U

/* Linux /dev/mem uses physical addresses, not SH P2 virtual aliases. */
#define SUBLCD_DATA_ALIAS UINT32_C(0xb8000000)
#define SUBLCD_DATA_PHYS UINT32_C(0x18000000)
#define SUBLCD_PRDR_ALIAS UINT32_C(0xa405013c)
#define SUBLCD_PRDR_PHYS UINT32_C(0x0405013c)
#define SUBLCD_DC UINT8_C(0x04)

#define SUBPAD_PARAMETER "/sys/module/exword_keypad/parameters/subpad_mode"
#define SUBLCD_LOCK "/tmp/exword-subpad.lock"

enum test_kind {
	TEST_INFO,
	TEST_INFO_LIVE,
	TEST_ROW,
	TEST_PATTERN_BARS,
	TEST_PATTERN_PAD,
};

enum touch_zone {
	ZONE_NONE,
	ZONE_LEFT,
	ZONE_UP,
	ZONE_RIGHT,
	ZONE_BUTTON_LEFT,
	ZONE_DOWN,
	ZONE_BUTTON_RIGHT,
};

struct options {
	enum test_kind kind;
	unsigned int row;
	uint16_t color;
};

struct mmio_maps {
	int fd;
	size_t page_size;
	void *prdr_page;
	void *data_page;
	volatile uint8_t *prdr;
	volatile uint16_t *data;
};

static void usage(FILE *stream)
{
	fputs(
		"EX-word XD-B8600 secondary-LCD diagnostic\n"
		"\n"
		"Usage:\n"
		"  sublcd-test info\n"
		"  sublcd-test info --live\n"
		"  sublcd-test row [top|middle|bottom] [white|red|green|blue] --yes\n"
		"  sublcd-test pattern [bars|pad] --yes\n"
		"\n"
		"'info' performs no MMIO.  '--live' reads only PRDR.\n"
		"Run row first, from the text console after leaving X.  A bad external-\n"
		"bus assumption can hard-freeze the device; reset it if that happens.\n",
		stream);
}

static int parse_row_name(const char *text, unsigned int *row)
{
	char *end;
	unsigned long value;

	if (strcmp(text, "top") == 0) {
		*row = 0;
		return 0;
	}
	if (strcmp(text, "middle") == 0) {
		*row = SUBLCD_HEIGHT / 2;
		return 0;
	}
	if (strcmp(text, "bottom") == 0) {
		*row = SUBLCD_HEIGHT - 1;
		return 0;
	}

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno != 0 || *text == '\0' || *end != '\0' ||
	    value >= SUBLCD_HEIGHT)
		return -1;
	*row = (unsigned int)value;
	return 0;
}

static int parse_color(const char *text, uint16_t *color)
{
	if (strcmp(text, "white") == 0)
		*color = UINT16_C(0xffff);
	else if (strcmp(text, "red") == 0)
		*color = UINT16_C(0xf800);
	else if (strcmp(text, "green") == 0)
		*color = UINT16_C(0x07e0);
	else if (strcmp(text, "blue") == 0)
		*color = UINT16_C(0x001f);
	else
		return -1;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *options)
{
	bool color_seen = false;
	bool row_seen = false;
	int argument;

	options->kind = TEST_INFO;
	options->row = SUBLCD_HEIGHT / 2;
	options->color = UINT16_C(0xffff);

	if (argc == 1 || (argc == 2 && strcmp(argv[1], "info") == 0))
		return 0;
	if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
			  strcmp(argv[1], "-h") == 0))
		return 1;
	if (argc == 3 && strcmp(argv[1], "info") == 0 &&
	    strcmp(argv[2], "--live") == 0) {
		options->kind = TEST_INFO_LIVE;
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "row") == 0 &&
	    strcmp(argv[argc - 1], "--yes") == 0) {
		if (argc > 5)
			return -1;
		options->kind = TEST_ROW;
		for (argument = 2; argument < argc - 1; argument++) {
			unsigned int row;
			uint16_t color;

			if (parse_row_name(argv[argument], &row) == 0) {
				if (row_seen)
					return -1;
				options->row = row;
				row_seen = true;
				continue;
			}
			if (parse_color(argv[argument], &color) == 0) {
				if (color_seen)
					return -1;
				options->color = color;
				color_seen = true;
				continue;
			}
			return -1;
		}
		return 0;
	}
	if ((argc == 3 || argc == 4) && strcmp(argv[1], "pattern") == 0 &&
	    strcmp(argv[argc - 1], "--yes") == 0) {
		const char *name = argc == 4 ? argv[2] : "bars";

		if (strcmp(name, "bars") == 0)
			options->kind = TEST_PATTERN_BARS;
		else if (strcmp(name, "pad") == 0)
			options->kind = TEST_PATTERN_PAD;
		else
			return -1;
		return 0;
	}
	return -1;
}

static int subpad_mode(void)
{
	char buffer[32];
	char *end;
	long value;
	ssize_t count;
	int fd;

	fd = open(SUBPAD_PARAMETER, O_RDONLY);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	count = read(fd, buffer, sizeof(buffer) - 1);
	if (close(fd) < 0 && count >= 0)
		count = -1;
	if (count <= 0)
		return -1;
	buffer[count] = '\0';
	errno = 0;
	value = strtol(buffer, &end, 10);
	if (errno != 0 || end == buffer)
		return -1;
	while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
		end++;
	if (*end != '\0')
		return -1;
	/* 1 means the experimental kernel interface exists but is disabled. */
	return value == 0 ? 1 : 2;
}

static int check_devmem(bool verbose)
{
	struct stat status;

	if (stat("/dev/mem", &status) < 0) {
		if (verbose)
			printf("/dev/mem: unavailable (%s)\n", strerror(errno));
		return -1;
	}
	if (!S_ISCHR(status.st_mode)) {
		if (verbose)
			puts("/dev/mem: present, but not a character device");
		errno = ENODEV;
		return -1;
	}
	if (verbose)
		puts("/dev/mem: character device present");
	return 0;
}

static void report_framebuffer(void)
{
	struct fb_var_screeninfo variable;
	int fd;

	fd = open("/dev/fb0", O_RDONLY);
	if (fd < 0) {
		printf("Main framebuffer: unavailable (%s)\n", strerror(errno));
		return;
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &variable) == 0)
		printf("Main framebuffer: %ux%u, %u bits/pixel\n",
		       variable.xres, variable.yres, variable.bits_per_pixel);
	else
		printf("Main framebuffer: query failed (%s)\n", strerror(errno));
	(void)close(fd);
}

static void report_console(void)
{
	int mode;

	if (!isatty(STDIN_FILENO)) {
		puts("Current input: not a tty (write tests will refuse)");
		return;
	}
	if (ioctl(STDIN_FILENO, KDGETMODE, &mode) < 0) {
		puts("Current input: not a Linux VT (leave X before write tests)");
		return;
	}
	printf("Current Linux VT: %s\n",
	       mode == KD_TEXT ? "text mode; write tests permitted" :
	       "graphics mode; write tests will refuse");
}

static int show_info(void)
{
	int mode = subpad_mode();

	puts("EX-word XD-B8600 secondary-LCD diagnostic");
	puts("Target: big-endian Renesas SH-4A");
	printf("Panel geometry: %ux%u RGB565 (UI area %ux%u)\n",
	       SUBLCD_WIDTH, SUBLCD_HEIGHT,
	       SUBLCD_UI_WIDTH, SUBLCD_UI_HEIGHT);
	printf("Data port: P2 alias 0x%08x -> physical 0x%08x, 16-bit write only\n",
	       SUBLCD_DATA_ALIAS, SUBLCD_DATA_PHYS);
	printf("DC port:   P2 alias 0x%08x -> physical 0x%08x, bit 0x%02x\n",
	       SUBLCD_PRDR_ALIAS, SUBLCD_PRDR_PHYS, (unsigned int)SUBLCD_DC);
	(void)check_devmem(true);
	report_framebuffer();
	report_console();
	if (mode < 0)
		puts("Kernel subpad mode: unreadable; write tests will refuse");
	else if (mode == 2)
		puts("Kernel subpad mode: active; write tests will refuse");
	else if (mode == 1)
		puts("Kernel subpad mode: experimental interface present; write tests will refuse");
	else
		puts("Kernel subpad mode: absent (expected for the proven kernel)");
	puts("No MMIO was performed. Next safe check: sublcd-test info --live");
	return 0;
}

static void close_maps(struct mmio_maps *maps)
{
	if (maps->data_page != MAP_FAILED)
		(void)munmap(maps->data_page, maps->page_size);
	if (maps->prdr_page != MAP_FAILED)
		(void)munmap(maps->prdr_page, maps->page_size);
	if (maps->fd >= 0)
		(void)close(maps->fd);
	maps->fd = -1;
	maps->prdr_page = MAP_FAILED;
	maps->data_page = MAP_FAILED;
}

static int open_maps(struct mmio_maps *maps, bool writable)
{
	uintptr_t page_mask;
	off_t prdr_base;
	off_t data_base;
	long page_size;
	int protection;

	maps->fd = -1;
	maps->page_size = 0;
	maps->prdr_page = MAP_FAILED;
	maps->data_page = MAP_FAILED;
	maps->prdr = NULL;
	maps->data = NULL;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0 || (page_size & (page_size - 1)) != 0 ||
	    page_size > 65536) {
		fprintf(stderr, "sublcd-test: unsupported page size %ld\n", page_size);
		return -1;
	}
	maps->page_size = (size_t)page_size;
	page_mask = (uintptr_t)maps->page_size - 1U;
	prdr_base = (off_t)(SUBLCD_PRDR_PHYS & ~page_mask);
	data_base = (off_t)(SUBLCD_DATA_PHYS & ~page_mask);

	if (check_devmem(false) < 0) {
		fprintf(stderr, "sublcd-test: /dev/mem is unavailable: %s\n",
			strerror(errno));
		return -1;
	}
	maps->fd = open("/dev/mem", (writable ? O_RDWR : O_RDONLY) | O_SYNC);
	if (maps->fd < 0) {
		fprintf(stderr, "sublcd-test: cannot open /dev/mem: %s\n",
			strerror(errno));
		return -1;
	}
	protection = PROT_READ | (writable ? PROT_WRITE : 0);
	maps->prdr_page = mmap(NULL, maps->page_size, protection, MAP_SHARED,
				maps->fd, prdr_base);
	if (maps->prdr_page == MAP_FAILED) {
		fprintf(stderr, "sublcd-test: cannot map PRDR: %s\n",
			strerror(errno));
		close_maps(maps);
		return -1;
	}
	maps->prdr = (volatile uint8_t *)maps->prdr_page +
		(SUBLCD_PRDR_PHYS & page_mask);

	if (!writable)
		return 0;
	maps->data_page = mmap(NULL, maps->page_size, protection, MAP_SHARED,
				maps->fd, data_base);
	if (maps->data_page == MAP_FAILED) {
		fprintf(stderr, "sublcd-test: cannot map secondary LCD data port: %s\n",
			strerror(errno));
		close_maps(maps);
		return -1;
	}
	maps->data = (volatile uint16_t *)((volatile uint8_t *)maps->data_page +
		(SUBLCD_DATA_PHYS & page_mask));
	if (((uintptr_t)maps->data & 1U) != 0) {
		fputs("sublcd-test: LCD data mapping is not 16-bit aligned\n", stderr);
		close_maps(maps);
		return -1;
	}
	return 0;
}

static inline void io_barrier(void)
{
	__asm__ __volatile__("synco" ::: "memory");
}

static int show_live_info(void)
{
	struct mmio_maps maps;
	uint8_t value;

	if (geteuid() != 0) {
		fputs("sublcd-test: info --live must be run as root\n", stderr);
		return 1;
	}
	if (open_maps(&maps, false) < 0)
		return 1;
	value = *maps.prdr;
	io_barrier();
	close_maps(&maps);
	printf("PRDR = 0x%02x; secondary-LCD DC is %s\n",
	       (unsigned int)value,
	       (value & SUBLCD_DC) != 0 ? "high (data)" : "low (command)");
	puts("One 8-bit PRDR read completed. The external LCD bus was not mapped or read.");
	return 0;
}

static int acquire_lock(void)
{
	int fd = open(SUBLCD_LOCK, O_RDWR | O_CREAT, 0600);

	if (fd < 0) {
		fprintf(stderr, "sublcd-test: cannot open lock: %s\n", strerror(errno));
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr, "sublcd-test: another LCD test is active: %s\n",
			strerror(errno));
		(void)close(fd);
		return -1;
	}
	return fd;
}

static int enter_quiet_console(int *saved_mode, bool *quieted)
{
	struct fb_fix_screeninfo fixed;
	struct fb_var_screeninfo variable;
	struct stat tty_status;
	struct vt_stat vt_state;
	int framebuffer;

	*quieted = false;

	if (!isatty(STDIN_FILENO) ||
	    ioctl(STDIN_FILENO, KDGETMODE, saved_mode) < 0) {
		fputs("sublcd-test: run this from the Linux text console, not xterm/X\n",
		      stderr);
		return -1;
	}
	if (*saved_mode != KD_TEXT) {
		fputs("sublcd-test: current VT is in graphics mode; leave X first\n",
		      stderr);
		return -1;
	}
	if (fstat(STDIN_FILENO, &tty_status) < 0 ||
	    !S_ISCHR(tty_status.st_mode) ||
	    major(tty_status.st_rdev) != 4U ||
	    minor(tty_status.st_rdev) == 0U ||
	    ioctl(STDIN_FILENO, VT_GETSTATE, &vt_state) < 0 ||
	    minor(tty_status.st_rdev) != (unsigned int)vt_state.v_active) {
		fputs("sublcd-test: current tty is not the active numbered Linux VT\n",
		      stderr);
		return -1;
	}
	if (ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS) < 0) {
		fprintf(stderr, "sublcd-test: cannot quiet the text console: %s\n",
			strerror(errno));
		return -1;
	}
	*quieted = true;

	/*
	 * exwordfb's pan callback cancels and drains its queued refresh worker,
	 * then performs one synchronous main-LCD refresh.  With fbcon now quiet,
	 * this prevents the main LCD and secondary LCD from racing on PRDR.
	 */
	errno = 0;
	framebuffer = open("/dev/fb0", O_RDWR);
	if (framebuffer < 0 ||
	    ioctl(framebuffer, FBIOGET_FSCREENINFO, &fixed) < 0 ||
	    ioctl(framebuffer, FBIOGET_VSCREENINFO, &variable) < 0 ||
	    strncmp(fixed.id, "EX-word LCD", sizeof(fixed.id)) != 0 ||
	    variable.xres != 528U || variable.yres != 320U ||
	    variable.bits_per_pixel != 16U ||
	    ioctl(framebuffer, FBIOPAN_DISPLAY, &variable) < 0) {
		int saved_errno = errno != 0 ? errno : ENODEV;

		if (framebuffer >= 0)
			(void)close(framebuffer);
		if (ioctl(STDIN_FILENO, KDSETMODE, *saved_mode) == 0) {
			*quieted = false;
		} else {
			fprintf(stderr,
				"sublcd-test: initial text-mode restore failed: %s; retrying during cleanup\n",
				strerror(errno));
		}
		errno = saved_errno;
		fprintf(stderr, "sublcd-test: cannot drain the main framebuffer: %s\n",
			strerror(errno));
		return -1;
	}
	(void)close(framebuffer);
	/* Give the worker scheduler several turns even if a late wakeup raced us. */
	for (unsigned int pass = 0; pass < 256U; pass++)
		(void)sched_yield();
	return 0;
}

static int leave_quiet_console(int saved_mode)
{
	if (ioctl(STDIN_FILENO, KDSETMODE, saved_mode) < 0) {
		fprintf(stderr,
			"sublcd-test: could not restore text mode: %s (reset if needed)\n",
			strerror(errno));
		return -1;
	}
	return 0;
}

static void lcd_command(struct mmio_maps *maps, uint16_t command)
{
	uint8_t prdr = *maps->prdr;

	*maps->prdr = (uint8_t)(prdr & (uint8_t)~SUBLCD_DC);
	io_barrier();
	*maps->data = command;
	io_barrier();
	*maps->prdr = (uint8_t)(prdr | SUBLCD_DC);
	io_barrier();
}

static inline void lcd_data(struct mmio_maps *maps, uint16_t value)
{
	*maps->data = value;
}

static void lcd_set_row(struct mmio_maps *maps, unsigned int row)
{
	lcd_command(maps, UINT16_C(0x21));
	lcd_data(maps, UINT16_C(6));
	lcd_command(maps, UINT16_C(0x52));
	lcd_data(maps, UINT16_C(6));
	lcd_command(maps, UINT16_C(0x53));
	lcd_data(maps, UINT16_C(6) + SUBLCD_WIDTH - 1U);
	lcd_command(maps, UINT16_C(0x20));
	lcd_data(maps, (uint16_t)row);
	lcd_command(maps, UINT16_C(0x50));
	lcd_data(maps, (uint16_t)row);
	lcd_command(maps, UINT16_C(0x51));
	lcd_data(maps, (uint16_t)row);
	lcd_command(maps, UINT16_C(0x22));
}

static unsigned int small_abs(int value)
{
	return value < 0 ? (unsigned int)-value : (unsigned int)value;
}

static bool arrow_pixel(enum touch_zone zone, int x, int y)
{
	int ax = x - 40;
	int ay = y - 24;

	switch (zone) {
	case ZONE_LEFT:
		return (x >= 35 && x <= 60 && small_abs(ay) <= 3U) ||
			(x >= 17 && x <= 40 &&
			 small_abs(ay) <= (unsigned int)(x - 17) / 2U);
	case ZONE_UP:
		return (y >= 22 && y <= 40 && small_abs(ax) <= 3U) ||
			(y >= 7 && y <= 28 &&
			 small_abs(ax) <= (unsigned int)(y - 7) / 2U);
	case ZONE_RIGHT:
		return (x >= 20 && x <= 45 && small_abs(ay) <= 3U) ||
			(x >= 40 && x <= 63 &&
			 small_abs(ay) <= (unsigned int)(63 - x) / 2U);
	case ZONE_DOWN:
		return (y >= 8 && y <= 26 && small_abs(ax) <= 3U) ||
			(y >= 20 && y <= 41 &&
			 small_abs(ax) <= (unsigned int)(41 - y) / 2U);
	default:
		return false;
	}
}

static bool letter_pixel(enum touch_zone zone, int x, int y)
{
	static const uint8_t letter_l[7] = {
		UINT8_C(0x10), UINT8_C(0x10), UINT8_C(0x10), UINT8_C(0x10),
		UINT8_C(0x10), UINT8_C(0x10), UINT8_C(0x1f),
	};
	static const uint8_t letter_r[7] = {
		UINT8_C(0x1e), UINT8_C(0x11), UINT8_C(0x11), UINT8_C(0x1e),
		UINT8_C(0x14), UINT8_C(0x12), UINT8_C(0x11),
	};
	const uint8_t *rows;
	unsigned int column;
	unsigned int row;

	if (zone != ZONE_BUTTON_LEFT && zone != ZONE_BUTTON_RIGHT)
		return false;
	if (x < 30 || x >= 50 || y < 10 || y >= 38)
		return false;
	rows = zone == ZONE_BUTTON_LEFT ? letter_l : letter_r;
	column = (unsigned int)(x - 30) >> 2;
	row = (unsigned int)(y - 10) >> 2;
	return (rows[row] & (uint8_t)(1U << (4U - column))) != 0;
}

static uint16_t pad_pixel(unsigned int x, unsigned int y)
{
	enum touch_zone zone;
	unsigned int local_x;
	unsigned int local_y;
	uint16_t background;

	if (x >= SUBLCD_UI_WIDTH || y >= SUBLCD_UI_HEIGHT)
		return UINT16_C(0x0000);
	if (x < 80U) {
		local_x = x;
		zone = y < 48U ? ZONE_LEFT : ZONE_BUTTON_LEFT;
	} else if (x < 160U) {
		local_x = x - 80U;
		zone = y < 48U ? ZONE_UP : ZONE_DOWN;
	} else {
		local_x = x - 160U;
		zone = y < 48U ? ZONE_RIGHT : ZONE_BUTTON_RIGHT;
	}
	local_y = y < 48U ? y : y - 48U;
	if (local_x < 2U || local_x >= 78U ||
	    local_y < 2U || local_y >= 46U)
		return UINT16_C(0xbdf7);
	background = (zone == ZONE_BUTTON_LEFT || zone == ZONE_BUTTON_RIGHT) ?
		UINT16_C(0x2448) : UINT16_C(0x194f);
	if (arrow_pixel(zone, (int)local_x, (int)local_y) ||
	    letter_pixel(zone, (int)local_x, (int)local_y))
		return UINT16_C(0xffff);
	return background;
}

static uint16_t bars_pixel(unsigned int x)
{
	static const uint16_t colors[8] = {
		UINT16_C(0xffff), UINT16_C(0xffe0), UINT16_C(0x07ff),
		UINT16_C(0x07e0), UINT16_C(0xf81f), UINT16_C(0xf800),
		UINT16_C(0x001f), UINT16_C(0x0000),
	};
	unsigned int index = (x * 8U) / SUBLCD_WIDTH;

	return colors[index < 8U ? index : 7U];
}

static int block_signals(sigset_t *old_set)
{
	static const int synchronous_fault_signals[] = {
		SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGTRAP, SIGSYS,
	};
	sigset_t blocked;
	unsigned int index;

	/* Block all catchable signals except synchronous hardware/software faults. */
	if (sigfillset(&blocked) < 0)
		return -1;
	for (index = 0;
	     index < sizeof(synchronous_fault_signals) /
		     sizeof(synchronous_fault_signals[0]);
	     index++) {
		if (sigdelset(&blocked, synchronous_fault_signals[index]) < 0)
			return -1;
	}
	return sigprocmask(SIG_BLOCK, &blocked, old_set);
}

static int perform_write(const struct options *options)
{
	struct mmio_maps maps;
	sigset_t old_set;
	unsigned int x;
	unsigned int y;
	int lock_fd;
	int saved_mode = KD_TEXT;
	int result = 1;
	bool console_quiet = false;
	bool signals_blocked = false;
	const char *deferred_error = NULL;
	int mode;

	if (geteuid() != 0) {
		fputs("sublcd-test: write tests must be run as root\n", stderr);
		return 1;
	}
	mode = subpad_mode();
	if (mode < 0) {
		fputs("sublcd-test: cannot verify kernel subpad mode; refusing MMIO\n",
		      stderr);
		return 1;
	}
	if (mode != 0) {
		fputs("sublcd-test: experimental kernel subpad control exists; refusing raw MMIO\n",
		      stderr);
		return 1;
	}
	lock_fd = acquire_lock();
	if (lock_fd < 0)
		return 1;
	if (open_maps(&maps, true) < 0)
		goto close_lock;

	if (options->kind == TEST_ROW)
		printf("About to write ONE %s test row at y=%u. ",
		       options->color == UINT16_C(0xffff) ? "white" : "colored",
		       options->row);
	else
		printf("About to write a complete %ux%u %s pattern. ",
		       SUBLCD_WIDTH, SUBLCD_HEIGHT,
		       options->kind == TEST_PATTERN_PAD ? "six-button" : "color-bar");
	puts("Reset the device if it freezes.");
	if (fflush(NULL) != 0) {
		fputs("sublcd-test: cannot flush the console; refusing MMIO\n", stderr);
		goto close_mmio;
	}
	/* Keep every catchable signal blocked until KD_TEXT and all locks return. */
	if (block_signals(&old_set) < 0) {
		fputs("sublcd-test: cannot block signals; refusing MMIO\n", stderr);
		goto close_mmio;
	}
	signals_blocked = true;
	if (enter_quiet_console(&saved_mode, &console_quiet) < 0)
		goto close_mmio;

	/* Absence is required again immediately before the first hardware write. */
	if (subpad_mode() != 0) {
		deferred_error =
			"sublcd-test: kernel subpad state changed; no LCD write was made";
		result = 1;
		goto close_mmio;
	}

	if (options->kind == TEST_ROW) {
		lcd_set_row(&maps, options->row);
		for (x = 0; x < SUBLCD_WIDTH; x++)
			lcd_data(&maps, options->color);
		io_barrier();
	} else {
		for (y = 0; y < SUBLCD_HEIGHT; y++) {
			lcd_set_row(&maps, y);
			for (x = 0; x < SUBLCD_WIDTH; x++) {
				uint16_t pixel = options->kind == TEST_PATTERN_PAD ?
					pad_pixel(x, y) : bars_pixel(x);
				lcd_data(&maps, pixel);
			}
			io_barrier();
			(void)sched_yield();
		}
	}
	/* Always leave the secondary controller in data mode. */
	*maps.prdr = (uint8_t)(*maps.prdr | SUBLCD_DC);
	io_barrier();
	result = 0;

	if (console_quiet) {
		if (leave_quiet_console(saved_mode) < 0) {
			result = 1;
		} else {
			console_quiet = false;
		}
	}

close_mmio:
	if (console_quiet)
		(void)leave_quiet_console(saved_mode);
	close_maps(&maps);
close_lock:
	(void)close(lock_fd);
	if (signals_blocked)
		(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	if (deferred_error != NULL)
		fprintf(stderr, "%s\n", deferred_error);
	if (result == 0)
		puts(options->kind == TEST_ROW ?
		     "Secondary-LCD row write completed." :
		     "Secondary-LCD pattern write completed.");
	return result;
}

int main(int argc, char **argv)
{
	struct options options;
	int parsed = parse_options(argc, argv, &options);

	if (parsed > 0) {
		usage(stdout);
		return 0;
	}
	if (parsed < 0) {
		usage(stderr);
		return 2;
	}
	if (options.kind == TEST_INFO)
		return show_info();
	if (options.kind == TEST_INFO_LIVE)
		return show_live_info();
	return perform_write(&options);
}

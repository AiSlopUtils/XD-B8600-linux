#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
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
#include <sys/types.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xtest.h>

#ifndef __sh__
#error "subpad-mouse must be built for SuperH"
#endif
#ifndef __SH4A__
#error "subpad-mouse must be built for SH-4A"
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "subpad-mouse must be built big-endian"
#endif

#define PFC_PHYS UINT32_C(0x04050000)
#define CPG_PHYS UINT32_C(0x04150000)
#define ADC_PHYS UINT32_C(0x04610000)

#define PGCR_OFF UINT32_C(0x10c)
#define PHCR_OFF UINT32_C(0x10e)
#define PLCR_OFF UINT32_C(0x114)
#define PGDR_OFF UINT32_C(0x12c)
#define PHDR_OFF UINT32_C(0x12e)
#define PLDR_OFF UINT32_C(0x134)
#define PSELE_OFF UINT32_C(0x156)
#define MSELCRA_OFF UINT32_C(0x180)
#define PFC_A_OFF UINT32_C(0x1d6)

#define FRQCRA_OFF UINT32_C(0x000)
#define MSTPCR2_OFF UINT32_C(0x038)
#define ADC_CLOCK_BIT UINT32_C(0x08000000)

#define ADDRA_OFF UINT32_C(0x080)
#define ADDRB_OFF UINT32_C(0x082)
#define ADDRC_OFF UINT32_C(0x084)
#define ADDRD_OFF UINT32_C(0x086)
#define ADCSR_OFF UINT32_C(0x088)
#define ADCCSR_OFF UINT32_C(0x08a)
#define ADCUST_OFF UINT32_C(0x08c)
#define ADPCTL_OFF UINT32_C(0x08e)

#define SUBPAD_PARAMETER "/sys/module/exword_keypad/parameters/subpad_mode"
#define SUBPAD_LOCK "/tmp/exword-subpad.lock"
#define SETTLE_LOOPS 16384U
#define ADC_TIMEOUT 8192U
#define YIELD_TURNS 1U
#define MOVE_STEP 6
#define MOVE_REPEAT_POLLS 3U
#define DEBOUNCE_POLLS 2U
#define RELEASE_POLLS 2U
#define RAW_JITTER_LIMIT 96U

/* Measured on this XD-B8600: 48,139 at top-left and 950,904 bottom-right. */
#define RAW_X_FIRST 349U
#define RAW_X_SECOND 650U
#define RAW_Y_SPLIT 522U

enum zone {
	ZONE_NONE,
	ZONE_LEFT,
	ZONE_UP,
	ZONE_RIGHT,
	ZONE_BUTTON_LEFT,
	ZONE_DOWN,
	ZONE_BUTTON_RIGHT,
};

enum adc_result {
	ADC_SAMPLE_VALID,
	ADC_SAMPLE_INVALID,
	ADC_FATAL_CLEAN,
	ADC_FATAL_DIRTY,
};

struct maps {
	int fd;
	size_t page_size;
	void *pfc;
	void *cpg;
	void *adc;
};

struct pfc_snapshot {
	uint16_t pgcr;
	uint16_t phcr;
	uint16_t plcr;
	uint16_t psele;
	uint16_t mselcra;
	uint16_t pfc_a;
	uint8_t pgdr;
	uint8_t phdr;
};

struct sample {
	uint16_t x;
	uint16_t y;
};

struct x_output {
	xcb_connection_t *connection;
};

static volatile sig_atomic_t stop_requested;
static sigset_t stopping_signals;

static inline void io_barrier(void)
{
	__asm__ __volatile__("synco" ::: "memory");
}

static void settle(void)
{
	unsigned int pass;

	for (pass = 0; pass < SETTLE_LOOPS; pass++)
		__asm__ __volatile__("nop" ::: "memory");
}

static volatile uint8_t *reg8(void *page, uintptr_t offset)
{
	return (volatile uint8_t *)page + offset;
}

static volatile uint16_t *reg16(void *page, uintptr_t offset)
{
	return (volatile uint16_t *)((volatile uint8_t *)page + offset);
}

static volatile uint32_t *reg32(void *page, uintptr_t offset)
{
	return (volatile uint32_t *)((volatile uint8_t *)page + offset);
}

static void close_maps(struct maps *maps)
{
	if (maps->adc != MAP_FAILED)
		(void)munmap(maps->adc, maps->page_size);
	if (maps->cpg != MAP_FAILED)
		(void)munmap(maps->cpg, maps->page_size);
	if (maps->pfc != MAP_FAILED)
		(void)munmap(maps->pfc, maps->page_size);
	if (maps->fd >= 0)
		(void)close(maps->fd);
	maps->fd = -1;
	maps->pfc = MAP_FAILED;
	maps->cpg = MAP_FAILED;
	maps->adc = MAP_FAILED;
}

static int map_page(struct maps *maps, void **destination, off_t physical)
{
	*destination = mmap(NULL, maps->page_size, PROT_READ | PROT_WRITE,
			    MAP_SHARED, maps->fd, physical);
	if (*destination == MAP_FAILED) {
		fprintf(stderr, "subpad-mouse: mmap 0x%08lx failed: %s\n",
			(unsigned long)physical, strerror(errno));
		return -1;
	}
	return 0;
}

static int open_maps(struct maps *maps)
{
	struct stat status;
	long page_size;

	maps->fd = -1;
	maps->page_size = 0;
	maps->pfc = MAP_FAILED;
	maps->cpg = MAP_FAILED;
	maps->adc = MAP_FAILED;
	page_size = sysconf(_SC_PAGESIZE);
	if (page_size != 4096) {
		fprintf(stderr, "subpad-mouse: unexpected page size %ld\n",
			page_size);
		return -1;
	}
	maps->page_size = (size_t)page_size;
	if (stat("/dev/mem", &status) < 0 || !S_ISCHR(status.st_mode)) {
		fputs("subpad-mouse: /dev/mem is unavailable\n", stderr);
		return -1;
	}
	maps->fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
	if (maps->fd < 0) {
		fprintf(stderr, "subpad-mouse: cannot open /dev/mem: %s\n",
			strerror(errno));
		return -1;
	}
	if (map_page(maps, &maps->pfc, (off_t)PFC_PHYS) < 0 ||
	    map_page(maps, &maps->cpg, (off_t)CPG_PHYS) < 0 ||
	    map_page(maps, &maps->adc, (off_t)ADC_PHYS) < 0) {
		close_maps(maps);
		return -1;
	}
	return 0;
}

static bool expected_hardware(void)
{
	struct fb_fix_screeninfo fixed;
	struct fb_var_screeninfo variable;
	int fd = open("/dev/fb0", O_RDONLY | O_CLOEXEC);
	bool matches = false;

	if (fd >= 0 && ioctl(fd, FBIOGET_FSCREENINFO, &fixed) == 0 &&
	    ioctl(fd, FBIOGET_VSCREENINFO, &variable) == 0 &&
	    strncmp(fixed.id, "EX-word LCD", sizeof(fixed.id)) == 0 &&
	    variable.xres == 528U && variable.yres == 320U &&
	    variable.bits_per_pixel == 16U)
		matches = true;
	if (fd >= 0)
		(void)close(fd);
	return matches;
}

static bool experimental_driver_absent(void)
{
	struct stat status;

	if (stat(SUBPAD_PARAMETER, &status) == 0)
		return false;
	return errno == ENOENT;
}

static int acquire_lock(void)
{
	int fd = open(SUBPAD_LOCK, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

	if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr, "subpad-mouse: secondary hardware is busy: %s\n",
			strerror(errno));
		if (fd >= 0)
			(void)close(fd);
		return -1;
	}
	return fd;
}

static void stop_handler(int signal_number)
{
	(void)signal_number;
	stop_requested = 1;
}

static int configure_signals(void)
{
	static const int synchronous[] = {
		SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGTRAP, SIGSYS,
	};
	static const int stopping[] = { SIGTERM, SIGINT, SIGHUP, SIGQUIT };
	struct sigaction action;
	sigset_t blocked;
	unsigned int index;

	if (sigfillset(&blocked) < 0)
		return -1;
	if (sigemptyset(&stopping_signals) < 0)
		return -1;
	for (index = 0; index < sizeof(synchronous) / sizeof(synchronous[0]);
	     index++) {
		if (sigdelset(&blocked, synchronous[index]) < 0)
			return -1;
	}
	for (index = 0; index < sizeof(stopping) / sizeof(stopping[0]); index++) {
		if (sigaddset(&stopping_signals, stopping[index]) < 0)
			return -1;
		if (sigdelset(&blocked, stopping[index]) < 0)
			return -1;
	}
	if (sigprocmask(SIG_SETMASK, &blocked, NULL) < 0)
		return -1;
	memset(&action, 0, sizeof(action));
	action.sa_handler = stop_handler;
	if (sigemptyset(&action.sa_mask) < 0)
		return -1;
	for (index = 0; index < sizeof(stopping) / sizeof(stopping[0]); index++) {
		if (sigaction(stopping[index], &action, NULL) < 0)
			return -1;
	}
	return 0;
}

static int block_stopping_signals(sigset_t *old_set)
{
	return sigprocmask(SIG_BLOCK, &stopping_signals, old_set);
}

static int restore_signal_mask(const sigset_t *old_set)
{
	return sigprocmask(SIG_SETMASK, old_set, NULL);
}

static void rmw8(volatile uint8_t *reg, uint8_t mask, uint8_t value)
{
	uint8_t current = *reg;

	*reg = (uint8_t)((current & (uint8_t)~mask) | (value & mask));
}

static void rmw16(volatile uint16_t *reg, uint16_t mask, uint16_t value)
{
	uint16_t current = *reg;

	*reg = (uint16_t)((current & (uint16_t)~mask) | (value & mask));
}

static void capture_pfc(struct maps *maps, struct pfc_snapshot *snapshot)
{
	snapshot->pgcr = *reg16(maps->pfc, PGCR_OFF);
	snapshot->phcr = *reg16(maps->pfc, PHCR_OFF);
	snapshot->plcr = *reg16(maps->pfc, PLCR_OFF);
	snapshot->pgdr = *reg8(maps->pfc, PGDR_OFF);
	snapshot->phdr = *reg8(maps->pfc, PHDR_OFF);
	snapshot->psele = *reg16(maps->pfc, PSELE_OFF);
	snapshot->mselcra = *reg16(maps->pfc, MSELCRA_OFF);
	snapshot->pfc_a = *reg16(maps->pfc, PFC_A_OFF);
	io_barrier();
}

static void base_pfc(struct maps *maps)
{
	*reg8(maps->pfc, PGDR_OFF) = UINT8_C(0);
	rmw16(reg16(maps->pfc, PHCR_OFF), UINT16_C(0x0300),
	      UINT16_C(0x0100));
	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c), UINT16_C(0));
	rmw16(reg16(maps->pfc, PSELE_OFF), UINT16_C(0x0c00), UINT16_C(0));
	io_barrier();
}

static bool wait_clear(volatile uint16_t *reg, uint16_t mask)
{
	unsigned int pass;

	for (pass = 0; pass < ADC_TIMEOUT; pass++) {
		if ((*reg & mask) == 0)
			return true;
		__asm__ __volatile__("nop" ::: "memory");
	}
	return false;
}

static bool emergency_abort_idle(struct maps *maps)
{
	volatile uint16_t *adccsr = reg16(maps->adc, ADCCSR_OFF);
	volatile uint16_t *adcsr = reg16(maps->adc, ADCSR_OFF);
	uint16_t value = *adccsr;

	*adccsr = (uint16_t)(value & (uint16_t)~UINT16_C(0xe000));
	io_barrier();
	if (!wait_clear(adccsr, UINT16_C(0x2000)))
		return false;
	*reg16(maps->adc, ADCUST_OFF) = UINT16_C(0);
	value = *adcsr;
	*adcsr = (uint16_t)(value & (uint16_t)~UINT16_C(0x2000));
	io_barrier();
	return wait_clear(adcsr, UINT16_C(0x2000));
}

static bool normal_rest(struct maps *maps, bool high)
{
	volatile uint16_t *adccsr = reg16(maps->adc, ADCCSR_OFF);
	volatile uint16_t *adcsr = reg16(maps->adc, ADCSR_OFF);
	uint16_t value;

	/* Exact firmware order: prove the custom unit idle before changing it. */
	if (!wait_clear(adccsr, UINT16_C(0x2000)))
		return false;
	*reg16(maps->adc, ADCUST_OFF) = UINT16_C(0);
	value = *adcsr;
	*adcsr = (uint16_t)(value & (uint16_t)~UINT16_C(0x2000));
	io_barrier();
	if (!wait_clear(adcsr, UINT16_C(0x2000)))
		return false;
	base_pfc(maps);
	*reg16(maps->pfc, PGCR_OFF) = UINT16_C(0xaaaa);
	*reg8(maps->pfc, PGDR_OFF) = UINT8_C(0);
	rmw8(reg8(maps->pfc, PHDR_OFF), UINT8_C(0x10),
	     high ? UINT8_C(0x10) : UINT8_C(0));
	io_barrier();
	settle();
	return true;
}

static bool read_pen(struct maps *maps)
{
	bool contact = true;
	unsigned int pass;

	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c),
	      UINT16_C(0x0008));
	io_barrier();
	for (pass = 0; pass < 5U; pass++) {
		if ((*reg8(maps->pfc, PLDR_OFF) & UINT8_C(0x02)) != 0)
			contact = false;
		__asm__ __volatile__("nop" ::: "memory");
	}
	base_pfc(maps);
	return contact;
}

static unsigned int small_abs(int value)
{
	return value < 0 ? (unsigned int)-value : (unsigned int)value;
}

static enum adc_result read_adc(struct maps *maps, struct sample *sample)
{
	volatile uint16_t *adccsr = reg16(maps->adc, ADCCSR_OFF);
	volatile uint16_t *adcsr = reg16(maps->adc, ADCSR_OFF);
	uint16_t b;
	uint16_t a;
	uint16_t d;
	uint16_t c;
	uint16_t base;
	uint16_t value;
	bool valid;
	int error;

	if (!normal_rest(maps, false))
		return ADC_FATAL_DIRTY;
	rmw16(reg16(maps->pfc, PFC_A_OFF), UINT16_C(0xf000),
	      UINT16_C(0x5000));
	*reg16(maps->pfc, PGCR_OFF) = UINT16_C(0);
	rmw16(reg16(maps->pfc, MSELCRA_OFF), UINT16_C(0x0300),
	      UINT16_C(0));
	io_barrier();
	if (!wait_clear(adcsr, UINT16_C(0x2000)))
		goto failed;
	*reg16(maps->adc, ADCUST_OFF) = UINT16_C(0x8000);
	base = ((*reg32(maps->cpg, FRQCRA_OFF) & UINT32_C(0x0f)) == 3U) ?
		UINT16_C(0x01cf) : UINT16_C(0x02cf);
	*adccsr = base;
	*reg16(maps->adc, ADPCTL_OFF) = UINT16_C(0x8000);
	io_barrier();
	*adccsr = (uint16_t)(base | UINT16_C(0x2000));
	io_barrier();
	if (!wait_clear(adccsr, UINT16_C(0x2000)))
		goto failed;
	b = (uint16_t)(*reg16(maps->adc, ADDRB_OFF) >> 6);
	a = (uint16_t)(*reg16(maps->adc, ADDRA_OFF) >> 6);
	d = (uint16_t)(*reg16(maps->adc, ADDRD_OFF) >> 6);
	c = (uint16_t)(*reg16(maps->adc, ADDRC_OFF) >> 6);
	value = *adccsr;
	*adccsr = (uint16_t)(value & (uint16_t)~UINT16_C(0xc000));
	io_barrier();
	error = 1023 - (int)b - (int)a;
	valid = small_abs(error) <= 64U;
	error = 1023 - (int)d - (int)c;
	valid = valid && small_abs(error) <= 64U;
	sample->x = (uint16_t)(((int)c - (int)d + 1023) >> 1);
	sample->y = (uint16_t)(((int)a - (int)b + 1023) >> 1);
	sample->x &= UINT16_C(1023);
	sample->y &= UINT16_C(1023);
	if (!normal_rest(maps, true))
		return ADC_FATAL_DIRTY;
	return valid ? ADC_SAMPLE_VALID : ADC_SAMPLE_INVALID;

failed:
	if (!emergency_abort_idle(maps))
		return ADC_FATAL_DIRTY;
	return normal_rest(maps, true) ? ADC_FATAL_CLEAN : ADC_FATAL_DIRTY;
}

static bool open_adc_clock(struct maps *maps, bool *was_gated)
{
	volatile uint32_t *mstpcr2 = reg32(maps->cpg, MSTPCR2_OFF);
	uint32_t current = *mstpcr2;

	*was_gated = (current & ADC_CLOCK_BIT) != 0;
	if (*was_gated) {
		current = *mstpcr2;
		*mstpcr2 = current & ~ADC_CLOCK_BIT;
		io_barrier();
		settle();
	}
	current = *mstpcr2;
	io_barrier();
	return (current & ADC_CLOCK_BIT) == 0;
}

static bool restore_adc_clock(struct maps *maps, bool was_gated)
{
	volatile uint32_t *mstpcr2 = reg32(maps->cpg, MSTPCR2_OFF);
	uint32_t current = *mstpcr2;

	if (was_gated)
		*mstpcr2 = current | ADC_CLOCK_BIT;
	io_barrier();
	current = *mstpcr2;
	io_barrier();
	return ((current & ADC_CLOCK_BIT) != 0) == was_gated;
}

static bool restore_pfc(struct maps *maps,
			const struct pfc_snapshot *snapshot)
{
	/* Caller has already proved ADC idle and established rest-high routing. */
	rmw16(reg16(maps->pfc, PFC_A_OFF), UINT16_C(0xf000),
	      snapshot->pfc_a);
	rmw16(reg16(maps->pfc, MSELCRA_OFF), UINT16_C(0x0300),
	      snapshot->mselcra);
	rmw16(reg16(maps->pfc, PSELE_OFF), UINT16_C(0x0c00), snapshot->psele);
	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c), snapshot->plcr);
	rmw16(reg16(maps->pfc, PHCR_OFF), UINT16_C(0x0300), snapshot->phcr);
	*reg8(maps->pfc, PGDR_OFF) = snapshot->pgdr;
	*reg16(maps->pfc, PGCR_OFF) = snapshot->pgcr;
	rmw8(reg8(maps->pfc, PHDR_OFF), UINT8_C(0x10), snapshot->phdr);
	io_barrier();
	return *reg16(maps->pfc, PGCR_OFF) == snapshot->pgcr &&
	       *reg8(maps->pfc, PGDR_OFF) == snapshot->pgdr &&
	       (*reg16(maps->pfc, PHCR_OFF) & UINT16_C(0x0300)) ==
		(snapshot->phcr & UINT16_C(0x0300)) &&
	       (*reg16(maps->pfc, PLCR_OFF) & UINT16_C(0x000c)) ==
		(snapshot->plcr & UINT16_C(0x000c)) &&
	       (*reg16(maps->pfc, PSELE_OFF) & UINT16_C(0x0c00)) ==
		(snapshot->psele & UINT16_C(0x0c00)) &&
	       (*reg16(maps->pfc, MSELCRA_OFF) & UINT16_C(0x0300)) ==
		(snapshot->mselcra & UINT16_C(0x0300)) &&
	       (*reg16(maps->pfc, PFC_A_OFF) & UINT16_C(0xf000)) ==
		(snapshot->pfc_a & UINT16_C(0xf000)) &&
	       (*reg8(maps->pfc, PHDR_OFF) & UINT8_C(0x10)) ==
		(snapshot->phdr & UINT8_C(0x10));
}

static enum zone sample_zone(const struct sample *sample)
{
	if (sample->y < RAW_Y_SPLIT) {
		if (sample->x < RAW_X_FIRST)
			return ZONE_LEFT;
		if (sample->x < RAW_X_SECOND)
			return ZONE_UP;
		return ZONE_RIGHT;
	}
	if (sample->x < RAW_X_FIRST)
		return ZONE_BUTTON_LEFT;
	if (sample->x < RAW_X_SECOND)
		return ZONE_DOWN;
	return ZONE_BUTTON_RIGHT;
}

static bool x_checked_button(struct x_output *output, uint8_t type,
			     uint8_t button)
{
	xcb_void_cookie_t cookie = xcb_test_fake_input_checked(
		output->connection, type, button, XCB_CURRENT_TIME, XCB_NONE,
		0, 0, 0);
	xcb_generic_error_t *error = xcb_request_check(output->connection,
						cookie);
	bool ok = error == NULL;

	if (error != NULL)
		fprintf(stderr,
			"subpad-mouse: X button request failed (error %u)\n",
			(unsigned int)error->error_code);

	free(error);
	return ok && xcb_connection_has_error(output->connection) == 0;
}

static bool open_x(struct x_output *output)
{
	xcb_test_get_version_reply_t *version;
	xcb_generic_error_t *error = NULL;
	const xcb_query_extension_reply_t *extension;
	int screen_number;
	unsigned int button;

	output->connection = xcb_connect(":0", &screen_number);
	(void)screen_number;
	if (output->connection == NULL ||
	    xcb_connection_has_error(output->connection) != 0)
		return false;
	extension = xcb_get_extension_data(output->connection, &xcb_test_id);
	if (extension == NULL || extension->present == 0)
		return false;
	version = xcb_test_get_version_reply(
		output->connection,
		xcb_test_get_version(output->connection, 2U, 2U), &error);
	if (version == NULL || error != NULL || version->major_version < 2U) {
		free(error);
		free(version);
		return false;
	}
	free(version);
	for (button = 1U; button <= 3U; button++) {
		if (!x_checked_button(output, XCB_BUTTON_RELEASE,
				      (uint8_t)button))
			return false;
	}
	return xcb_flush(output->connection) > 0;
}

static void close_x(struct x_output *output)
{
	if (output->connection != NULL)
		xcb_disconnect(output->connection);
	output->connection = NULL;
}

static bool emit_motion(struct x_output *output, int dx, int dy)
{
	xcb_void_cookie_t cookie = xcb_warp_pointer_checked(
		output->connection, XCB_NONE, XCB_NONE, 0, 0, 0, 0,
		(int16_t)dx, (int16_t)dy);
	xcb_generic_error_t *error = xcb_request_check(output->connection,
						       cookie);
	bool ok = error == NULL;

	if (error != NULL)
		fprintf(stderr,
			"subpad-mouse: X pointer motion failed (error %u)\n",
			(unsigned int)error->error_code);
	free(error);
	return ok && xcb_connection_has_error(output->connection) == 0;
}

static bool emit_button(struct x_output *output, unsigned int button,
			bool down)
{
	return x_checked_button(output,
				down ? XCB_BUTTON_PRESS : XCB_BUTTON_RELEASE,
				(uint8_t)button);
}

static const char *zone_name(enum zone zone)
{
	switch (zone) {
	case ZONE_LEFT: return "left";
	case ZONE_UP: return "up";
	case ZONE_RIGHT: return "right";
	case ZONE_BUTTON_LEFT: return "left-click";
	case ZONE_DOWN: return "down";
	case ZONE_BUTTON_RIGHT: return "right-click";
	default: return "none";
	}
}

static bool direction_zone(enum zone zone)
{
	return zone == ZONE_LEFT || zone == ZONE_UP || zone == ZONE_RIGHT ||
	       zone == ZONE_DOWN;
}

static bool emit_zone(struct x_output *output, enum zone zone)
{
	switch (zone) {
	case ZONE_LEFT: return emit_motion(output, -MOVE_STEP, 0);
	case ZONE_UP: return emit_motion(output, 0, -MOVE_STEP);
	case ZONE_RIGHT: return emit_motion(output, MOVE_STEP, 0);
	case ZONE_DOWN: return emit_motion(output, 0, MOVE_STEP);
	case ZONE_BUTTON_LEFT: return emit_button(output, 1U, true);
	case ZONE_BUTTON_RIGHT: return emit_button(output, 3U, true);
	default: return true;
	}
}

static bool release_zone(struct x_output *output, enum zone zone)
{
	if (zone == ZONE_BUTTON_LEFT)
		return emit_button(output, 1U, false);
	if (zone == ZONE_BUTTON_RIGHT)
		return emit_button(output, 3U, false);
	return true;
}

static void cadence(void)
{
	unsigned int pass;

	for (pass = 0; pass < YIELD_TURNS; pass++)
		(void)sched_yield();
}

int main(int argc, char **argv)
{
	struct pfc_snapshot snapshot;
	struct sample candidate_sample = { 0 };
	struct x_output output = { NULL };
	struct maps maps;
	enum zone candidate = ZONE_NONE;
	enum zone active = ZONE_NONE;
	unsigned int candidate_polls = 0U;
	unsigned int release_polls = 0U;
	unsigned int repeat_polls = 0U;
	bool clock_was_gated = false;
	bool hardware_safe = false;
	bool snapshot_valid = false;
	bool clock_opened = false;
	bool pfc_restored = false;
	bool clock_restored = false;
	bool fatal_error = false;
	bool stopping_blocked = false;
	sigset_t old_signal_mask;
	int lock_fd = -1;
	int result = 1;

	if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
			  strcmp(argv[1], "-h") == 0)) {
		puts("usage: subpad-mouse\nRuns the XD-B8600 six-button pad on X :0.");
		return 0;
	}
	if (argc != 1) {
		fputs("subpad-mouse: no arguments are accepted\n", stderr);
		return 2;
	}
	if (geteuid() != 0 || !expected_hardware() ||
	    !experimental_driver_absent()) {
		fputs("subpad-mouse: exact stable XD-B8600 environment not found\n",
		      stderr);
		return 1;
	}
	if (configure_signals() < 0) {
		fputs("subpad-mouse: cannot establish safe signal handling\n", stderr);
		return 1;
	}
	if (!open_x(&output)) {
		fputs("subpad-mouse: XTEST 2.x is unavailable on DISPLAY :0\n",
		      stderr);
		goto finished;
	}
	lock_fd = acquire_lock();
	if (lock_fd < 0)
		goto finished;
	if (open_maps(&maps) < 0)
		goto finished;
	if (block_stopping_signals(&old_signal_mask) < 0) {
		fputs("subpad-mouse: cannot protect the MMIO setup\n", stderr);
		goto close_mmio;
	}
	stopping_blocked = true;
	if (!experimental_driver_absent()) {
		fputs("subpad-mouse: kernel touch driver appeared; refusing MMIO\n",
		      stderr);
		goto close_mmio;
	}
	capture_pfc(&maps, &snapshot);
	snapshot_valid = true;
	if (!open_adc_clock(&maps, &clock_was_gated)) {
		if (!restore_adc_clock(&maps, clock_was_gated))
			fputs("subpad-mouse: ADC clock did not open or restore; reset advised\n",
			      stderr);
		else
			fputs("subpad-mouse: ADC clock did not open\n", stderr);
		goto close_mmio;
	}
	clock_opened = true;
	if (!normal_rest(&maps, true)) {
		fputs("subpad-mouse: ADC did not become idle; RESET REQUIRED\n",
		      stderr);
		goto close_mmio;
	}
	hardware_safe = true;
	if (restore_signal_mask(&old_signal_mask) < 0) {
		fputs("subpad-mouse: cannot restore signal mask\n", stderr);
		fatal_error = true;
		goto shutdown_hardware;
	}
	stopping_blocked = false;
	puts("subpad-mouse: ready (L/U/R, left-click/down/right-click)");

	while (stop_requested == 0 &&
	       xcb_connection_has_error(output.connection) == 0) {
		struct sample current = { 0 };
		enum adc_result adc;
		enum zone zone;
		bool valid = false;
		bool pen_down;

		if (block_stopping_signals(&old_signal_mask) < 0) {
			fatal_error = true;
			break;
		}
		stopping_blocked = true;
		pen_down = read_pen(&maps);
		adc = ADC_SAMPLE_INVALID;
		if (pen_down)
			adc = read_adc(&maps, &current);
		if (restore_signal_mask(&old_signal_mask) < 0) {
			fatal_error = true;
			break;
		}
		stopping_blocked = false;
		if (pen_down) {
			if (adc == ADC_SAMPLE_VALID) {
				valid = true;
			} else if (adc == ADC_FATAL_CLEAN) {
				fatal_error = true;
				break;
			} else if (adc == ADC_FATAL_DIRTY) {
				hardware_safe = false;
				fatal_error = true;
				break;
			}
		}

		if (!valid) {
			candidate = ZONE_NONE;
			candidate_polls = 0U;
			if (active != ZONE_NONE && ++release_polls >= RELEASE_POLLS) {
				if (!release_zone(&output, active))
					break;
				active = ZONE_NONE;
				release_polls = 0U;
				repeat_polls = 0U;
			}
			cadence();
			continue;
		}

		release_polls = 0U;
		zone = sample_zone(&current);
		if (active != ZONE_NONE) {
			if (direction_zone(active) &&
			    ++repeat_polls >= MOVE_REPEAT_POLLS) {
				repeat_polls = 0U;
				if (!emit_zone(&output, active))
					break;
			}
			cadence();
			continue;
		}

		if (zone != candidate ||
		    small_abs((int)current.x - (int)candidate_sample.x) >
			RAW_JITTER_LIMIT ||
		    small_abs((int)current.y - (int)candidate_sample.y) >
			RAW_JITTER_LIMIT) {
			candidate = zone;
			candidate_polls = 1U;
		} else {
			candidate_polls++;
		}
		candidate_sample = current;
		if (candidate_polls >= DEBOUNCE_POLLS) {
			active = zone;
			fprintf(stderr,
				"subpad-mouse: touch x=%u y=%u zone=%s\n",
				(unsigned int)current.x, (unsigned int)current.y,
				zone_name(active));
			candidate = ZONE_NONE;
			candidate_polls = 0U;
			repeat_polls = 0U;
			if (!emit_zone(&output, active))
				break;
		}
		cadence();
	}
	if (stop_requested == 0)
		fatal_error = true;

shutdown_hardware:
	if (active != ZONE_NONE)
		(void)release_zone(&output, active);
	if (hardware_safe) {
		if (!stopping_blocked) {
			if (block_stopping_signals(&old_signal_mask) < 0)
				hardware_safe = false;
			else
				stopping_blocked = true;
		}
		if (hardware_safe) {
			if (!normal_rest(&maps, true)) {
				hardware_safe = false;
			} else {
				pfc_restored = restore_pfc(&maps, &snapshot);
				if (!pfc_restored) {
					hardware_safe = false;
				} else {
					clock_restored = restore_adc_clock(
						&maps, clock_was_gated);
				}
			}
		}
		if (stopping_blocked) {
			(void)restore_signal_mask(&old_signal_mask);
			stopping_blocked = false;
		}
	}
	if (!hardware_safe) {
		fputs("subpad-mouse: ADC remained active; RESET REQUIRED\n", stderr);
	} else if (!pfc_restored || !clock_restored) {
		fputs("subpad-mouse: hardware restoration failed; reset advised\n",
		      stderr);
	} else if (!fatal_error) {
		result = 0;
	}

close_mmio:
	if (stopping_blocked) {
		(void)restore_signal_mask(&old_signal_mask);
		stopping_blocked = false;
	}
	if (snapshot_valid && clock_opened && !hardware_safe)
		fputs("subpad-mouse: unsafe state left untouched for reset\n", stderr);
	close_maps(&maps);
finished:
	if (lock_fd >= 0)
		(void)close(lock_fd);
	close_x(&output);
	return result;
}

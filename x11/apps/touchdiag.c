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
#error "touchdiag must be built for SuperH"
#endif
#ifndef __SH4A__
#error "touchdiag must be built for SH-4A"
#endif
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#error "touchdiag must be built big-endian"
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
#define TOUCH_LOCK "/tmp/exword-subpad.lock"
#define SETTLE_LOOPS 16384U
#define ADC_TIMEOUT 65536U

#define MAP_PFC 1U
#define MAP_CPG 2U
#define MAP_ADC 4U

enum command {
	CMD_INFO,
	CMD_PFC_LIVE,
	CMD_PEN,
	CMD_CLOCK_LIVE,
	CMD_CLOCK_TEST,
	CMD_ADC_ONCE,
};

enum adc_status {
	ADC_OK,
	ADC_NO_CONTACT,
	ADC_CLOCK_FAILED,
	ADC_IDLE_TIMEOUT,
	ADC_GENERIC_TIMEOUT,
	ADC_CONVERSION_TIMEOUT,
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
	uint16_t b;
	uint16_t a;
	uint16_t d;
	uint16_t c;
	uint16_t x;
	uint16_t y;
	bool valid;
};

static void usage(FILE *stream)
{
	fputs(
		"XD-B8600 secondary touch diagnostic\n\n"
		"Usage:\n"
		"  touchdiag info\n"
		"  touchdiag pfc --live\n"
		"  touchdiag pen --yes\n"
		"  touchdiag clock --live\n"
		"  touchdiag clock-on --yes\n"
		"  touchdiag adc-once --yes\n\n"
		"Run only on an XD-B8600 text console before startx. Test pen first.\n"
		"ADC access is experimental and may require a hardware reset.\n",
		stream);
}

static int parse_command(int argc, char **argv, enum command *command)
{
	if (argc == 1 || (argc == 2 && strcmp(argv[1], "info") == 0)) {
		*command = CMD_INFO;
		return 0;
	}
	if (argc == 2 && (strcmp(argv[1], "--help") == 0 ||
			  strcmp(argv[1], "-h") == 0))
		return 1;
	if (argc == 3 && strcmp(argv[1], "pfc") == 0 &&
	    strcmp(argv[2], "--live") == 0) {
		*command = CMD_PFC_LIVE;
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "pen") == 0 &&
	    strcmp(argv[2], "--yes") == 0) {
		*command = CMD_PEN;
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "clock") == 0 &&
	    strcmp(argv[2], "--live") == 0) {
		*command = CMD_CLOCK_LIVE;
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "clock-on") == 0 &&
	    strcmp(argv[2], "--yes") == 0) {
		*command = CMD_CLOCK_TEST;
		return 0;
	}
	if (argc == 3 && strcmp(argv[1], "adc-once") == 0 &&
	    strcmp(argv[2], "--yes") == 0) {
		*command = CMD_ADC_ONCE;
		return 0;
	}
	return -1;
}

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

static int map_page(struct maps *maps, void **destination, off_t physical,
		    int protection)
{
	*destination = mmap(NULL, maps->page_size, protection, MAP_SHARED,
			    maps->fd, physical);
	if (*destination == MAP_FAILED) {
		fprintf(stderr, "touchdiag: mmap 0x%08lx failed: %s\n",
			(unsigned long)physical, strerror(errno));
		return -1;
	}
	return 0;
}

static int open_maps(struct maps *maps, unsigned int wanted, bool writable)
{
	struct stat status;
	long page_size;
	int protection;

	maps->fd = -1;
	maps->page_size = 0;
	maps->pfc = MAP_FAILED;
	maps->cpg = MAP_FAILED;
	maps->adc = MAP_FAILED;

	page_size = sysconf(_SC_PAGESIZE);
	if (page_size != 4096) {
		fprintf(stderr, "touchdiag: expected 4096-byte pages, got %ld\n",
			page_size);
		return -1;
	}
	maps->page_size = (size_t)page_size;
	if (stat("/dev/mem", &status) < 0 || !S_ISCHR(status.st_mode)) {
		fputs("touchdiag: /dev/mem is unavailable\n", stderr);
		return -1;
	}
	maps->fd = open("/dev/mem", (writable ? O_RDWR : O_RDONLY) | O_SYNC);
	if (maps->fd < 0) {
		fprintf(stderr, "touchdiag: cannot open /dev/mem: %s\n",
			strerror(errno));
		return -1;
	}
	protection = PROT_READ | (writable ? PROT_WRITE : 0);
	if ((wanted & MAP_PFC) != 0 &&
	    map_page(maps, &maps->pfc, (off_t)PFC_PHYS, protection) < 0)
		goto failed;
	if ((wanted & MAP_CPG) != 0 &&
	    map_page(maps, &maps->cpg, (off_t)CPG_PHYS, protection) < 0)
		goto failed;
	if ((wanted & MAP_ADC) != 0 &&
	    map_page(maps, &maps->adc, (off_t)ADC_PHYS, protection) < 0)
		goto failed;
	return 0;

failed:
	close_maps(maps);
	return -1;
}

static int block_async_signals(sigset_t *old_set)
{
	static const int synchronous_fault_signals[] = {
		SIGBUS, SIGSEGV, SIGILL, SIGFPE, SIGTRAP, SIGSYS,
	};
	sigset_t blocked;
	unsigned int index;

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

static bool experimental_driver_absent(void)
{
	struct stat status;

	if (stat(SUBPAD_PARAMETER, &status) == 0)
		return false;
	return errno == ENOENT;
}

static bool expected_hardware(void)
{
	struct fb_fix_screeninfo fixed;
	struct fb_var_screeninfo variable;
	int fd = open("/dev/fb0", O_RDONLY);
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

static bool active_text_vt(void)
{
	struct stat status;
	struct vt_stat state;
	int mode;

	if (!isatty(STDIN_FILENO) ||
	    ioctl(STDIN_FILENO, KDGETMODE, &mode) < 0 || mode != KD_TEXT ||
	    fstat(STDIN_FILENO, &status) < 0 || !S_ISCHR(status.st_mode) ||
	    major(status.st_rdev) != 4U || minor(status.st_rdev) == 0U ||
	    ioctl(STDIN_FILENO, VT_GETSTATE, &state) < 0)
		return false;
	return minor(status.st_rdev) == (unsigned int)state.v_active;
}

static int preflight_write(void)
{
	if (geteuid() != 0) {
		fputs("touchdiag: must run as root\n", stderr);
		return -1;
	}
	if (!expected_hardware()) {
		fputs("touchdiag: expected XD-B8600 framebuffer was not found\n",
		      stderr);
		return -1;
	}
	if (!active_text_vt()) {
		fputs("touchdiag: use the active text console before startx\n", stderr);
		return -1;
	}
	if (!experimental_driver_absent()) {
		fputs("touchdiag: experimental kernel touch driver exists; refusing MMIO\n",
		      stderr);
		return -1;
	}
	return 0;
}

static int acquire_lock(void)
{
	int fd = open(TOUCH_LOCK, O_RDWR | O_CREAT, 0600);

	if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) < 0) {
		fprintf(stderr, "touchdiag: touch hardware is busy: %s\n",
			strerror(errno));
		if (fd >= 0)
			(void)close(fd);
		return -1;
	}
	return fd;
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
	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c),
	      UINT16_C(0));
	rmw16(reg16(maps->pfc, PSELE_OFF), UINT16_C(0x0c00),
	      UINT16_C(0));
	io_barrier();
}

static void force_rest_high(struct maps *maps)
{
	base_pfc(maps);
	*reg16(maps->pfc, PGCR_OFF) = UINT16_C(0xaaaa);
	*reg8(maps->pfc, PGDR_OFF) = UINT8_C(0);
	rmw8(reg8(maps->pfc, PHDR_OFF), UINT8_C(0x10), UINT8_C(0x10));
	io_barrier();
	settle();
}

static bool read_pen(struct maps *maps, uint8_t *pldr_value)
{
	bool contact = true;
	uint8_t observed = UINT8_C(0);
	unsigned int pass;

	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c),
	      UINT16_C(0x0008));
	io_barrier();
	for (pass = 0; pass < 5U; pass++) {
		uint8_t value = *reg8(maps->pfc, PLDR_OFF);

		observed = (uint8_t)(observed | value);
		if ((value & UINT8_C(0x02)) != 0)
			contact = false;
		__asm__ __volatile__("nop" ::: "memory");
	}
	base_pfc(maps);
	*pldr_value = observed;
	return contact;
}

static bool restore_pfc(struct maps *maps,
			const struct pfc_snapshot *snapshot)
{
	/* First establish the electrically safe rest-high state. */
	force_rest_high(maps);
	rmw16(reg16(maps->pfc, PFC_A_OFF), UINT16_C(0xf000),
	      snapshot->pfc_a);
	rmw16(reg16(maps->pfc, MSELCRA_OFF), UINT16_C(0x0300),
	      snapshot->mselcra);
	rmw16(reg16(maps->pfc, PSELE_OFF), UINT16_C(0x0c00),
	      snapshot->psele);
	rmw16(reg16(maps->pfc, PLCR_OFF), UINT16_C(0x000c),
	      snapshot->plcr);
	rmw16(reg16(maps->pfc, PHCR_OFF), UINT16_C(0x0300),
	      snapshot->phcr);
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

static bool open_adc_clock(struct maps *maps, bool *was_gated,
			   uint32_t *after)
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
	*after = *mstpcr2;
	io_barrier();
	return (*after & ADC_CLOCK_BIT) == 0;
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

static bool adc_cleanup(struct maps *maps)
{
	volatile uint16_t *adccsr = reg16(maps->adc, ADCCSR_OFF);
	volatile uint16_t *adcsr = reg16(maps->adc, ADCSR_OFF);
	uint16_t value;

	value = *adccsr;
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

static unsigned int small_abs(int value)
{
	return value < 0 ? (unsigned int)-value : (unsigned int)value;
}

static enum adc_status adc_once(struct maps *maps, struct sample *sample,
				bool *clock_was_gated, uint32_t *clock_after)
{
	volatile uint16_t *adccsr;
	volatile uint16_t *adcsr;
	uint16_t base;
	uint16_t value;
	int error;

	if (!open_adc_clock(maps, clock_was_gated, clock_after))
		return ADC_CLOCK_FAILED;
	adccsr = reg16(maps->adc, ADCCSR_OFF); /* First ADC-page access. */
	adcsr = reg16(maps->adc, ADCSR_OFF);
	if (!wait_clear(adccsr, UINT16_C(0x2000)))
		return ADC_IDLE_TIMEOUT;

	*reg16(maps->adc, ADCUST_OFF) = UINT16_C(0);
	value = *adcsr;
	*adcsr = (uint16_t)(value & (uint16_t)~UINT16_C(0x2000));
	*reg16(maps->pfc, PGCR_OFF) = UINT16_C(0xaaaa);
	*reg8(maps->pfc, PGDR_OFF) = UINT8_C(0);
	rmw8(reg8(maps->pfc, PHDR_OFF), UINT8_C(0x10), UINT8_C(0));
	io_barrier();
	settle();

	rmw16(reg16(maps->pfc, PFC_A_OFF), UINT16_C(0xf000),
	      UINT16_C(0x5000));
	*reg16(maps->pfc, PGCR_OFF) = UINT16_C(0);
	rmw16(reg16(maps->pfc, MSELCRA_OFF), UINT16_C(0x0300),
	      UINT16_C(0));
	io_barrier();
	if (!wait_clear(adcsr, UINT16_C(0x2000)))
		return ADC_GENERIC_TIMEOUT;

	*reg16(maps->adc, ADCUST_OFF) = UINT16_C(0x8000);
	base = ((*reg32(maps->cpg, FRQCRA_OFF) & UINT32_C(0x0f)) == 3U) ?
		UINT16_C(0x01cf) : UINT16_C(0x02cf);
	*adccsr = base;
	*reg16(maps->adc, ADPCTL_OFF) = UINT16_C(0x8000);
	io_barrier();
	*adccsr = (uint16_t)(base | UINT16_C(0x2000));
	io_barrier();
	if (!wait_clear(adccsr, UINT16_C(0x2000)))
		return ADC_CONVERSION_TIMEOUT;

	sample->b = (uint16_t)(*reg16(maps->adc, ADDRB_OFF) >> 6);
	sample->a = (uint16_t)(*reg16(maps->adc, ADDRA_OFF) >> 6);
	sample->d = (uint16_t)(*reg16(maps->adc, ADDRD_OFF) >> 6);
	sample->c = (uint16_t)(*reg16(maps->adc, ADDRC_OFF) >> 6);
	value = *adccsr;
	*adccsr = (uint16_t)(value & (uint16_t)~UINT16_C(0xc000));
	io_barrier();
	error = 1023 - (int)sample->b - (int)sample->a;
	sample->valid = small_abs(error) <= 64U;
	error = 1023 - (int)sample->d - (int)sample->c;
	sample->valid = sample->valid && small_abs(error) <= 64U;
	sample->x = (uint16_t)(((int)sample->c - (int)sample->d + 1023) >> 1);
	sample->y = (uint16_t)(((int)sample->a - (int)sample->b + 1023) >> 1);
	sample->x &= UINT16_C(1023);
	sample->y &= UINT16_C(1023);
	return ADC_OK;
}

static int show_info(void)
{
	puts("XD-B8600 secondary touch diagnostic");
	puts("Target: big-endian Renesas SH-4A; XD-B8600 only");
	printf("PFC page: 0x%08x  CPG page: 0x%08x  ADC page: 0x%08x\n",
	       PFC_PHYS, CPG_PHYS, ADC_PHYS);
	puts("Factory touch calibration is NOT retained by this exact kernel.");
	puts("No MMIO was performed. Next: touchdiag pfc --live");
	return 0;
}

static int show_pfc(void)
{
	struct maps maps;
	struct pfc_snapshot snapshot;
	uint8_t pldr;

	if (preflight_write() < 0 ||
	    open_maps(&maps, MAP_PFC, false) < 0)
		return 1;
	capture_pfc(&maps, &snapshot);
	pldr = *reg8(maps.pfc, PLDR_OFF);
	io_barrier();
	close_maps(&maps);
	printf("PGCR=%04x PHCR=%04x PLCR=%04x PGDR=%02x PHDR=%02x PLDR=%02x\n",
	       (unsigned int)snapshot.pgcr, (unsigned int)snapshot.phcr,
	       (unsigned int)snapshot.plcr, (unsigned int)snapshot.pgdr,
	       (unsigned int)snapshot.phdr, (unsigned int)pldr);
	printf("PSELE=%04x MSELCRA=%04x PFC_A=%04x (read-only snapshot)\n",
	       (unsigned int)snapshot.psele, (unsigned int)snapshot.mselcra,
	       (unsigned int)snapshot.pfc_a);
	return 0;
}

static int show_clock(void)
{
	struct maps maps;
	uint32_t frqcra;
	uint32_t mstpcr2;

	if (preflight_write() < 0 ||
	    open_maps(&maps, MAP_CPG, false) < 0)
		return 1;
	frqcra = *reg32(maps.cpg, FRQCRA_OFF);
	mstpcr2 = *reg32(maps.cpg, MSTPCR2_OFF);
	io_barrier();
	close_maps(&maps);
	printf("FRQCRA=0x%08x MSTPCR2=0x%08x ADC-clock-bit27=%s\n",
	       frqcra, mstpcr2,
	       (mstpcr2 & ADC_CLOCK_BIT) != 0 ? "stopped" : "running");
	puts("Read-only CPG snapshot; no clock was changed.");
	return 0;
}

static int run_pen(void)
{
	struct pfc_snapshot snapshot;
	struct maps maps;
	sigset_t old_set;
	uint8_t pldr = UINT8_C(0xff);
	bool signals_blocked = false;
	bool pfc_restored;
	bool contact;
	int lock_fd;

	if (preflight_write() < 0)
		return 1;
	lock_fd = acquire_lock();
	if (lock_fd < 0)
		return 1;
	if (open_maps(&maps, MAP_PFC, true) < 0)
		goto failed_lock;
	puts("PFC-only pen check: no ADC or clock access. Hold the panel if testing DOWN.");
	if (fflush(NULL) != 0 || block_async_signals(&old_set) < 0)
		goto failed_maps;
	signals_blocked = true;
	if (!experimental_driver_absent()) {
		fputs("touchdiag: kernel touch driver appeared; refusing MMIO\n",
		      stderr);
		goto failed_maps;
	}
	capture_pfc(&maps, &snapshot);
	force_rest_high(&maps);
	contact = read_pen(&maps, &pldr);
	pfc_restored = restore_pfc(&maps, &snapshot);
	close_maps(&maps);
	(void)close(lock_fd);
	(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	printf("PLDR-or=0x%02x bit1=%u PEN=%s restore=%s\n",
	       (unsigned int)pldr, (unsigned int)((pldr >> 1) & 1U),
	       contact ? "DOWN" : "UP", pfc_restored ? "OK" : "FAILED");
	return pfc_restored ? 0 : 1;

failed_maps:
	close_maps(&maps);
	(void)close(lock_fd);
	if (signals_blocked)
		(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	return 1;
failed_lock:
	(void)close(lock_fd);
	return 1;
}

static int run_clock_test(void)
{
	struct maps maps;
	sigset_t old_set;
	uint32_t initial;
	uint32_t after = UINT32_C(0);
	uint32_t restored_value;
	bool was_gated;
	bool opened;
	bool restore_ok;
	bool signals_blocked = false;
	int lock_fd;

	if (preflight_write() < 0)
		return 1;
	lock_fd = acquire_lock();
	if (lock_fd < 0)
		return 1;
	if (open_maps(&maps, MAP_CPG, true) < 0)
		goto failed_lock;
	puts("Testing only MSTPCR2 bit 27; the original clock state will be restored.");
	if (fflush(NULL) != 0 || block_async_signals(&old_set) < 0)
		goto failed_maps;
	signals_blocked = true;
	if (!experimental_driver_absent()) {
		fputs("touchdiag: kernel touch driver appeared; refusing MMIO\n",
		      stderr);
		goto failed_maps;
	}
	initial = *reg32(maps.cpg, MSTPCR2_OFF);
	opened = open_adc_clock(&maps, &was_gated, &after);
	restore_ok = restore_adc_clock(&maps, was_gated);
	restored_value = *reg32(maps.cpg, MSTPCR2_OFF);
	io_barrier();
	close_maps(&maps);
	(void)close(lock_fd);
	(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	printf("MSTPCR2 initial=%08x opened=%08x restored=%08x result=%s\n",
	       initial, after, restored_value,
	       opened && restore_ok ? "OK" : "FAILED");
	return opened && restore_ok ? 0 : 1;

failed_maps:
	close_maps(&maps);
	(void)close(lock_fd);
	if (signals_blocked)
		(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	return 1;
failed_lock:
	(void)close(lock_fd);
	return 1;
}

static const char *adc_status_name(enum adc_status status)
{
	switch (status) {
	case ADC_OK: return "conversion completed";
	case ADC_NO_CONTACT: return "no pen contact; ADC was not accessed";
	case ADC_CLOCK_FAILED: return "ADC clock did not open";
	case ADC_IDLE_TIMEOUT: return "ADCCSR was busy before conversion";
	case ADC_GENERIC_TIMEOUT: return "ADCSR stayed busy";
	case ADC_CONVERSION_TIMEOUT: return "ADCCSR conversion timeout";
	default: return "unknown failure";
	}
}

static int run_adc(void)
{
	struct pfc_snapshot snapshot;
	struct sample sample = { 0 };
	struct maps maps;
	sigset_t old_set;
	uint32_t clock_after = UINT32_C(0);
	uint8_t pldr = UINT8_C(0xff);
	bool clock_was_gated = false;
	bool clock_attempted = false;
	bool clock_restored = true;
	bool adc_accessed = false;
	bool cleanup_ok = true;
	bool pfc_restored = false;
	bool signals_blocked = false;
	bool contact;
	enum adc_status status;
	int lock_fd;

	if (preflight_write() < 0)
		return 1;
	lock_fd = acquire_lock();
	if (lock_fd < 0)
		return 1;
	if (open_maps(&maps, MAP_PFC | MAP_CPG | MAP_ADC, true) < 0)
		goto failed_lock;
	puts("Hold the secondary panel now. If contact is detected, one ADC access follows.");
	puts("If the machine freezes during ADC MMIO, reset it; no boot files are changed.");
	if (fflush(NULL) != 0 || block_async_signals(&old_set) < 0)
		goto failed_maps;
	signals_blocked = true;
	if (!experimental_driver_absent()) {
		fputs("touchdiag: kernel touch driver appeared; refusing MMIO\n",
		      stderr);
		goto failed_maps;
	}
	capture_pfc(&maps, &snapshot);
	force_rest_high(&maps);
	contact = read_pen(&maps, &pldr);
	if (!contact) {
		status = ADC_NO_CONTACT;
	} else {
		clock_attempted = true;
		status = adc_once(&maps, &sample, &clock_was_gated, &clock_after);
		adc_accessed = status != ADC_CLOCK_FAILED;
		if (adc_accessed)
			cleanup_ok = adc_cleanup(&maps);
	}
	if (cleanup_ok) {
		force_rest_high(&maps);
		pfc_restored = restore_pfc(&maps, &snapshot);
		if (clock_attempted)
			clock_restored = restore_adc_clock(&maps, clock_was_gated);
	}
	close_maps(&maps);
	(void)close(lock_fd);
	(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	printf("PEN=%s PLDR-or=0x%02x: %s\n", contact ? "DOWN" : "UP",
	       (unsigned int)pldr, adc_status_name(status));
	printf("cleanup=%s pfc-restore=%s clock-restore=%s\n",
	       adc_accessed ? (cleanup_ok ? "OK" : "FAILED") : "not-needed",
	       cleanup_ok ? (pfc_restored ? "OK" : "FAILED") : "SKIPPED",
	       clock_attempted ?
		(cleanup_ok ? (clock_restored ? "OK" : "FAILED") :
		 "left-running") : "not-needed");
	if (!cleanup_ok)
		puts("RESET REQUIRED: ADC did not become idle; its active routing and clock were deliberately left in place.");
	if (status == ADC_OK) {
		printf("B=%u A=%u D=%u C=%u raw-x=%u raw-y=%u pairs=%s clock=%08x\n",
		       (unsigned int)sample.b, (unsigned int)sample.a,
		       (unsigned int)sample.d, (unsigned int)sample.c,
		       (unsigned int)sample.x, (unsigned int)sample.y,
		       sample.valid ? "valid" : "INVALID", clock_after);
	}
	return status == ADC_OK && cleanup_ok && pfc_restored &&
	       clock_restored ? 0 : 1;

failed_maps:
	close_maps(&maps);
	(void)close(lock_fd);
	if (signals_blocked)
		(void)sigprocmask(SIG_SETMASK, &old_set, NULL);
	return 1;
failed_lock:
	(void)close(lock_fd);
	return 1;
}

int main(int argc, char **argv)
{
	enum command command;
	int parsed = parse_command(argc, argv, &command);

	if (parsed > 0) {
		usage(stdout);
		return 0;
	}
	if (parsed < 0) {
		usage(stderr);
		return 2;
	}
	switch (command) {
	case CMD_INFO: return show_info();
	case CMD_PFC_LIVE: return show_pfc();
	case CMD_PEN: return run_pen();
	case CMD_CLOCK_LIVE: return show_clock();
	case CMD_CLOCK_TEST: return run_clock_test();
	case CMD_ADC_ONCE: return run_adc();
	default: return 2;
	}
}

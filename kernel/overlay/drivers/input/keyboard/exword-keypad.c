// SPDX-License-Identifier: GPL-2.0
/* Keyboard matrix support for Casio EX-word DATAPLUS 6 dictionaries. */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>

#define EXWORD_KEYPAD_BASE	0xa44b0000UL
#define EXWORD_MSTPCR0		0xa4150030UL
#define EXWORD_KEYSC_MSTP_BIT	6
#define EXWORD_SNAPSHOT_BASE	0xac252c00UL
#define EXWORD_SNAPSHOT_MAGIC	0x45584b53UL
#define EXWORD_TOUCH_SNAPSHOT_MAGIC 0x45585453UL
#define EXWORD_TOUCH_SNAPSHOT_OFFSET 0x28
#define EXWORD_TOUCH_VARIANT_OFFSET 0x2c
#define EXWORD_TOUCH_CAL1_OFFSET 0x30
#define EXWORD_TOUCH_CAL2_OFFSET 0x40
#define EXWORD_PRIME_READS	65536
#define EXWORD_POLL_BURST	256
#define EXWORD_MOUSE_TOGGLE_SCAN	10
#define EXWORD_MOUSE_REPEAT_SCANS 16384
#define EXWORD_MOUSE_STEP	4

/* Firmware-initialized secondary LCD (R61580-family controller). */
#define EXWORD_SUBLCD_DATA	0xb8000000UL
#define EXWORD_SUBLCD_PRDR	0xa405013cUL
#define EXWORD_SUBLCD_DC		BIT(2)
#define EXWORD_SUBLCD_WIDTH	244
#define EXWORD_SUBLCD_HEIGHT	99
#define EXWORD_SUBLCD_UI_WIDTH	240
#define EXWORD_SUBLCD_UI_HEIGHT	96

/* Firmware direct-resistive touch path. */
#define EXWORD_TOUCH_PLCR	0xa4050114UL
#define EXWORD_TOUCH_PLDR	0xa4050134UL
#define EXWORD_TOUCH_PHCR	0xa405010eUL
#define EXWORD_TOUCH_PHDR	0xa405012eUL
#define EXWORD_TOUCH_PGDR	0xa405012cUL
#define EXWORD_TOUCH_PSELE	0xa4050156UL
#define EXWORD_TOUCH_PFC_A	0xa40501d6UL
#define EXWORD_TOUCH_PGCR	0xa405010cUL
#define EXWORD_TOUCH_MSELCRA	0xa4050180UL
#define EXWORD_TOUCH_ADDRA	0xa4610080UL
#define EXWORD_TOUCH_ADDRB	0xa4610082UL
#define EXWORD_TOUCH_ADDRC	0xa4610084UL
#define EXWORD_TOUCH_ADDRD	0xa4610086UL
#define EXWORD_TOUCH_ADCSR	0xa4610088UL
#define EXWORD_TOUCH_ADCCSR	0xa461008aUL
#define EXWORD_TOUCH_ADCUST	0xa461008cUL
#define EXWORD_TOUCH_ADPCTL	0xa461008eUL
#define EXWORD_TOUCH_CPG_ID	0xa4150000UL
#define EXWORD_TOUCH_MSTPCR2	0xa4150038UL
#define EXWORD_TOUCH_MSTP_BIT	27
#define EXWORD_TOUCH_POLL_SCANS	4096
#define EXWORD_TOUCH_ADC_TIMEOUT	8192
#define EXWORD_TOUCH_SETTLE_LOOPS 16384
#define EXWORD_TOUCH_DEBOUNCE	2
#define EXWORD_TOUCH_RELEASE_DEBOUNCE 2
#define EXWORD_TOUCH_REPEAT_POLLS 3
#define EXWORD_TOUCH_MOUSE_STEP	6

#define EXWORD_POINTER_SOURCE_KEYPAD BIT(0)
#define EXWORD_POINTER_SOURCE_TOUCH BIT(1)

/* EX-word scan codes are encoded as (column + 1) * 10 + row. */
struct exword_key {
	u8 scan;
	u16 keycode;
	bool down;
};

struct exword_touch_calibration {
	s32 x_origin;
	s32 y_origin;
	s32 x_span;
	s32 y_span;
};

struct exword_touch_sample {
	u16 x;
	u16 y;
};

enum exword_touch_zone {
	EXWORD_TOUCH_NONE,
	EXWORD_TOUCH_LEFT,
	EXWORD_TOUCH_UP,
	EXWORD_TOUCH_RIGHT,
	EXWORD_TOUCH_BUTTON_LEFT,
	EXWORD_TOUCH_DOWN,
	EXWORD_TOUCH_BUTTON_RIGHT,
};

/*
 * The dictionary has letters but no conventional number row. The eight
 * function keys provide 1-8, Page Up/Down provide 9/0, and the labelled
 * utility keys provide shell punctuation. This makes the built-in keyboard
 * immediately useful on a Linux VT without a custom userspace keymap.
 */
static struct exword_key exword_keys[] = {
	{ 21, KEY_1 },		/* function 1 */
	{ 31, KEY_2 },		/* function 2 */
	{ 41, KEY_3 },		/* function 3 */
	{ 51, KEY_4 },		/* function 4 */
	{ 61, KEY_5 },		/* function 5 */
	{ 22, KEY_6 },		/* function 6 */
	{ 71, KEY_7 },		/* function 7 */
	{ 81, KEY_8 },		/* function 8 */
	{ 83, KEY_9 },		/* page up */
	{ 84, KEY_0 },		/* page down */
	{ 10, KEY_RESERVED },		/* power: toggle X pointer mode */
	{ 53, KEY_LEFTSHIFT },
	{ 58, KEY_BACKSPACE },
	{ 48, KEY_SPACE },		/* symbol */
	{ 76, KEY_ENTER },
	{ 75, KEY_LEFTCTRL },		/* back: usable shell control key */
	{ 73, KEY_MINUS },		/* history */
	{ 64, KEY_EQUAL },		/* zoom */
	{ 74, KEY_SLASH },		/* jump */
	{ 68, KEY_DOT },		/* audio */
	{ 77, KEY_UP },
	{ 86, KEY_DOWN },
	{ 87, KEY_LEFT },
	{ 88, KEY_RIGHT },
	{ 23, KEY_Q },
	{ 33, KEY_W },
	{ 24, KEY_E },
	{ 25, KEY_R },
	{ 35, KEY_T },
	{ 57, KEY_Y },
	{ 26, KEY_U },
	{ 36, KEY_I },
	{ 27, KEY_O },
	{ 28, KEY_P },
	{ 43, KEY_A },
	{ 34, KEY_S },
	{ 44, KEY_D },
	{ 45, KEY_F },
	{ 67, KEY_G },
	{ 66, KEY_H },
	{ 46, KEY_J },
	{ 37, KEY_K },
	{ 38, KEY_L },
	{ 63, KEY_Z },
	{ 54, KEY_X },
	{ 55, KEY_C },
	{ 65, KEY_V },
	{ 85, KEY_B },
	{ 56, KEY_N },
	{ 47, KEY_M },
};

static struct input_dev *exword_input;
static struct input_dev *exword_pointer;
static struct clk *exword_keysc_clk;
static struct task_struct *exword_scan_task;
static u16 exword_raw[6];
static bool exword_enter_press_seen;
static bool exword_mouse_mode;
static unsigned int exword_mouse_repeat_scans;
static u8 exword_pointer_button_sources[3];
static struct exword_touch_calibration exword_touch_cal;
static bool exword_touch_cal_valid;
static bool exword_touch_available;
static bool exword_touch_timeout_reported;
static bool exword_touch_enabled;
static bool exword_touch_prepared;
static bool exword_touch_gpio_prepared;
static bool exword_sublcd_supported;
static bool exword_sublcd_enabled;
static bool exword_touch_first_sample_reported;
static unsigned int exword_touch_poll_scans;
static enum exword_touch_zone exword_touch_candidate;
static enum exword_touch_zone exword_touch_active;
static unsigned int exword_touch_candidate_samples;
static unsigned int exword_touch_release_samples;
static unsigned int exword_touch_repeat_polls;
static struct exword_touch_sample exword_touch_candidate_sample;
static unsigned int exword_sublcd_next_row;
static bool exword_sublcd_finished;
static int exword_subpad_mode;

/*
 * Secondary hardware is deliberately opt-in while the B8600 register handoff
 * is being validated.  Bit 0 draws the LCD, bit 1 enables the custom ADC
 * touch path, and bit 2 performs a one-shot pen-GPIO diagnostic.  A writable
 * built-in module
 * parameter lets userspace stage each operation after the console is alive.
 */
module_param_named(subpad_mode, exword_subpad_mode, int, 0644);
MODULE_PARM_DESC(subpad_mode,
	"EX-word subpad: 0=off, 1=LCD, 2=ADC touch, 3=both, 4=pen GPIO");

static u16 exword_snapshot_reg(unsigned int n)
{
	return __raw_readw((void __iomem *)(EXWORD_SNAPSHOT_BASE + 4 + n * 2));
}

static s32 exword_touch_snapshot_long(unsigned int offset)
{
	return (s32)__raw_readl((void __iomem *)(EXWORD_SNAPSHOT_BASE + offset));
}

static bool exword_touch_calibration_sane(
	const struct exword_touch_calibration *cal)
{
	if (cal->x_origin < 0 || cal->x_origin > 1023 ||
	    cal->y_origin < 0 || cal->y_origin > 1023)
		return false;
	if (cal->x_span < 128 || cal->x_span > 8192)
		return false;
	if (cal->y_span < 128 || cal->y_span > 8192)
		return false;
	return true;
}

static void exword_touch_read_calibration(unsigned int offset,
					  struct exword_touch_calibration *cal)
{
	cal->x_origin = exword_touch_snapshot_long(offset);
	cal->y_origin = exword_touch_snapshot_long(offset + 4);
	cal->x_span = exword_touch_snapshot_long(offset + 8);
	cal->y_span = exword_touch_snapshot_long(offset + 12);
}

static void exword_touch_load_firmware_setup(void)
{
	u32 variant;

	if (__raw_readl((void __iomem *)(EXWORD_SNAPSHOT_BASE +
					 EXWORD_TOUCH_SNAPSHOT_OFFSET)) !=
	    EXWORD_TOUCH_SNAPSHOT_MAGIC) {
		pr_warn("EXTOUCH firmware snapshot missing; touch and sub-LCD disabled\n");
		exword_touch_cal_valid = false;
		exword_touch_available = false;
		exword_sublcd_supported = false;
		return;
	}

	variant = __raw_readl((void __iomem *)(EXWORD_SNAPSHOT_BASE +
					       EXWORD_TOUCH_VARIANT_OFFSET));
	/* CAL1 is the firmware's 240x96 secondary-panel calibration bank. */
	exword_touch_read_calibration(EXWORD_TOUCH_CAL1_OFFSET,
				       &exword_touch_cal);
	exword_touch_cal_valid =
		exword_touch_calibration_sane(&exword_touch_cal);
	/* Only firmware variant 1 is the recovered direct-ADC backend. */
	exword_sublcd_supported = variant == 1;
	exword_touch_available = variant == 1 && exword_touch_cal_valid;

	pr_info("EXTOUCH firmware variant=%u cal=%d,%d span=%d,%d valid=%u "
		 "touch=%u lcd=%u (hardware disabled at boot)\n",
		 variant, exword_touch_cal.x_origin, exword_touch_cal.y_origin,
		 exword_touch_cal.x_span, exword_touch_cal.y_span,
		 exword_touch_cal_valid, exword_touch_available,
		 exword_sublcd_supported);
}

static void exword_sublcd_command(u8 command)
{
	u8 prdr = __raw_readb((void __iomem *)EXWORD_SUBLCD_PRDR);

	__raw_writeb(prdr & ~EXWORD_SUBLCD_DC,
		     (void __iomem *)EXWORD_SUBLCD_PRDR);
	mb();
	__raw_writew(command, (void __iomem *)EXWORD_SUBLCD_DATA);
	mb();
	__raw_writeb(prdr | EXWORD_SUBLCD_DC,
		     (void __iomem *)EXWORD_SUBLCD_PRDR);
	mb();
}

static void exword_sublcd_data(u16 value)
{
	__raw_writew(value, (void __iomem *)EXWORD_SUBLCD_DATA);
}

static void exword_sublcd_set_row(unsigned int y)
{
	/* Controller X is offset by six; Y is direct in the firmware mode. */
	exword_sublcd_command(0x21);
	exword_sublcd_data(6);
	exword_sublcd_command(0x52);
	exword_sublcd_data(6);
	exword_sublcd_command(0x53);
	exword_sublcd_data(6 + EXWORD_SUBLCD_WIDTH - 1);

	exword_sublcd_command(0x20);
	exword_sublcd_data(y);
	exword_sublcd_command(0x50);
	exword_sublcd_data(y);
	exword_sublcd_command(0x51);
	exword_sublcd_data(y);
	exword_sublcd_command(0x22);
}

static unsigned int exword_small_abs(int value)
{
	return value < 0 ? (unsigned int)-value : (unsigned int)value;
}

static bool exword_sublcd_arrow_pixel(enum exword_touch_zone zone,
				      int x, int y)
{
	int ax = x - 40;
	int ay = y - 24;

	switch (zone) {
	case EXWORD_TOUCH_LEFT:
		return (x >= 35 && x <= 60 && exword_small_abs(ay) <= 3) ||
			(x >= 17 && x <= 40 &&
			 exword_small_abs(ay) <= (unsigned int)(x - 17) / 2);
	case EXWORD_TOUCH_UP:
		return (y >= 22 && y <= 40 && exword_small_abs(ax) <= 3) ||
			(y >= 7 && y <= 28 &&
			 exword_small_abs(ax) <= (unsigned int)(y - 7) / 2);
	case EXWORD_TOUCH_RIGHT:
		return (x >= 20 && x <= 45 && exword_small_abs(ay) <= 3) ||
			(x >= 40 && x <= 63 &&
			 exword_small_abs(ay) <= (unsigned int)(63 - x) / 2);
	case EXWORD_TOUCH_DOWN:
		return (y >= 8 && y <= 26 && exword_small_abs(ax) <= 3) ||
			(y >= 20 && y <= 41 &&
			 exword_small_abs(ax) <= (unsigned int)(41 - y) / 2);
	default:
		return false;
	}
}

static bool exword_sublcd_letter_pixel(enum exword_touch_zone zone,
				       int x, int y)
{
	static const u8 letter_l[7] = {
		0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f,
	};
	static const u8 letter_r[7] = {
		0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11,
	};
	const u8 *rows;
	unsigned int column;
	unsigned int row;

	if (zone != EXWORD_TOUCH_BUTTON_LEFT &&
	    zone != EXWORD_TOUCH_BUTTON_RIGHT)
		return false;
	if (x < 30 || x >= 50 || y < 10 || y >= 38)
		return false;
	rows = zone == EXWORD_TOUCH_BUTTON_LEFT ? letter_l : letter_r;
	column = (x - 30) >> 2;
	row = (y - 10) >> 2;
	return rows[row] & BIT(4 - column);
}

static u16 exword_sublcd_pad_pixel(unsigned int x, unsigned int y)
{
	enum exword_touch_zone zone;
	unsigned int local_x;
	unsigned int local_y;
	u16 background;

	if (x >= EXWORD_SUBLCD_UI_WIDTH || y >= EXWORD_SUBLCD_UI_HEIGHT)
		return 0x0000;
	if (x < 80) {
		local_x = x;
		zone = y < 48 ? EXWORD_TOUCH_LEFT :
			EXWORD_TOUCH_BUTTON_LEFT;
	} else if (x < 160) {
		local_x = x - 80;
		zone = y < 48 ? EXWORD_TOUCH_UP : EXWORD_TOUCH_DOWN;
	} else {
		local_x = x - 160;
		zone = y < 48 ? EXWORD_TOUCH_RIGHT :
			EXWORD_TOUCH_BUTTON_RIGHT;
	}
	local_y = y < 48 ? y : y - 48;

	if (local_x < 2 || local_x >= 78 || local_y < 2 || local_y >= 46)
		return 0xbdf7;
	background = (zone == EXWORD_TOUCH_BUTTON_LEFT ||
		      zone == EXWORD_TOUCH_BUTTON_RIGHT) ? 0x2448 : 0x194f;
	if (exword_sublcd_arrow_pixel(zone, local_x, local_y) ||
	    exword_sublcd_letter_pixel(zone, local_x, local_y))
		return 0xffff;
	return background;
}

static bool exword_sublcd_draw_next_row(void)
{
	unsigned int x;
	unsigned int y = exword_sublcd_next_row;

	/*
	 * Reuse the controller state left by the Casio firmware.  In particular,
	 * do not replay its model-specific power, reset, PFC or BSC sequence.
	 */
	if (exword_sublcd_finished || !exword_sublcd_enabled ||
	    !exword_sublcd_supported)
		return false;
	if (!y)
		pr_warn("EXTOUCH LCD probe begin (first external-bus row)\n");
	exword_sublcd_set_row(y);
	for (x = 0; x < EXWORD_SUBLCD_WIDTH; x++)
		exword_sublcd_data(exword_sublcd_pad_pixel(x, y));
	mb();
	if (!y)
		pr_warn("EXTOUCH LCD probe completed (first row)\n");

	if (++exword_sublcd_next_row == EXWORD_SUBLCD_HEIGHT) {
		exword_sublcd_finished = true;
		pr_info("EXTOUCH secondary LCD six-button pad drawn (%ux%u)\n",
			 EXWORD_SUBLCD_WIDTH, EXWORD_SUBLCD_HEIGHT);
	}
	return true;
}

static void exword_report_firmware_setup(void)
{
	if (__raw_readl((void __iomem *)EXWORD_SNAPSHOT_BASE) !=
	    EXWORD_SNAPSHOT_MAGIC) {
		pr_warn("EXKEY firmware snapshot missing\n");
		return;
	}

	pr_info("EXKEY firmware data %04x %04x %04x %04x %04x %04x\n",
		 exword_snapshot_reg(0), exword_snapshot_reg(1),
		 exword_snapshot_reg(2), exword_snapshot_reg(3),
		 exword_snapshot_reg(4), exword_snapshot_reg(5));
	pr_info("EXKEY firmware ctrl %04x %04x %04x %04x %04x %04x "
		 "%04x %04x %04x mstp0=%08x\n",
		 exword_snapshot_reg(6), exword_snapshot_reg(7),
		 exword_snapshot_reg(8), exword_snapshot_reg(9),
		 exword_snapshot_reg(10), exword_snapshot_reg(11),
		 exword_snapshot_reg(12), exword_snapshot_reg(13),
		 exword_snapshot_reg(14),
		 __raw_readl((void __iomem *)(EXWORD_SNAPSHOT_BASE + 0x24)));
}

static void exword_touch_settle(void)
{
	unsigned int pass;

	for (pass = 0; pass < EXWORD_TOUCH_SETTLE_LOOPS; pass++)
		cpu_relax();
}

static void exword_touch_disable(const char *reason)
{
	exword_touch_available = false;
	if (!exword_touch_timeout_reported) {
		pr_err("EXTOUCH %s; touch disabled, keypad unaffected\n", reason);
		exword_touch_timeout_reported = true;
	}
}

static bool exword_touch_open_clock(void)
{
	u32 mstp2 = __raw_readl((void __iomem *)EXWORD_TOUCH_MSTPCR2);

	if (mstp2 & BIT(EXWORD_TOUCH_MSTP_BIT)) {
		/* Change only the ADC gate; SDHI and other gates share this word. */
		__raw_writel(mstp2 & ~BIT(EXWORD_TOUCH_MSTP_BIT),
			     (void __iomem *)EXWORD_TOUCH_MSTPCR2);
		mb();
		exword_touch_settle();
	}
	mstp2 = __raw_readl((void __iomem *)EXWORD_TOUCH_MSTPCR2);
	if (mstp2 & BIT(EXWORD_TOUCH_MSTP_BIT)) {
		exword_touch_disable("ADC clock gate did not open");
		return false;
	}
	return true;
}

static bool exword_touch_wait_adcsr(void)
{
	unsigned int pass;

	for (pass = 0; pass < EXWORD_TOUCH_ADC_TIMEOUT; pass++) {
		if (!(__raw_readw((void __iomem *)EXWORD_TOUCH_ADCSR) & 0x2000))
			return true;
		cpu_relax();
	}
	exword_touch_disable("generic ADC timeout");
	return false;
}

static bool exword_touch_wait_adccsr(void)
{
	unsigned int pass;

	for (pass = 0; pass < EXWORD_TOUCH_ADC_TIMEOUT; pass++) {
		if (!(__raw_readw((void __iomem *)EXWORD_TOUCH_ADCCSR) & 0x2000))
			return true;
		cpu_relax();
	}
	exword_touch_disable("custom ADC timeout");
	return false;
}

static void exword_touch_base_pfc(void)
{
	u16 value;

	__raw_writeb(0, (void __iomem *)EXWORD_TOUCH_PGDR);
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_PHCR);
	__raw_writew((value & 0xfcff) | 0x0100,
		     (void __iomem *)EXWORD_TOUCH_PHCR);
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_PLCR);
	__raw_writew(value & ~0x000c, (void __iomem *)EXWORD_TOUCH_PLCR);
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_PSELE);
	__raw_writew(value & ~0x0c00, (void __iomem *)EXWORD_TOUCH_PSELE);
	mb();
}

static bool exword_touch_rest(bool high)
{
	u16 value;
	u8 phdr;

	/* Firmware waits for the custom unit before returning ADCUST to zero. */
	if (!exword_touch_wait_adccsr())
		return false;
	__raw_writew(0, (void __iomem *)EXWORD_TOUCH_ADCUST);
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_ADCSR);
	__raw_writew(value & ~0x2000, (void __iomem *)EXWORD_TOUCH_ADCSR);
	__raw_writew(0xaaaa, (void __iomem *)EXWORD_TOUCH_PGCR);
	__raw_writeb(0, (void __iomem *)EXWORD_TOUCH_PGDR);
	phdr = __raw_readb((void __iomem *)EXWORD_TOUCH_PHDR);
	if (high)
		phdr |= 0x10;
	else
		phdr &= ~0x10;
	__raw_writeb(phdr, (void __iomem *)EXWORD_TOUCH_PHDR);
	mb();
	exword_touch_settle();
	return true;
}

static bool exword_touch_read_pen_level(void);

static bool exword_touch_prepare_hardware(void)
{
	if (!exword_touch_available)
		return false;
	if (!exword_touch_open_clock())
		return false;
	exword_touch_base_pfc();
	if (!exword_touch_rest(true))
		return false;
	pr_info("EXTOUCH direct resistive controller ready, initial contact=%u\n",
		 exword_touch_read_pen_level());
	return true;
}

static bool exword_touch_read_pen_level(void)
{
	u16 value;
	bool contact = true;
	unsigned int pass;

	/* Probe PL1 in the firmware's mode 10 and filter five raw samples. */
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_PLCR);
	__raw_writew((value & ~0x000c) | 0x0008,
		     (void __iomem *)EXWORD_TOUCH_PLCR);
	mb();
	for (pass = 0; pass < 5; pass++) {
		if (__raw_readb((void __iomem *)EXWORD_TOUCH_PLDR) & BIT(1)) {
			contact = false;
			break;
		}
		cpu_relax();
	}
	/* Exact firmware post-probe routing, not an arbitrary saved value. */
	exword_touch_base_pfc();
	return contact;
}

static bool exword_touch_prepare_gpio_hardware(void)
{
	u8 phdr;

	if (!exword_sublcd_supported)
		return false;

	/*
	 * Recreate only the firmware's rest-high pen-detect routing.  Deliberately
	 * do not touch MSTPCR2 or any register in the unverified ADC block; this
	 * mode exists to distinguish a PFC/GPIO problem from an ADC bus stall.
	 */
	exword_touch_base_pfc();
	__raw_writew(0xaaaa, (void __iomem *)EXWORD_TOUCH_PGCR);
	__raw_writeb(0, (void __iomem *)EXWORD_TOUCH_PGDR);
	phdr = __raw_readb((void __iomem *)EXWORD_TOUCH_PHDR);
	__raw_writeb(phdr | 0x10, (void __iomem *)EXWORD_TOUCH_PHDR);
	mb();
	exword_touch_settle();
	pr_info("EXTOUCH pen GPIO ready, initial contact=%u\n",
		 exword_touch_read_pen_level());
	return true;
}

static bool exword_touch_pen_down(void)
{
	/* Rest-high PL1 is active low; require five low firmware-style samples. */
	return exword_touch_read_pen_level();
}

static bool exword_touch_read_adc(struct exword_touch_sample *sample)
{
	u16 raw[4];
	u16 value;
	u16 base;
	int pair_error;
	bool valid = false;

	if (!exword_touch_open_clock())
		return false;
	if (!exword_touch_rest(false))
		return false;

	/* Exact direct-touch custom ADC selection used by the Casio firmware. */
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_PFC_A);
	__raw_writew((value & 0x0fff) | 0x5000,
		     (void __iomem *)EXWORD_TOUCH_PFC_A);
	__raw_writew(0, (void __iomem *)EXWORD_TOUCH_PGCR);
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_MSELCRA);
	__raw_writew(value & 0xfcff, (void __iomem *)EXWORD_TOUCH_MSELCRA);
	mb();
	if (!exword_touch_wait_adcsr())
		return false;
	__raw_writew(0x8000, (void __iomem *)EXWORD_TOUCH_ADCUST);
	base = (__raw_readl((void __iomem *)EXWORD_TOUCH_CPG_ID) & 0xf) == 3 ?
		0x01cf : 0x02cf;
	__raw_writew(base, (void __iomem *)EXWORD_TOUCH_ADCCSR);
	__raw_writew(0x8000, (void __iomem *)EXWORD_TOUCH_ADPCTL);
	mb();
	/* Start without the firmware's ADC interrupt-enable bit. */
	__raw_writew(base | 0x2000, (void __iomem *)EXWORD_TOUCH_ADCCSR);
	if (!exword_touch_wait_adccsr())
		return false;

	raw[0] = __raw_readw((void __iomem *)EXWORD_TOUCH_ADDRB) >> 6;
	raw[1] = __raw_readw((void __iomem *)EXWORD_TOUCH_ADDRA) >> 6;
	raw[2] = __raw_readw((void __iomem *)EXWORD_TOUCH_ADDRD) >> 6;
	raw[3] = __raw_readw((void __iomem *)EXWORD_TOUCH_ADDRC) >> 6;
	value = __raw_readw((void __iomem *)EXWORD_TOUCH_ADCCSR);
	__raw_writew(value & ~0xc000, (void __iomem *)EXWORD_TOUCH_ADCCSR);

	pair_error = 1023 - raw[0] - raw[1];
	if (exword_small_abs(pair_error) <= 64) {
		pair_error = 1023 - raw[2] - raw[3];
		valid = exword_small_abs(pair_error) <= 64;
	}
	if (valid) {
		sample->x = ((int)raw[3] - raw[2] + 1023) >> 1;
		sample->y = ((int)raw[1] - raw[0] + 1023) >> 1;
		sample->x &= 1023;
		sample->y &= 1023;
	}

	/* Only switch modes and restore pen-detect drive after ADST cleared. */
	if (!exword_touch_rest(true))
		return false;
	return valid;
}

static bool exword_touch_read_sample(struct exword_touch_sample *sample)
{
	if (!exword_touch_available || !exword_touch_pen_down())
		return false;
	return exword_touch_read_adc(sample);
}

static void exword_read_state(u16 state[6])
{
	unsigned int i;

	/* This exact read-only access pattern is used by libdataplus and gint. */
	for (i = 0; i < 6; i++)
		state[i] = __raw_readw((void __iomem *)
					 (EXWORD_KEYPAD_BASE + i * 2));
}

/* Match the six 16-bit KIUDATA words used by the original add-in SDK. */
static bool exword_key_down(const u16 state[6], u8 scan)
{
	unsigned int row = scan % 10;
	unsigned int column = scan / 10 - 1;
	unsigned int word = row >> 1;

	return state[word] & BIT(column + 8 * (row & 1));
}

static void __init exword_prime_scanner(void)
{
	u16 sample[ARRAY_SIZE(exword_raw)];
	unsigned int pass;
	unsigned int i;

	/*
	 * SH7305 KEYSC behaves like a small sample queue and gives stale data if
	 * it is not consumed often enough.  Linux boot leaves a long gap after
	 * the add-in's last read, so drain a fixed, bounded number of samples
	 * before accepting the first state.  Do not use jiffies here: the
	 * provisional SH7305 port has no reliable Linux clock tick yet.  The
	 * hardware already performs key debounce.
	 */
	for (pass = 0; pass < EXWORD_PRIME_READS; pass++) {
		exword_read_state(sample);
		cpu_relax();
	}

	memcpy(exword_raw, sample, sizeof(exword_raw));
	for (i = 0; i < ARRAY_SIZE(exword_keys); i++)
		exword_keys[i].down = exword_key_down(sample,
						       exword_keys[i].scan);
}

static void exword_report_enter_tap(void)
{
	/*
	 * Keep Enter independent of key-repeat timers (which are not available
	 * yet on this SH7305 port) and leave the input core in the released
	 * state after every activation.
	 */
	input_report_key(exword_input, KEY_ENTER, 1);
	input_sync(exword_input);
	input_report_key(exword_input, KEY_ENTER, 0);
	input_sync(exword_input);
}

static bool exword_mouse_action_scan(u8 scan)
{
	switch (scan) {
	case 64: /* zoom: middle button */
	case 75: /* back: right button */
	case 76: /* enter: left button */
	case 77: /* up */
	case 86: /* down */
	case 87: /* left */
	case 88: /* right */
		return true;
	default:
		return false;
	}
}

static unsigned int exword_mouse_button(u8 scan)
{
	switch (scan) {
	case 76:
		return BTN_LEFT;
	case 64:
		return BTN_MIDDLE;
	case 75:
		return BTN_RIGHT;
	default:
		return 0;
	}
}

static int exword_pointer_button_index(unsigned int button)
{
	switch (button) {
	case BTN_LEFT:
		return 0;
	case BTN_MIDDLE:
		return 1;
	case BTN_RIGHT:
		return 2;
	default:
		return -1;
	}
}

static bool exword_set_pointer_button(unsigned int button, u8 source,
				       bool down)
{
	int index = exword_pointer_button_index(button);
	u8 previous;

	if (index < 0)
		return false;
	previous = exword_pointer_button_sources[index];
	if (down)
		exword_pointer_button_sources[index] |= source;
	else
		exword_pointer_button_sources[index] &= ~source;
	if (!!previous == !!exword_pointer_button_sources[index])
		return false;

	input_report_key(exword_pointer, button,
			 !!exword_pointer_button_sources[index]);
	input_sync(exword_pointer);
	return true;
}

static void exword_release_keypad_mouse_buttons(void)
{
	exword_set_pointer_button(BTN_LEFT, EXWORD_POINTER_SOURCE_KEYPAD, false);
	exword_set_pointer_button(BTN_MIDDLE, EXWORD_POINTER_SOURCE_KEYPAD, false);
	exword_set_pointer_button(BTN_RIGHT, EXWORD_POINTER_SOURCE_KEYPAD, false);
}

static void exword_release_mouse_keys_from_keyboard(void)
{
	/* Prevent a key held while mouse mode starts from sticking in X or tty1. */
	input_report_key(exword_input, KEY_UP, 0);
	input_report_key(exword_input, KEY_DOWN, 0);
	input_report_key(exword_input, KEY_LEFT, 0);
	input_report_key(exword_input, KEY_RIGHT, 0);
	input_report_key(exword_input, KEY_ENTER, 0);
	input_report_key(exword_input, KEY_EQUAL, 0);
	input_report_key(exword_input, KEY_LEFTCTRL, 0);
	input_sync(exword_input);
}

static void exword_toggle_mouse_mode(void)
{
	exword_mouse_mode = !exword_mouse_mode;
	exword_mouse_repeat_scans = 0;
	if (exword_mouse_mode)
		exword_release_mouse_keys_from_keyboard();
	else
		exword_release_keypad_mouse_buttons();
}

static bool exword_report_pointer_motion(const u16 state[6])
{
	int dx = 0;
	int dy = 0;

	if (exword_key_down(state, 87))
		dx -= EXWORD_MOUSE_STEP;
	if (exword_key_down(state, 88))
		dx += EXWORD_MOUSE_STEP;
	if (exword_key_down(state, 77))
		dy -= EXWORD_MOUSE_STEP;
	if (exword_key_down(state, 86))
		dy += EXWORD_MOUSE_STEP;
	if (!dx && !dy)
		return false;

	input_report_rel(exword_pointer, REL_X, dx);
	input_report_rel(exword_pointer, REL_Y, dy);
	input_sync(exword_pointer);
	return true;
}

static int exword_touch_coordinate(u16 raw, s32 origin, s32 span,
				    unsigned int limit)
{
	int value;

	if (!exword_touch_cal_valid)
		return (raw * limit) >> 10;
	/* Firmware calibration already produces secondary-panel pixel space. */
	value = ((s32)raw - origin) * 256 / span;
	if (value < 0)
		return 0;
	if (value >= (int)limit)
		return limit - 1;
	return value;
}

static enum exword_touch_zone exword_touch_sample_zone(
	const struct exword_touch_sample *sample)
{
	int x = exword_touch_coordinate(sample->x, exword_touch_cal.x_origin,
					 exword_touch_cal.x_span,
					 EXWORD_SUBLCD_UI_WIDTH);
	int y = exword_touch_coordinate(sample->y, exword_touch_cal.y_origin,
					 exword_touch_cal.y_span,
					 EXWORD_SUBLCD_UI_HEIGHT);

	if (y < 48) {
		if (x < 80)
			return EXWORD_TOUCH_LEFT;
		if (x < 160)
			return EXWORD_TOUCH_UP;
		return EXWORD_TOUCH_RIGHT;
	}
	if (x < 80)
		return EXWORD_TOUCH_BUTTON_LEFT;
	if (x < 160)
		return EXWORD_TOUCH_DOWN;
	return EXWORD_TOUCH_BUTTON_RIGHT;
}

static bool exword_touch_report_motion(enum exword_touch_zone zone)
{
	int dx = 0;
	int dy = 0;

	switch (zone) {
	case EXWORD_TOUCH_LEFT:
		dx = -EXWORD_TOUCH_MOUSE_STEP;
		break;
	case EXWORD_TOUCH_UP:
		dy = -EXWORD_TOUCH_MOUSE_STEP;
		break;
	case EXWORD_TOUCH_RIGHT:
		dx = EXWORD_TOUCH_MOUSE_STEP;
		break;
	case EXWORD_TOUCH_DOWN:
		dy = EXWORD_TOUCH_MOUSE_STEP;
		break;
	default:
		return false;
	}
	input_report_rel(exword_pointer, REL_X, dx);
	input_report_rel(exword_pointer, REL_Y, dy);
	input_sync(exword_pointer);
	return true;
}

static bool exword_touch_activate_zone(enum exword_touch_zone zone)
{
	switch (zone) {
	case EXWORD_TOUCH_BUTTON_LEFT:
		return exword_set_pointer_button(BTN_LEFT,
				EXWORD_POINTER_SOURCE_TOUCH, true);
	case EXWORD_TOUCH_BUTTON_RIGHT:
		return exword_set_pointer_button(BTN_RIGHT,
				EXWORD_POINTER_SOURCE_TOUCH, true);
	default:
		return exword_touch_report_motion(zone);
	}
}

static bool exword_touch_release_active(void)
{
	bool activity = false;

	if (exword_touch_active == EXWORD_TOUCH_BUTTON_LEFT)
		activity = exword_set_pointer_button(BTN_LEFT,
			EXWORD_POINTER_SOURCE_TOUCH, false);
	else if (exword_touch_active == EXWORD_TOUCH_BUTTON_RIGHT)
		activity = exword_set_pointer_button(BTN_RIGHT,
			EXWORD_POINTER_SOURCE_TOUCH, false);
	exword_touch_active = EXWORD_TOUCH_NONE;
	exword_touch_repeat_polls = 0;
	return activity;
}

static void exword_service_subpad_mode(void)
{
	static int applied_mode = -1;
	int requested = READ_ONCE(exword_subpad_mode);
	int mode = requested < 0 ? 0 : requested & 7;
	bool want_lcd = mode & 1;
	bool want_touch = mode & 2;
	bool want_pen_probe = mode & 4;

	if (mode == applied_mode)
		return;

	/* All state and MMIO changes happen in this one existing scan thread. */
	if (!want_touch && exword_touch_enabled) {
		exword_touch_enabled = false;
		exword_touch_release_active();
		exword_touch_poll_scans = 0;
		exword_touch_candidate = EXWORD_TOUCH_NONE;
		exword_touch_candidate_samples = 0;
		exword_touch_release_samples = 0;
		exword_touch_repeat_polls = 0;
	}

	if (want_lcd && !exword_sublcd_enabled) {
		exword_sublcd_next_row = 0;
		exword_sublcd_finished = false;
	}
	exword_sublcd_enabled = want_lcd && exword_sublcd_supported;

	if (want_pen_probe && !exword_touch_gpio_prepared) {
		/* Mark first so a bounded failure is not retried every scan burst. */
		exword_touch_gpio_prepared = true;
		pr_warn("EXTOUCH pen-GPIO probe begin (ADC untouched)\n");
		if (!exword_touch_prepare_gpio_hardware())
			pr_warn("EXTOUCH pen GPIO diagnostic unavailable\n");
		else
			pr_warn("EXTOUCH pen-GPIO probe completed\n");
	}

	if (want_touch) {
		if (!exword_touch_prepared) {
			exword_touch_prepared = true;
			pr_warn("EXTOUCH ADC preparation begin\n");
			exword_touch_enabled = exword_touch_prepare_hardware();
			if (exword_touch_enabled) {
				exword_touch_gpio_prepared = true;
				pr_warn("EXTOUCH ADC preparation completed\n");
			} else {
				pr_warn("EXTOUCH ADC preparation failed\n");
			}
		} else {
			exword_touch_enabled = exword_touch_available;
		}
	}

	applied_mode = mode;
	pr_info("EXTOUCH subpad mode=%d lcd=%u touch=%u pen-probed=%u\n",
		 mode, exword_sublcd_enabled, exword_touch_enabled,
		 exword_touch_gpio_prepared);
}

static bool exword_touch_poll(void)
{
	struct exword_touch_sample sample;
	enum exword_touch_zone zone;
	bool activity = false;

	if (!exword_touch_read_sample(&sample)) {
		exword_touch_candidate = EXWORD_TOUCH_NONE;
		exword_touch_candidate_samples = 0;
		if (exword_touch_active != EXWORD_TOUCH_NONE &&
		    ++exword_touch_release_samples >=
		    EXWORD_TOUCH_RELEASE_DEBOUNCE) {
			exword_touch_release_samples = 0;
			activity = exword_touch_release_active();
		}
		return activity;
	}

	exword_touch_release_samples = 0;
	zone = exword_touch_sample_zone(&sample);
	if (!exword_touch_first_sample_reported) {
		pr_info("EXTOUCH first valid sample raw=%u,%u zone=%u\n",
			 sample.x, sample.y, zone);
		exword_touch_first_sample_reported = true;
	}

	/* Once pressed, latch the zone until pen-up to avoid boundary chatter. */
	if (exword_touch_active != EXWORD_TOUCH_NONE) {
		if (exword_touch_active != EXWORD_TOUCH_BUTTON_LEFT &&
		    exword_touch_active != EXWORD_TOUCH_BUTTON_RIGHT &&
		    ++exword_touch_repeat_polls >= EXWORD_TOUCH_REPEAT_POLLS) {
			exword_touch_repeat_polls = 0;
			activity = exword_touch_report_motion(exword_touch_active);
		}
		return activity;
	}

	if (zone != exword_touch_candidate ||
	    exword_small_abs((int)sample.x -
			     exword_touch_candidate_sample.x) > 96 ||
	    exword_small_abs((int)sample.y -
			     exword_touch_candidate_sample.y) > 96) {
		exword_touch_candidate = zone;
		exword_touch_candidate_samples = 1;
	} else {
		exword_touch_candidate_samples++;
	}
	exword_touch_candidate_sample = sample;
	if (exword_touch_candidate_samples < EXWORD_TOUCH_DEBOUNCE)
		return false;

	exword_touch_active = zone;
	exword_touch_candidate = EXWORD_TOUCH_NONE;
	exword_touch_candidate_samples = 0;
	exword_touch_repeat_polls = 0;
	return exword_touch_activate_zone(zone);
}

static bool exword_scan_once(void)
{
	u16 sample[ARRAY_SIZE(exword_raw)];
	bool activity = false;
	bool mouse_direction_edge = false;
	bool mouse_direction_held;
	bool raw_changed = false;
	bool sync = false;
	unsigned int i;
	u32 mstp0;

	/* Defeat any late SH7724 clock pass that mistakes this gate for SCIF3. */
	mstp0 = __raw_readl((void __iomem *)EXWORD_MSTPCR0);
	if (mstp0 & BIT(EXWORD_KEYSC_MSTP_BIT)) {
		__raw_writel(mstp0 & ~BIT(EXWORD_KEYSC_MSTP_BIT),
			     (void __iomem *)EXWORD_MSTPCR0);
		pr_warn("EXKEY reopened stopped SH7305 clock gate\n");
	}

	/* SH7305 exposes six native 16-bit key-state words, unlike SH7724. */
	exword_read_state(sample);
	for (i = 0; i < ARRAY_SIZE(sample); i++) {
		if (sample[i] != exword_raw[i])
			raw_changed = true;
	}

	if (raw_changed) {
		for (i = 0; i < ARRAY_SIZE(sample); i++)
			exword_raw[i] = sample[i];
		pr_debug("EXKEY change %04x %04x %04x %04x %04x %04x\n",
			 sample[0], sample[1], sample[2], sample[3], sample[4],
			 sample[5]);
	}

	for (i = 0; i < ARRAY_SIZE(exword_keys); i++) {
		bool down = exword_key_down(sample, exword_keys[i].scan);

		if (down == exword_keys[i].down)
			continue;
		exword_keys[i].down = down;

		if (exword_keys[i].scan == EXWORD_MOUSE_TOGGLE_SCAN) {
			if (down)
				exword_toggle_mouse_mode();
			activity = true;
			continue;
		}

		if (exword_mouse_mode &&
		    exword_mouse_action_scan(exword_keys[i].scan)) {
			unsigned int button =
				exword_mouse_button(exword_keys[i].scan);

			if (button) {
				exword_set_pointer_button(button,
					EXWORD_POINTER_SOURCE_KEYPAD, down);
			} else if (down) {
				mouse_direction_edge = true;
			}
			activity = true;
			continue;
		}

		/*
		 * The Enter used to launch Linux can remain queued as held.  In that
		 * case the first observable edge is its release, not its press.  Turn
		 * either a normal press or that one unmatched release into one full
		 * Return tap.  Normal releases following a seen press emit nothing.
		 */
		if (exword_keys[i].keycode == KEY_ENTER) {
			if (down || !exword_enter_press_seen)
				exword_report_enter_tap();
			exword_enter_press_seen = down;
			activity = true;
			continue;
		}

		input_report_key(exword_input, exword_keys[i].keycode, down);
		pr_debug("EXKEY event code=%u down=%u\n",
			 exword_keys[i].keycode, down);
		activity = true;
		sync = true;
	}
	if (sync)
		input_sync(exword_input);

	if (!exword_mouse_mode) {
		exword_mouse_repeat_scans = 0;
	} else {
		mouse_direction_held = exword_key_down(sample, 77) ||
			exword_key_down(sample, 86) ||
			exword_key_down(sample, 87) ||
			exword_key_down(sample, 88);
		if (!mouse_direction_held) {
			exword_mouse_repeat_scans = 0;
		} else if (mouse_direction_edge ||
			   ++exword_mouse_repeat_scans >=
			   EXWORD_MOUSE_REPEAT_SCANS) {
			exword_mouse_repeat_scans = 0;
			activity |= exword_report_pointer_motion(sample);
		}
	}

	/* The secondary touch pad is a pointer only after explicit activation. */
	if (!exword_touch_enabled) {
		exword_touch_poll_scans = 0;
	} else if (++exword_touch_poll_scans >= EXWORD_TOUCH_POLL_SCANS) {
		exword_touch_poll_scans = 0;
		activity |= exword_touch_poll();
	}

	return activity;
}

static int exword_scan_thread(void *unused)
{
	unsigned int burst = 0;
	bool activity;

	/* Interactive userspace wins every voluntary scheduling decision. */
	set_user_nice(current, 19);

	/*
	 * Delayed work cannot run until the SH7305 clock-event support is fixed.
	 * Stay runnable, poll in short bursts, and yield explicitly.  When the
	 * shell blocks in read(), this thread scans; a key event wakes the shell.
	 * yield() is important here: plain schedule() is allowed to select this
	 * still-runnable thread again, while the fair scheduler's yield path marks
	 * it to be skipped when BusyBox has just become runnable.
	 */
	while (!kthread_should_stop()) {
		activity = exword_scan_once();
		if (activity || ++burst == EXWORD_POLL_BURST) {
			burst = 0;
			exword_service_subpad_mode();
			/* One bounded row per turn keeps keypad scanning responsive. */
			exword_sublcd_draw_next_row();
			yield();
		}
	}

	return 0;
}

static int __init exword_keypad_init(void)
{
	unsigned int i;
	int error;

	/*
	 * The EX-word uses the SH7305's custom KEYSC block.  We build the
	 * kernel with the closely related SH7724 clock code, whose late init
	 * pass stops every unclaimed module.  On SH7305 the KEYSC module-stop
	 * gate is MSTPCR0 bit 6; the SH7724 clock table calls that same gate
	 * "sh-sci.3" (its own KEYSC gate is elsewhere).  Claim this physical
	 * gate before the late pass so the firmware-configured scanner keeps
	 * updating the six key-state words at 0xa44b0000.
	 */
	exword_keysc_clk = clk_get_sys("sh-sci.3", NULL);
	if (IS_ERR(exword_keysc_clk)) {
		pr_emerg("EXKEY compatibility clock lookup failed: %ld; "
			 "using direct SH7305 gate\n", PTR_ERR(exword_keysc_clk));
		exword_keysc_clk = NULL;
	} else {
		error = clk_prepare_enable(exword_keysc_clk);
		if (error) {
			pr_emerg("EXKEY compatibility clock enable failed: %d; "
				 "using direct SH7305 gate\n", error);
			clk_put(exword_keysc_clk);
			exword_keysc_clk = NULL;
		}
	}

	/* Also force the native SH7305 gate on; the SH7724 table misnames it. */
	__raw_writel(__raw_readl((void __iomem *)EXWORD_MSTPCR0) &
		     ~BIT(EXWORD_KEYSC_MSTP_BIT),
		     (void __iomem *)EXWORD_MSTPCR0);
	exword_report_firmware_setup();
	exword_touch_load_firmware_setup();
	exword_prime_scanner();

	exword_input = input_allocate_device();
	if (!exword_input) {
		if (exword_keysc_clk) {
			clk_disable_unprepare(exword_keysc_clk);
			clk_put(exword_keysc_clk);
		}
		return -ENOMEM;
	}
	exword_pointer = input_allocate_device();
	if (!exword_pointer) {
		input_free_device(exword_input);
		exword_input = NULL;
		if (exword_keysc_clk) {
			clk_disable_unprepare(exword_keysc_clk);
			clk_put(exword_keysc_clk);
		}
		return -ENOMEM;
	}

	exword_input->name = "EX-word keypad";
	exword_input->phys = "exword/input0";
	exword_input->id.bustype = BUS_HOST;
	exword_input->id.vendor = 0x07cf; /* Casio */
	exword_input->id.product = 0xb860;
	exword_input->id.version = 1;
	__set_bit(EV_REP, exword_input->evbit);
	for (i = 0; i < ARRAY_SIZE(exword_keys); i++) {
		if (exword_keys[i].keycode != KEY_RESERVED)
			input_set_capability(exword_input, EV_KEY,
					     exword_keys[i].keycode);
	}

	exword_pointer->name = "EX-word pointer";
	exword_pointer->phys = "exword/input1";
	exword_pointer->id.bustype = BUS_HOST;
	exword_pointer->id.vendor = 0x07cf; /* Casio */
	exword_pointer->id.product = 0xb860;
	exword_pointer->id.version = 1;
	input_set_capability(exword_pointer, EV_REL, REL_X);
	input_set_capability(exword_pointer, EV_REL, REL_Y);
	input_set_capability(exword_pointer, EV_KEY, BTN_LEFT);
	input_set_capability(exword_pointer, EV_KEY, BTN_MIDDLE);
	input_set_capability(exword_pointer, EV_KEY, BTN_RIGHT);

	error = input_register_device(exword_input);
	if (error) {
		input_free_device(exword_input);
		input_free_device(exword_pointer);
		exword_input = NULL;
		exword_pointer = NULL;
		if (exword_keysc_clk) {
			clk_disable_unprepare(exword_keysc_clk);
			clk_put(exword_keysc_clk);
		}
		return error;
	}
	error = input_register_device(exword_pointer);
	if (error) {
		input_free_device(exword_pointer);
		exword_pointer = NULL;
		input_unregister_device(exword_input);
		exword_input = NULL;
		if (exword_keysc_clk) {
			clk_disable_unprepare(exword_keysc_clk);
			clk_put(exword_keysc_clk);
		}
		return error;
	}

	exword_scan_task = kthread_run(exword_scan_thread, NULL, "exword-keypad");
	if (IS_ERR(exword_scan_task)) {
		error = PTR_ERR(exword_scan_task);
		pr_err("EXKEY cannot start polling thread: %d\n", error);
		input_unregister_device(exword_pointer);
		exword_pointer = NULL;
		input_unregister_device(exword_input);
		exword_input = NULL;
		if (exword_keysc_clk) {
			clk_disable_unprepare(exword_keysc_clk);
			clk_put(exword_keysc_clk);
		}
		return error;
	}

	pr_info("EXKEY read-only scanner ready MSTPCR0=%08x ctrl=%04x "
		 "auto=%04x mode=%04x "
		 "state=%04x irq=%04x wait=%04x interval=%04x out=%04x in=%04x\n",
		 __raw_readl((void __iomem *)EXWORD_MSTPCR0),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x0c)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x0e)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x10)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x12)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x14)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x16)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x18)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x1a)),
		 __raw_readw((void __iomem *)(EXWORD_KEYPAD_BASE + 0x1c)));
	return 0;
}
device_initcall(exword_keypad_init);

MODULE_DESCRIPTION("Casio EX-word DATAPLUS 6 keypad");
MODULE_LICENSE("GPL");

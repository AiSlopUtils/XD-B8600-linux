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
#define EXWORD_PRIME_READS	65536
#define EXWORD_POLL_BURST	256
#define EXWORD_MOUSE_TOGGLE_SCAN	10
#define EXWORD_MOUSE_REPEAT_SCANS 16384
#define EXWORD_MOUSE_STEP	4

/* EX-word scan codes are encoded as (column + 1) * 10 + row. */
struct exword_key {
	u8 scan;
	u16 keycode;
	bool down;
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

static u16 exword_snapshot_reg(unsigned int n)
{
	return __raw_readw((void __iomem *)(EXWORD_SNAPSHOT_BASE + 4 + n * 2));
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

static void exword_release_mouse_buttons(void)
{
	input_report_key(exword_pointer, BTN_LEFT, 0);
	input_report_key(exword_pointer, BTN_MIDDLE, 0);
	input_report_key(exword_pointer, BTN_RIGHT, 0);
	input_sync(exword_pointer);
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
		exword_release_mouse_buttons();
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
				input_report_key(exword_pointer, button, down);
				input_sync(exword_pointer);
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
		return activity;
	}

	mouse_direction_held = exword_key_down(sample, 77) ||
		exword_key_down(sample, 86) || exword_key_down(sample, 87) ||
		exword_key_down(sample, 88);
	if (!mouse_direction_held) {
		exword_mouse_repeat_scans = 0;
	} else if (mouse_direction_edge ||
		   ++exword_mouse_repeat_scans >= EXWORD_MOUSE_REPEAT_SCANS) {
		exword_mouse_repeat_scans = 0;
		activity |= exword_report_pointer_motion(sample);
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

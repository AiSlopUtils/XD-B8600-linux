// SPDX-License-Identifier: GPL-2.0
/* Framebuffer for the firmware-initialized Casio EX-word main LCD. */

#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#define EXWORD_WIDTH 528
#define EXWORD_HEIGHT 320
#define EXWORD_LINE_LENGTH (EXWORD_WIDTH * 2)
#define EXWORD_VRAM_SIZE (EXWORD_LINE_LENGTH * EXWORD_HEIGHT)

#define EXWORD_VRAM_P1 0x8c200000UL
#define EXWORD_VRAM_P2 0xac200000UL
#define EXWORD_VRAM_PHYS 0x0c200000UL
#define EXWORD_LCDC 0xb4000000UL
#define EXWORD_PORTR 0xa405013cUL
#define EXWORD_SAR3 0xfe008050UL
#define EXWORD_DAR3 0xfe008054UL
#define EXWORD_TCR3 0xfe008058UL
#define EXWORD_CHCR3 0xfe00805cUL
#define EXWORD_DMAOR 0xfe008060UL

struct exwordfb_par {
	u32 pseudo_palette[16];
	struct work_struct refresh_work;
	atomic_t refresh_generation;
};

static DEFINE_SPINLOCK(exwordfb_dma_lock);

static void exwordfb_refresh(void)
{
	unsigned long flags;
	unsigned int timeout = 1000000;
	u8 portr;
	u16 dmaor;

	spin_lock_irqsave(&exwordfb_dma_lock, flags);
	portr = __raw_readb((void __iomem *)EXWORD_PORTR);
	__raw_writeb(portr & ~0x10, (void __iomem *)EXWORD_PORTR);
	mb();
	__raw_writew(0x2c, (void __iomem *)EXWORD_LCDC);
	mb();
	__raw_writeb(portr | 0x10, (void __iomem *)EXWORD_PORTR);
	mb();

	__raw_writel((EXWORD_WIDTH * EXWORD_HEIGHT) >> 4,
		     (void __iomem *)EXWORD_TCR3);
	__raw_writel(EXWORD_VRAM_PHYS, (void __iomem *)EXWORD_SAR3);
	__raw_writel(EXWORD_LCDC & 0x1fffffff,
		     (void __iomem *)EXWORD_DAR3);
	dmaor = __raw_readw((void __iomem *)EXWORD_DMAOR);
	__raw_writew(dmaor & ~1, (void __iomem *)EXWORD_DMAOR);
	__raw_writel(0x40101401, (void __iomem *)EXWORD_CHCR3);
	__raw_writew(dmaor | 1, (void __iomem *)EXWORD_DMAOR);
	while (!(__raw_readl((void __iomem *)EXWORD_CHCR3) & 2) && --timeout)
		cpu_relax();
	spin_unlock_irqrestore(&exwordfb_dma_lock, flags);
}

static void exwordfb_refresh_work(struct work_struct *work)
{
	struct exwordfb_par *par = container_of(work, struct exwordfb_par,
						refresh_work);
	int generation;

	/*
	 * Waking this worker can make fbcon's conditional reschedule points
	 * switch to us halfway through a multi-row redraw.  Yield back until a
	 * complete scheduling turn passes without another drawing operation;
	 * then present the finished frame once instead of once per text row.
	 */
	do {
		generation = atomic_read(&par->refresh_generation);
		yield();
	} while (generation != atomic_read(&par->refresh_generation));

	exwordfb_refresh();
}

static void exwordfb_queue_refresh(struct fb_info *info)
{
	struct exwordfb_par *par = info->par;

	/*
	 * The provisional SH7305 port has no reliable clock event yet, so a
	 * delayed work item may never expire.  A normal work item is also the
	 * batching primitive we want here: all framebuffer operations performed
	 * before the console task next schedules collapse into one LCD transfer.
	 */
	atomic_inc(&par->refresh_generation);
	schedule_work(&par->refresh_work);
}

static int exwordfb_setcolreg(u_int regno, u_int red, u_int green,
			     u_int blue, u_int transp, struct fb_info *info)
{
	u32 *palette = info->pseudo_palette;

	if (regno >= 16)
		return -EINVAL;
	palette[regno] = ((red & 0xf800) >> 0) |
			 ((green & 0xfc00) >> 5) |
			 ((blue & 0xf800) >> 11);
	return 0;
}

static void exwordfb_fillrect(struct fb_info *info,
			     const struct fb_fillrect *rect)
{
	cfb_fillrect(info, rect);
	exwordfb_queue_refresh(info);
}

static void exwordfb_copyarea(struct fb_info *info,
			     const struct fb_copyarea *area)
{
	cfb_copyarea(info, area);
	exwordfb_queue_refresh(info);
}

static void exwordfb_imageblit(struct fb_info *info,
			      const struct fb_image *image)
{
	cfb_imageblit(info, image);
	exwordfb_queue_refresh(info);
}

static int exwordfb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info)
{
	struct exwordfb_par *par = info->par;

	cancel_work_sync(&par->refresh_work);
	exwordfb_refresh();
	return 0;
}

static ssize_t exwordfb_write(struct fb_info *info, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	ssize_t written = fb_sys_write(info, buf, count, ppos);

	if (written > 0)
		exwordfb_queue_refresh(info);
	return written;
}

static int exwordfb_blank(int blank, struct fb_info *info)
{
	/* Firmware owns panel power; an unblank request presents the latest frame. */
	if (blank == FB_BLANK_UNBLANK)
		exwordfb_queue_refresh(info);
	return 0;
}

static const struct fb_ops exwordfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = exwordfb_write,
	.fb_setcolreg = exwordfb_setcolreg,
	.fb_fillrect = exwordfb_fillrect,
	.fb_copyarea = exwordfb_copyarea,
	.fb_imageblit = exwordfb_imageblit,
	.fb_pan_display = exwordfb_pan_display,
	.fb_blank = exwordfb_blank,
};

static int __init exwordfb_init(void)
{
	struct exwordfb_par *par;
	struct fb_info *info;
	int error;

	info = framebuffer_alloc(sizeof(*par), NULL);
	if (!info)
		return -ENOMEM;
	par = info->par;
	INIT_WORK(&par->refresh_work, exwordfb_refresh_work);
	atomic_set(&par->refresh_generation, 0);

	strscpy(info->fix.id, "EX-word LCD", sizeof(info->fix.id));
	info->fix.smem_start = EXWORD_VRAM_PHYS;
	info->fix.smem_len = EXWORD_VRAM_SIZE;
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.visual = FB_VISUAL_TRUECOLOR;
	info->fix.line_length = EXWORD_LINE_LENGTH;
	info->fix.accel = FB_ACCEL_NONE;

	info->var.xres = EXWORD_WIDTH;
	info->var.yres = EXWORD_HEIGHT;
	info->var.xres_virtual = EXWORD_WIDTH;
	info->var.yres_virtual = EXWORD_HEIGHT;
	info->var.bits_per_pixel = 16;
	info->var.red.offset = 11;
	info->var.red.length = 5;
	info->var.green.offset = 5;
	info->var.green.length = 6;
	info->var.blue.offset = 0;
	info->var.blue.length = 5;
	info->var.activate = FB_ACTIVATE_NOW;
	info->var.height = -1;
	info->var.width = -1;
	info->var.vmode = FB_VMODE_NONINTERLACED;

	info->fbops = &exwordfb_ops;
	info->screen_base = (char __iomem *)EXWORD_VRAM_P2;
	info->screen_size = EXWORD_VRAM_SIZE;
	info->pseudo_palette = par->pseudo_palette;
	info->flags = FBINFO_DEFAULT;

	error = fb_alloc_cmap(&info->cmap, 16, 0);
	if (error)
		goto release;
	error = register_framebuffer(info);
	if (error)
		goto cmap;

	exwordfb_refresh();
	pr_info("exwordfb: registered %ux%u RGB565 framebuffer\n",
		EXWORD_WIDTH, EXWORD_HEIGHT);
	return 0;

cmap:
	fb_dealloc_cmap(&info->cmap);
release:
	framebuffer_release(info);
	return error;
}
/*
 * Register after the framebuffer core (subsys_initcall) but before the
 * rootfs initcall.  A malformed or memory-starved built-in initramfs must be
 * visible on this framebuffer instead of leaving the decompressor's marker
 * on screen with no diagnostic output.
 */
fs_initcall(exwordfb_init);

MODULE_DESCRIPTION("Casio EX-word DATAPLUS 6 framebuffer");
MODULE_LICENSE("GPL");

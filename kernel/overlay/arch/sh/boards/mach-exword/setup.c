// SPDX-License-Identifier: GPL-2.0
/* Minimal Casio EX-word DATAPLUS 6 board support. */

#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/string.h>

#if IS_ENABLED(CONFIG_MMC_SDHI_SYS_DMAC)
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/platform_device.h>
#include <linux/sh_intc.h>
#endif

#if IS_BUILTIN(CONFIG_MTD)
#include <linux/mtd/mtd.h>
#include <asm/addrspace.h>
#endif

#include <asm/machvec.h>

#define EXWORD_VRAM_PHYS 0x0c200000UL
#define EXWORD_VRAM_SIZE 0x00053000UL

/*
 * The signed loader stages the read-only X11 SquashFS immediately after the
 * zImage.  Keeping it outside initramfs prevents a 2.3 MiB compressed archive
 * from being copied into the decompressed kernel and then duplicated again by
 * populate_rootfs() on a machine with only 16 MiB.
 *
 * This window ends exactly where the +9 MiB compressed-kernel relocation
 * begins.  The payload packer rejects a zImage that reaches the window and a
 * SquashFS that is larger than it.
 */
#define EXWORD_X11_PHYS 0x0c9b0000UL
#define EXWORD_X11_SIZE 0x00350000UL

#if IS_ENABLED(CONFIG_MMC_SDHI_SYS_DMAC)
/*
 * The add-in loader enters Linux after the Casio firmware has configured and
 * powered the external SD slot.  Leave that board-specific state untouched
 * and expose only the documented SH7724 SDHI1 controller.  Empty DMA channel
 * cookies deliberately keep the Renesas/TMIO host on its PIO path.
 */
static struct tmio_mmc_data exword_sdhi1_data = {
	.ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.capabilities	= MMC_CAP_NEEDS_POLL,
	.max_blk_count	= 8,
	.max_segs	= 1,
};

static struct resource exword_sdhi1_resources[] = {
	{
		.name	= "SDHI1",
		.start	= 0x04cf0000,
		.end	= 0x04cf00ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= evt2irq(0x4e0),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device exword_sdhi1_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(exword_sdhi1_resources),
	.resource	= exword_sdhi1_resources,
	.dev = {
		.platform_data = &exword_sdhi1_data,
	},
};

static int __init exword_sdhi1_init(void)
{
	int ret;

	/*
	 * Keep boot independent of the still-unverified firmware storage
	 * handoff.  The legacy SH clock tree currently reports a zero rate and
	 * the stock asynchronous SDHI probe can otherwise hold kernel init
	 * forever.  A heap-backed override is required because sysfs may replace
	 * and free this string later while we diagnose the inherited pin mux.
	 */
	exword_sdhi1_device.driver_override =
		kstrdup("exword-storage-disabled", GFP_KERNEL);
	if (!exword_sdhi1_device.driver_override)
		return -ENOMEM;

	ret = platform_device_register(&exword_sdhi1_device);
	if (ret) {
		kfree(exword_sdhi1_device.driver_override);
		exword_sdhi1_device.driver_override = NULL;
		return ret;
	}

	pr_info("exword: SDHI1 registered unbound for boot-safe diagnostics\n");
	return 0;
}
arch_initcall(exword_sdhi1_init);
#endif

static void __init exword_setup(char **cmdline_p)
{
}

static void __init exword_mem_reserve(void)
{
	memblock_reserve(EXWORD_VRAM_PHYS, EXWORD_VRAM_SIZE);
	memblock_reserve(EXWORD_X11_PHYS, EXWORD_X11_SIZE);
}

#if IS_BUILTIN(CONFIG_MTD)
static struct mtd_info exword_x11_mtd;

static int exword_x11_read(struct mtd_info *mtd, loff_t from, size_t len,
			   size_t *retlen, u_char *buf)
{
	const void *source;

	if (from < 0 || from >= mtd->size)
		return -EINVAL;
	if (len > mtd->size - from)
		len = mtd->size - from;

	/* P2 reads avoid inheriting stale cache lines from the add-in loader. */
	source = (const void *)(P2SEGADDR(EXWORD_X11_PHYS) +
				 (unsigned long)from);
	memcpy(buf, source, len);
	*retlen = len;
	return 0;
}

static int exword_x11_write(struct mtd_info *mtd, loff_t to, size_t len,
			    size_t *retlen, const u_char *buf)
{
	*retlen = 0;
	return -EROFS;
}

static int exword_x11_erase(struct mtd_info *mtd, struct erase_info *erase)
{
	return -EROFS;
}

static void exword_x11_sync(struct mtd_info *mtd)
{
}

static int __init exword_x11_mtd_init(void)
{
	exword_x11_mtd.name = "exword-x11";
	exword_x11_mtd.type = MTD_ROM;
	exword_x11_mtd.flags = MTD_CAP_ROM;
	exword_x11_mtd.size = EXWORD_X11_SIZE;
	exword_x11_mtd.erasesize = PAGE_SIZE;
	exword_x11_mtd.writesize = 1;
	exword_x11_mtd.writebufsize = 1;
	exword_x11_mtd._read = exword_x11_read;
	exword_x11_mtd._write = exword_x11_write;
	exword_x11_mtd._erase = exword_x11_erase;
	exword_x11_mtd._sync = exword_x11_sync;
	exword_x11_mtd.owner = THIS_MODULE;

	return mtd_device_register(&exword_x11_mtd, NULL, 0);
}
late_initcall(exword_x11_mtd_init);
#endif

static struct sh_machine_vector mv_exword __initmv = {
	.mv_name = "Casio EX-word DATAPLUS 6",
	.mv_setup = exword_setup,
	.mv_mem_reserve = exword_mem_reserve,
};

// SPDX-License-Identifier: GPL-2.0
/* Minimal Casio EX-word DATAPLUS 6 board support. */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/platform_device.h>
#include <linux/usb/r8a66597.h>

#if IS_BUILTIN(CONFIG_MTD)
#include <linux/mtd/mtd.h>
#include <asm/addrspace.h>
#endif

#include <asm/machvec.h>
#include <linux/sh_intc.h>

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

/*
 * The external USB connector is USB1.  Casio's firmware normally uses this
 * dual-role controller as a peripheral; Linux owns it after the add-in loader
 * jumps to the kernel and deliberately brings it up as a host.  Port power is
 * controlled by the controller's VBOUT bit, avoiding guesses about unrelated
 * EX-word GPIOs.  Use an OTG host adapter, and preferably a powered hub while
 * the board's available VBUS current is still being characterized.
 */
static struct r8a66597_platdata exword_usb1_host_data = {
	.on_chip = 1,
};

static struct resource exword_usb1_host_resources[] = {
	{
		.start = 0xa4d90000,
		.end = 0xa4d90124 - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = evt2irq(0xa40),
		.end = evt2irq(0xa40),
		.flags = IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device exword_usb1_host_device = {
	.name = "r8a66597_hcd",
	.id = 1,
	.dev = {
		.dma_mask = NULL,
		.coherent_dma_mask = 0xffffffff,
		.platform_data = &exword_usb1_host_data,
	},
	.num_resources = ARRAY_SIZE(exword_usb1_host_resources),
	.resource = exword_usb1_host_resources,
};

static int __init exword_devices_init(void)
{
	return platform_device_register(&exword_usb1_host_device);
}
arch_initcall(exword_devices_init);

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

#!/bin/sh
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 PATH_TO_DOT_CONFIG" >&2
	exit 2
fi

cfg=$1
kernel_tree=${KERNEL_TREE:-/work/linux-work/linux}
config_tool=$kernel_tree/scripts/config

enable_options() {
	for option in "$@"; do
		"$config_tool" --file "$cfg" --enable "$option"
	done
}

disable_options() {
	for option in "$@"; do
		"$config_tool" --file "$cfg" --disable "$option"
	done
}

# Start from the hardware-proven pre-lowmem configuration.  Keep its SLUB,
# SPARSEMEM, BASE_FULL, HZ=250, NO_HZ and high-resolution timer choices; none
# of those core boot paths are changed in this conservative X11 build.

# Remove facilities that are independent of allocator, memory model and time.
disable_options \
	CROSS_MEMORY_ATTACH UID16 SGETMASK_SYSCALL SYSFS_SYSCALL FHANDLE \
	AIO IO_URING IO_WQ COREDUMP KALLSYMS STACKTRACE SECCOMP \
	ALLOW_DEV_COREDUMP SCHED_DEBUG STACKDEPOT DUMP_CODE SLUB_DEBUG \
	STACKPROTECTOR_STRONG
enable_options STACKPROTECTOR PRINTK BUG

# The built-in archive and outer kernel both use gzip only.
disable_options RD_BZIP2 RD_LZMA RD_XZ RD_LZO RD_LZ4 RD_ZSTD
enable_options RD_GZIP INITRAMFS_COMPRESSION_GZIP

# No suspend or runtime firmware loading is used by the EX-word board port.
disable_options SUSPEND SUSPEND_FREEZER PM_SLEEP PM PM_CLK FREEZER \
	FW_LOADER FW_CACHE ALLOW_DEV_COREDUMP

# The custom keypad is direct MMIO.  These generic buses and input transports
# were present in the proven build but are unreachable on this appliance.
disable_options INPUT_FF_MEMLESS INPUT_VIVALDIFMAP KEYBOARD_ATKBD \
	INPUT_MOUSE MOUSE_PS2 SERIO SERIO_SERPORT SERIO_LIBPS2 HID HID_GENERIC

# Firmware leaves the required pins configured; the EX-word drivers use raw
# MMIO and do not register through these frameworks.
disable_options PINCTRL PINMUX PINCONF GENERIC_PINCONF PINCTRL_RENESAS \
	PINCTRL_SH_PFC PINCTRL_SH_PFC_GPIO PINCTRL_SH_FUNC_GPIO \
	PINCTRL_PFC_SH7724 GPIOLIB GPIO_CDEV GPIO_CDEV_V1 NVMEM NVMEM_SYSFS \
	IOMMU_SUPPORT

# Xfbdev needs Unix sockets and pseudoterminals, not packet networking.
enable_options NET UNIX UNIX98_PTYS BLOCK MISC_FILESYSTEMS \
	SQUASHFS SQUASHFS_FILE_DIRECT SQUASHFS_DECOMP_SINGLE SQUASHFS_XZ \
	SQUASHFS_EMBEDDED XZ_DEC FILE_LOCKING MEMFD_CREATE MTD MTD_BLOCK_RO
disable_options INET PACKET UNIX_DIAG NETLINK_DIAG NETFILTER NETDEVICES \
	MTD_BLOCK MTD_CHAR MTD_TESTS MTD_PARTITIONED_MASTER BLK_DEV_LOOP \
	SQUASHFS_FILE_CACHE SQUASHFS_DECOMP_MULTI \
	SQUASHFS_DECOMP_MULTI_PERCPU SQUASHFS_XATTR SQUASHFS_ZLIB \
	SQUASHFS_LZ4 SQUASHFS_LZO SQUASHFS_ZSTD SQUASHFS_4K_DEVBLK_SIZE \
	XZ_DEC_BCJ XZ_DEC_X86 XZ_DEC_POWERPC XZ_DEC_IA64 XZ_DEC_ARM \
	XZ_DEC_ARMTHUMB XZ_DEC_SPARC
"$config_tool" --file "$cfg" --set-val BLK_DEV_LOOP_MIN_COUNT 1
"$config_tool" --file "$cfg" --set-val SQUASHFS_FRAGMENT_CACHE_SIZE 1

# First restore a bootable X kernel.  Zram is deliberately left out of this
# recovery image; it can be reintroduced independently after hardware proof.
disable_options ZRAM ZSMALLOC_STAT ZRAM_WRITEBACK ZRAM_MEMORY_TRACKING ZSWAP

# Build the SH7724 storage path, but keep the EX-word platform device unbound
# until the firmware clock and pin handoff has been measured safely.  Do not
# enable pinctrl, GPIO, regulators or automatic mount/swap policy yet.
enable_options MMC MMC_BLOCK MMC_SDHI MMC_SDHI_SYS_DMAC
disable_options MMC_DEBUG MMC_TEST MMC_CRYPTO MMC_TMIO MMC_SH_MMCIF \
	MMC_SDHI_INTERNAL_DMAC DMADEVICES
"$config_tool" --file "$cfg" --set-val MMC_BLOCK_MINORS 8

# Retain the proven framebuffer, keyboard, VT and filesystem path.
enable_options SH_EXWORD KEYBOARD_EXWORD INPUT INPUT_KEYBOARD INPUT_EVDEV \
	TTY VT VT_CONSOLE FB FB_EXWORD FRAMEBUFFER_CONSOLE SH_TIMER_CMT \
	SH_TIMER_TMU BINFMT_ELF BINFMT_SCRIPT SYSCTL POSIX_TIMERS DEVMEM \
	PROC_FS PROC_SYSCTL PROC_PAGE_MONITOR SYSFS TMPFS SHMEM DEVTMPFS \
	DEVTMPFS_MOUNT FONTS
"$config_tool" --file "$cfg" --keep-case --enable FONT_8x8
"$config_tool" --file "$cfg" --keep-case --disable FONT_8x16

# Compact tables and logs without changing the allocator or memory topology.
disable_options CRC32_SLICEBY8 CRC32_SLICEBY4 CRC32_BIT DEBUG_MEMORY_INIT \
	DEBUG_MISC DEBUG_BUGVERBOSE SYMBOLIC_ERRNAME
enable_options CRC32 CRC32_SARWATE
"$config_tool" --file "$cfg" --set-val LOG_BUF_SHIFT 12
"$config_tool" --file "$cfg" --set-val PRINTK_SAFE_LOG_BUF_SHIFT 10

# The X11 SquashFS is loader-staged in reserved RAM and exposed by the board
# as a tiny read-only MTD device.  It is deliberately not embedded in cpio.
"$config_tool" --file "$cfg" --set-str INITRAMFS_SOURCE \
	"/work/linux-work/busybox-root-xsafe /work/linux-work/initramfs-devices.list"

# Xterm's compact runtime closure needs one more MiB after the X11 SquashFS.
# Relocate the compressed loader from 0x0cc00000 to 0x0cd00000.  Its measured
# end plus the 64 KiB gzip heap remains below 0x0ceac000, leaving over 1.3 MiB
# below the 0x0d000000 end of RAM.  The decompressed kernel stays at 0x0c400000.
"$config_tool" --file "$cfg" --set-val BOOT_LINK_OFFSET 0x00900000
"$config_tool" --file "$cfg" --set-str CMDLINE \
	"console=tty1 init=/init loglevel=7 fbcon=map:0 initramfs_async=0"

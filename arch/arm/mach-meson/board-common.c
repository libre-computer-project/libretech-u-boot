// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Beniamino Galvani <b.galvani@gmail.com>
 */

#include <common.h>
#include <cpu_func.h>
#include <fastboot.h>
#include <fs.h>
#include <ini.h>
#include <init.h>
#include <net.h>
#include <asm/arch/boot.h>
#include <env.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/ptrace.h>
#include <linux/libfdt.h>
#include <linux/err.h>
#include <asm/arch/mem.h>
#include <asm/arch/sm.h>
#include <asm/armv8/mmu.h>
#include <asm/unaligned.h>
#include <efi_loader.h>
#include <u-boot/crc.h>
#include <splash.h>
#include <vsprintf.h>

#include <asm/psci.h>

DECLARE_GLOBAL_DATA_PTR;

__weak int board_init(void)
{
	return 0;
}

int dram_init(void)
{
	const fdt64_t *val;
	int offset;
	int len;

	offset = fdt_path_offset(gd->fdt_blob, "/memory");
	if (offset < 0)
		return -EINVAL;

	val = fdt_getprop(gd->fdt_blob, offset, "reg", &len);
	if (len < sizeof(*val) * 2)
		return -EINVAL;

	/* Use unaligned access since cache is still disabled */
	gd->ram_size = get_unaligned_be64(&val[1]);

	return 0;
}

__weak int meson_ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	meson_init_reserved_memory(blob);

	return meson_ft_board_setup(blob, bd);
}

void meson_board_add_reserved_memory(void *fdt, u64 start, u64 size)
{
	int ret;

	ret = fdt_add_mem_rsv(fdt, start, size);
	if (ret)
		printf("Could not reserve zone @ 0x%llx\n", start);

	if (IS_ENABLED(CONFIG_EFI_LOADER))
		efi_add_memory_map(start, size, EFI_RESERVED_MEMORY_TYPE);
}

int meson_generate_serial_ethaddr(void)
{
	u8 mac_addr[ARP_HLEN];
	char serial[SM_SERIAL_SIZE];
	u32 sid;
	u16 sid16;

	if (!meson_sm_get_serial(serial, SM_SERIAL_SIZE)) {
		sid = crc32(0, (unsigned char *)serial, SM_SERIAL_SIZE);
		sid16 = crc16_ccitt(0, (unsigned char *)serial,	SM_SERIAL_SIZE);

		/* Ensure the NIC specific bytes of the mac are not all 0 */
		if ((sid & 0xffffff) == 0)
			sid |= 0x800000;

		/* Non OUI / registered MAC address */
		mac_addr[0] = ((sid16 >> 8) & 0xfc) | 0x02;
		mac_addr[1] = (sid16 >>  0) & 0xff;
		mac_addr[2] = (sid >> 24) & 0xff;
		mac_addr[3] = (sid >> 16) & 0xff;
		mac_addr[4] = (sid >>  8) & 0xff;
		mac_addr[5] = (sid >>  0) & 0xff;

		eth_env_set_enetaddr("ethaddr", mac_addr);
	} else
		return -EINVAL;

	return 0;
}

static void meson_set_boot_source(void)
{
	const char *source;

	switch (meson_get_boot_device()) {
	case BOOT_DEVICE_EMMC:
		source = "emmc";
		break;

	case BOOT_DEVICE_NAND:
		source = "nand";
		break;

	case BOOT_DEVICE_SPI:
		source = "spi";
		break;

	case BOOT_DEVICE_SD:
		source = "sd";
		break;

	case BOOT_DEVICE_USB:
		source = "usb";
		break;

	default:
		source = "unknown";
	}

	env_set("boot_source", source);
}

#ifdef CONFIG_SYS_MMC_ENV_DEV
int mmc_get_env_dev(void)
{
	if (meson_get_boot_device() == BOOT_DEVICE_SD)
		return 1;
	
	return CONFIG_SYS_MMC_ENV_DEV;
}
#endif

void env_set_bootdevice(void){
	char *bootdev = simple_itoa(meson_get_boot_device() == BOOT_DEVICE_SD);
	env_set("bootdevice", bootdev);
#ifdef CONFIG_SPLASH_SOURCE
	env_set("splashdevpart", bootdev);
#endif
}

__weak int meson_board_late_init(void)
{
	return 0;
}

int board_late_init(void)
{
	meson_set_boot_source();

	return meson_board_late_init();
}

#ifdef CONFIG_SPLASH_SCREEN
static struct splash_location splash_locations[] = {
	{
		.name = "mmc_fs",
		.storage = SPLASH_STORAGE_MMC,
		.flags = SPLASH_STORAGE_FS,
		.devpart = "0:auto",
	}
};

int splash_screen_prepare(void)
{
	if (CONFIG_IS_ENABLED(SPLASH_SOURCE))
		return splash_source_load(splash_locations,
			ARRAY_SIZE(splash_locations)) && splash_video_logo_load();
	return splash_video_logo_load();
}
#endif

#ifdef CONFIG_CMD_INI
void env_ini_load(){
	int res;
	char *load_addr;
	loff_t size;
	res = fs_set_blk_dev("mmc", env_get("bootdevice"), FS_TYPE_ANY);
	if (res) return;
	load_addr = (char *)hextoul(env_get("loadaddr"), NULL);
	res = fs_read("boot.ini",load_addr, 0, 0, &size);
	if (res) return;
	ini_parse(load_addr, size, ini_handler, "");
}
#endif


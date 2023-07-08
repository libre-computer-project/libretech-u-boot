// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <fs.h>
#include <ini.h>
#include <asm/arch/boot.h>
#include <env.h>
#include <linux/kernel.h>
#include <vsprintf.h>
#include <event.h>
#include <splash.h>
#include "board.h"

#ifdef CONFIG_SYS_MMC_ENV_DEV
int mmc_get_env_dev(void)
{
	int bootdevice_num = CONFIG_SYS_MMC_ENV_DEV;

	switch (meson_get_boot_device()) {
		case BOOT_DEVICE_EMMC:
			break;

		case BOOT_DEVICE_SPI:
			break;

		case BOOT_DEVICE_SD:
			bootdevice_num = 1;
			break;

		case BOOT_DEVICE_USB:
			break;
	}

	env_set_ulong("bootdevice", bootdevice_num);

	return bootdevice_num;
}
#endif

#ifdef CONFIG_SPLASH_SCREEN
static struct splash_location splash_locations[] = {
	{
		.name = "mmc_fs",
		.storage = SPLASH_STORAGE_MMC,
		.flags = SPLASH_STORAGE_FS,
		/* .devpart = "", # taken from env */
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
void env_ini_load(char *bootsource, char *bootdevice){
	int res;
	ulong load_addr;
	loff_t size;
	res = fs_set_blk_dev(bootsource, bootdevice, FS_TYPE_ANY);
	if (res) return;
	load_addr = env_get_ulong("loadaddr", 16, CONFIG_SYS_LOAD_ADDR);
	res = fs_read("boot.ini", load_addr, 0, 0, &size);
	if (res) return;
	ini_parse((char *)load_addr, size, ini_handler, "");
}
#endif

static int settings_r(void){
	int bootdevice_num = CONFIG_SYS_MMC_ENV_DEV;
	
	char *bootsource;
	char *bootdevice;

	switch (meson_get_boot_device()) {
		case BOOT_DEVICE_EMMC:
			bootsource = "mmc";
			break;

		case BOOT_DEVICE_SPI:
			/* TODO: add search function for mmc, usb, nvme */
			bootsource = "mmc";
			break;

		case BOOT_DEVICE_SD:
			bootsource = "mmc";
			bootdevice_num = 1;
			break;

		case BOOT_DEVICE_USB:
			bootsource = "usb";
			break;
	}

	bootdevice = simple_itoa(bootdevice_num);

#ifdef CONFIG_SPLASH_SOURCE
	env_set("splashdevpart", bootdevice);
#endif
#ifdef CONFIG_CMD_INI
	if (strcmp(bootsource, "mmc") == 0)
		env_ini_load(bootsource, bootdevice);
#endif
	return 0;
}
EVENT_SPY_SIMPLE(EVT_SETTINGS_R, settings_r);

/*
int meson_board_late_init(void){
	return 0;
}
*/

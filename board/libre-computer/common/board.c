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
#include <dm.h>
#include <mmc.h>
#include <event.h>
#include <splash.h>
#include <linux/stringify.h>
#include "board.h"

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

bool mmc_is_present(int seq){
	struct udevice *dev;
	struct mmc *mmc;
	uclass_get_device_by_seq(UCLASS_MMC, seq, &dev);
	return dev && (mmc = mmc_get_mmc_dev(dev)) && !mmc_init(mmc);
}

/*
int meson_board_late_init(void){
	return 0;
}
*/

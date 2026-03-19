// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 *
 * Rockchip-specific Libre Computer board support.
 * Provides EVT_SETTINGS_R handler for boot device detection,
 * splash partition setup, and boot.ini loading.
 */

#include <dm.h>
#include <env.h>
#include <ini.h>
#include <mmc.h>
#include <event.h>
#include <vsprintf.h>
#include "board.h"

static int settings_r(void)
{
	const char *boot_device;
	struct udevice *dev;
	int bootdevice_num = CONFIG_ENV_MMC_DEVICE_INDEX;
	char *bootdevice;

	boot_device = ofnode_read_chosen_string("u-boot,spl-boot-device");
	if (boot_device) {
		if (!uclass_get_device_by_ofnode(UCLASS_MMC,
				ofnode_path(boot_device), &dev))
			bootdevice_num = dev->seq_;
	}

	bootdevice = simple_itoa(bootdevice_num);

	env_set("bootdevice", bootdevice);
#ifdef CONFIG_SPLASH_SOURCE
	env_set("splashdevpart", bootdevice);
#endif
#ifdef CONFIG_CMD_INI
	env_ini_load("mmc", bootdevice);
#endif
	return 0;
}
EVENT_SPY_SIMPLE(EVT_SETTINGS_R, settings_r);

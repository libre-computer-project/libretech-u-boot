// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <dm.h>
#include <env.h>
#include <ini.h>
#include <linux/stringify.h>
#include <mmc.h>
#include <event.h>
#include <vsprintf.h>
#include "board.h"

extern uint32_t sunxi_get_boot_device(void);

#ifdef CONFIG_ENV_MMC_DEVICE_INDEX
int mmc_get_env_dev(void)
{
	debug("%s\n", __func__);
	switch (sunxi_get_boot_device()) {
	case BOOT_DEVICE_MMC1:
		return 1;
	default:
		return CONFIG_ENV_MMC_DEVICE_INDEX;
	}
}
#endif

static int settings_r(void)
{
	int bootdevice_num = CONFIG_ENV_MMC_DEVICE_INDEX;
	char *bootdevice;

	switch (sunxi_get_boot_device()) {
	case BOOT_DEVICE_MMC1:
		bootdevice_num = 1;
		break;
	case BOOT_DEVICE_MMC2:
		bootdevice_num = 0;
		break;
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

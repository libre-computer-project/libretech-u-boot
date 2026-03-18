// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 *
 * Sunxi-specific Libre Computer board support.
 * Provides EVT_SETTINGS_R handler for boot device detection,
 * splash partition setup, and boot.ini loading.
 *
 * Note: mmc_get_env_dev() is handled by modifying board/sunxi/board.c
 * directly since sunxi board code is monolithic and cannot be replaced
 * by a vendor board directory without losing essential platform functions.
 */

#include <dm.h>
#include <env.h>
#include <ini.h>
#include <mmc.h>
#include <event.h>
#include <vsprintf.h>
#include "board.h"

extern uint32_t sunxi_get_boot_device(void);

static int settings_r(void)
{
	int bootdevice_num = 0;
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

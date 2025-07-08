// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#define DEBUG

#include <asm/arch/boot.h>
#include <asm/arch/gx.h>
#include <asm/arch/sm.h>
#include <asm/arch/eth.h>
#include <dm.h>
#include <env.h>
#include <ini.h>
#include <linux/stringify.h>
#include <mmc.h>
#include <net.h>
#include "board.h"
#include "board-meson.h"

#define EFUSE_SN_OFFSET		20
#define EFUSE_SN_SIZE		16
#define EFUSE_MAC_OFFSET	52
#define EFUSE_MAC_SIZE		6

#ifdef CONFIG_SYS_MMC_ENV_DEV
int mmc_get_env_dev(void)
{
	debug("%s\n", __func__);
	int bootdevice_num = CONFIG_SYS_MMC_ENV_DEV;
	switch (meson_get_boot_device()) {
		case BOOT_DEVICE_EMMC:
			break;

		case BOOT_DEVICE_SPI:
		case BOOT_DEVICE_USB:
			/* TODO: eMMC, NVME, SD, USB */
			if (!mmc_is_present(bootdevice_num))
				bootdevice_num = mmc_is_present(!bootdevice_num);
			break;
		case BOOT_DEVICE_SD:
			bootdevice_num = 1;
			break;
	}

	env_set_ulong("bootdevice", bootdevice_num);

	return bootdevice_num;
}
#endif

#if IS_ENABLED(CONFIG_SET_DFU_ALT_INFO)
#define DFU_ALT_INFO_ENV_LENGTH 512
#define DFU_INTERFACE_SEPARATOR "&"
#ifdef CONFIG_DFU_RAM
#define MESON_DFU_RAM_ALTS \
	"load ram " __stringify(CONFIG_SYS_LOAD_ADDR) " " __stringify(CONFIG_SYS_BOOTM_LEN) ";" \
	"script script " SCRIPT_ADDR_R " 0x8000;" \
	"fdt ram " FDT_ADDR_R " 0x78000;" \
	"kernel ram " KERNEL_ADDR_R " 0x7f80000;" \
	"ramdisk ram " RAMDISK_ADDR_R " 0x4000000"
#define MESON_DFU_RAM "ram 0=" MESON_DFU_RAM_ALTS
#else
#define MESON_DFU_RAM
#endif
#ifdef CONFIG_DFU_SF
#define MESON_DFU_SF_ALTS \
	"u-boot-bin raw 0 0x10000"
#define MESON_DFU_SF "sf 0:0=" MESON_DFU_SF_ALTS "&"
#else
#define MESON_DFU_SF
#endif
#ifdef CONFIG_DFU_MMC
#define MESON_DFU_MMC_EMMC_ALTS \
	"u-boot-bin-emmc raw 1 0x7ff mmcpart 0;" \
	"u-boot-bin-emmc-boot0 raw 0x1 0x7ff mmcpart 1;" \
	"u-boot-bin-emmc-boot1 raw 0x1 0x7ff mmcpart 2;" \
	"emmc raw 0 0 mmcpart 0;" \
	"emmc-boot0 raw 0 0 mmcpart 1;" \
	"emmc-boot1 raw 0 0 mmcpart 2"
#define MESON_DFU_MMC_EMMC "mmc 0=" MESON_DFU_MMC_EMMC_ALTS "&"

#ifdef MESON_DFU_MMC_SD_HIDE
#define MESON_DFU_MMC_SD_ALTS
#define MESON_DFU_MMC_SD
#else
#define MESON_DFU_MMC_SD_ALTS \
	"u-boot-bin-sd raw 1 0x7ff mmcpart 0;" \
	"sd raw 0 0 mmcpart 0"
#define MESON_DFU_MMC_SD "mmc 1=" MESON_DFU_MMC_SD_ALTS "&"
#endif
#else
#define MESON_DFU_MMC_EMMC
#define MESON_DFU_MMC_SD
#endif

void meson_set_dfu_alt_info(char *interface, char *devstr)
{
	debug("%s: interface %s devstr %s\n", __func__, interface, devstr);
	char *dfu_alt_info;
	bool free_dfu_alt_info = false;
	if (interface){
#ifdef CONFIG_DFU_RAM
		if (strcmp(interface, "ram") == 0)
			dfu_alt_info = MESON_DFU_RAM_ALTS;
#endif
#ifdef CONFIG_DFU_SF
		if (strcmp(interface, "sf") == 0)
			dfu_alt_info = MESON_DFU_SF_ALTS;
#endif
#ifdef CONFIG_DFU_MMC
		if (strcmp(interface, "mmc") == 0){
			if (!devstr){
				dfu_alt_info = malloc(DFU_ALT_INFO_ENV_LENGTH);
				free_dfu_alt_info = true;
				if (mmc_is_present(0))
					strcat(dfu_alt_info, MESON_DFU_MMC_EMMC);
#ifndef MESON_DFU_MMC_SD_HIDE
				if (mmc_is_present(1))
					strcat(dfu_alt_info, MESON_DFU_MMC_SD);
#endif
			}
			else if (strcmp(devstr, "0") == 0 && mmc_is_present(0))
				dfu_alt_info = MESON_DFU_MMC_EMMC_ALTS;
#ifndef MESON_DFU_MMC_SD_HIDE
			else if (strcmp(devstr, "1") == 0 && mmc_is_present(1))
				dfu_alt_info = MESON_DFU_MMC_SD_ALTS;
#endif
		}
#endif
	} else {
		dfu_alt_info = malloc(DFU_ALT_INFO_ENV_LENGTH);
		free_dfu_alt_info = true;
#ifdef CONFIG_DFU_SF
		strcpy(dfu_alt_info, MESON_DFU_SF);
#endif
		if (mmc_is_present(0))
			strcat(dfu_alt_info, MESON_DFU_MMC_EMMC);
#ifndef MESON_DFU_MMC_SD_HIDE
		if (mmc_is_present(1))
					strcat(dfu_alt_info, MESON_DFU_MMC_SD);
#endif
		strcat(dfu_alt_info, MESON_DFU_RAM);
	}
	
	if (dfu_alt_info)
		env_set("dfu_alt_info", dfu_alt_info);
	if (free_dfu_alt_info)
		free(dfu_alt_info);
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
			/* TODO: eMMC, NVME, SD, USB */
			bootsource = "mmc";
			if (!mmc_is_present(bootdevice_num))
				bootdevice_num = mmc_is_present(!bootdevice_num);
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

	env_set("bootdevice", bootdevice);
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

int meson_gxl_misc_init_r(void)
{
	u8 mac_addr[EFUSE_MAC_SIZE + 1];
	char serial[EFUSE_SN_SIZE + 1];
	ssize_t len;

	if (!eth_env_get_enetaddr("ethaddr", mac_addr)) {
		len = meson_sm_read_efuse(EFUSE_MAC_OFFSET,
					  mac_addr, EFUSE_MAC_SIZE);
		mac_addr[len] = '\0';
		if (len == EFUSE_MAC_SIZE && is_valid_ethaddr(mac_addr))
			eth_env_set_enetaddr("ethaddr", mac_addr);
		else
			meson_generate_serial_ethaddr();
	}

	if (!env_get("serial#")) {
		len = meson_sm_read_efuse(EFUSE_SN_OFFSET, serial,
			EFUSE_SN_SIZE);
		serial[len] = '\0';
		if (len == EFUSE_SN_SIZE)
			env_set("serial#", serial);
	}

	return 0;
}

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for LibreTech AC
 *
 * Copyright (C) 2017 Baylibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define LC_SPI_NOR
#define LC_SPI_NOR_SIZE 0x1000000


#define LC_ETHEREALOS
#define LC_ETHEREALOS_OFFSET 0x100000
#define LC_ETHEREALOS_SIZE 0x0

#ifdef CONFIG_CMD_BOOTMENU
#define LC_BOOTMENU_ITEMS_ENV \
	"bootmenu_0=Boot=" \
		"boot; " \
		"echo \"Boot failed.\"; " \
		"sleep 5; " \
		"$menucmd\0" \
	"bootmenu_1=Boot USB=" \
		"run bootcmd_usb0; " \
		"echo \"USB Boot failed.\"; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_2=Boot eMMC=" \
		"run bootcmd_mmc0; " \
		"run bootcmd_mmc0.1; " \
		"run bootcmd_mmc0.2; " \
		"echo \"eMMC Boot failed.\"; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_3=Boot PXE=" \
		"run bootcmd_pxe; " \
		"echo \"PXE Boot failed.\"; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_4=Boot DHCP=" \
		"run bootcmd_dhcp; " \
		"echo \"DHCP Boot failed.\"; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_7=eMMC USB Drive Mode=" \
		"mmc list; " \
		"if mmc dev 0; then " \
			"echo \"Press Control+C to end USB Drive Mode.\"; " \
			"ums 0 mmc 0; " \
			"echo \"USB Drive Mode ended.\"; " \
		"else " \
			"echo \"eMMC not detected.\"; " \
		"fi; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_8=Detect USB Devices=" \
		"if usb reset; then " \
			"echo \"USB detection complete.\"; " \
		"else echo \"USB detection failed.\"; " \
		"fi; " \
		"sleep 5; " \
		"$menucmd -1\0" \
	"bootmenu_9=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0"

#ifdef LC_ETHEREALOS
#define LC_ETHEREALOS_BOOTMENU_ITEMS \
	"bootmenu_5=Boot LOST=" \
		"env set bootargs \"lost\"; run bootcmd_etherealos; echo \"LOST Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_6=Boot EtherealOS=" \
		"run bootcmd_etherealos; echo \"EtherealOS Boot failed.\"; sleep 5; $menucmd -1\0"
#else
#define LC_ETHEREALOS_BOOTMENU_ITEMS \
	"bootmenu_5==$menucmd -1\0" \
	"bootmenu_6==$menucmd -1\0"
#endif
#else
#define LC_BOOTMENU_ITEMS_ENV
#endif

#include <configs/libretech.h>

#define BOOT_TARGET_DEVICES(func) \
	func(ROMUSB, romusb, na)  \
	func(MMC, mmc, 0) \
	func(MMC, mmc, 0.1) \
	func(MMC, mmc, 0.2) \
	BOOT_TARGET_DEVICES_USB(func) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)

#define CONFIG_EXTRA_ENV_SETTINGS \
	"stdin=" STDIN_CFG "\0" \
	"stdout=" STDOUT_CFG "\0" \
	"stderr=" STDOUT_CFG "\0" \
	"fdt_addr_r=0x08008000\0" \
	"scriptaddr=0x08000000\0" \
	"kernel_addr_r=0x08080000\0" \
	"kernel_comp_addr_r=0x10000000\0" \
	"kernel_comp_size=0x03000000\0" \
	"pxefile_addr_r=0x01080000\0" \
	"fdtoverlay_addr_r=0x01000000\0" \
	"ramdisk_addr_r=0x13000000\0" \
	"fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	SPLASH_ENV \
	LC_ENV \
	BOOTENV


#include <configs/meson64.h>

#endif /* __CONFIG_H */

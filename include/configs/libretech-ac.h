/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for LibreTech AC
 *
 * Copyright (C) 2017 Baylibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define BOOT_TARGET_DEVICES(func) \
	func(ROMUSB, romusb, na)  \
	func(MMC, mmc, 0) \
	BOOT_TARGET_DEVICES_USB(func) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)

#ifdef CONFIG_CMD_BOOTMENU
#define ETHEREALOS
#ifdef ETHEREALOS
#define ETHEREALOS_OFFSET 0x100000
#define ETHEREALOS_SIZE 0x0
#define ETHEREALOS_BOOTMENU_ITEM "bootmenu_5=Boot EtherealOS=run bootcmd_etherealos; echo \"EtherealOS Boot failed.\"; sleep 5; $menucmd -1\0"
#define ETHEREALOS_BOOTCMD "bootcmd_etherealos=if test " __stringify(ETHEREALOS_SIZE) " -gt 0; then if sf probe && sf read $pxefile_addr_r " __stringify(ETHEREALOS_OFFSET) " " __stringify (ETHEREALOS_SIZE) "; then bootm $pxefile_addr_r; fi; else echo Ethereal OS not available.; fi\0"
#else
#define ETHEREALOS_BOOTMENU_ITEM "bootmenu_5==$menucmd -1\0"
#define ETHEREALOS_BOOTCMD
#endif
#define BOOTMENU_ITEMS_ENV \
	"bootmenu_0=Boot=boot; echo \"Boot failed.\"; sleep 30; $menucmd\0" \
	"bootmenu_1=Boot USB=run bootcmd_usb0; echo \"USB Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_2=Boot eMMC=run bootcmd_mmc0; echo \"eMMC Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_3=Boot PXE=run bootcmd_pxe; echo \"PXE Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_4=Boot DHCP=run bootcmd_dhcp; echo \"DHCP Boot failed.\"; sleep 5; $menucmd -1\0" \
	ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_6=eMMC USB Drive Mode=mmc list; if mmc dev 0; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 0; echo \"USB Drive Mode ended.\"; else echo \"eMMC not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_7=Detect USB Devices=if usb reset; then echo \"USB detection complete.\"; else echo \"USB detection failed.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_8=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0"
#else
#define ETHEREALOS_BOOTCMD
#define BOOTMENU_ITEMS_ENV
#endif

#ifdef CONFIG_CMD_BMP
#define CONFIG_SPLASH_SOURCE
#define CONFIG_SPLASHIMAGE_GUARD
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (512*512*4)
#define CONFIG_VIDEO_BMP_GZIP
#define CONFIG_VIDEO_LOGO
#define SPLASH_ENV \
	"splashimage=0x08080000\0" \
	"splashpos=m,m\0" \
	"splashfile=boot.bmp\0" \
	"splashsource=mmc_fs\0"
#else
#define SPLASH_ENV
#endif

#define CONFIG_EXTRA_ENV_SETTINGS \
	"stdin=" STDIN_CFG "\0" \
	"stdout=" STDOUT_CFG "\0" \
	"stderr=" STDOUT_CFG "\0" \
	"fdt_addr_r=0x08008000\0" \
	"scriptaddr=0x08000000\0" \
	"kernel_addr_r=0x08080000\0" \
	"pxefile_addr_r=0x01080000\0" \
	"ramdisk_addr_r=0x13000000\0" \
	"lc_fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
	"fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	ETHEREALOS_BOOTCMD \
	BOOTMENU_ITEMS_ENV \
	SPLASH_ENV \
	BOOTENV

#ifdef CONFIG_CMD_MEMTEST
#define CONFIG_SYS_MEMTEST_START 0x08000000
#define CONFIG_SYS_MEMTEST_END 0x1affffff
#endif

#include <configs/meson64.h>

#endif /* __CONFIG_H */

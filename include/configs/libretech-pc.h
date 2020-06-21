/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for LibreTech PC
 *
 * Copyright (C) 2017 Baylibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define BOOT_TARGET_DEVICES(func) \
	func(ROMUSB, romusb, na)  \
	func(MMC, mmc, 0) \
	func(MMC, mmc, 1) \
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
        "pxefile_addr_r=0x01080000\0" \
        "ramdisk_addr_r=0x13000000\0" \
        "lc_fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
        "fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	"bootmenu_0=Boot=boot; echo \"Boot failed.\"; sleep 30; $menucmd\0" \
	"bootmenu_1=Boot USB=run bootcmd_usb0; echo \"USB Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_2=Boot SD Card=run bootcmd_mmc0; echo \"SD Card Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_3=Boot eMMC=run bootcmd_mmc1; echo \"eMMC Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_4=Boot PXE=run bootcmd_pxe; echo \"PXE Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_5=Boot DHCP=run bootcmd_dhcp; echo \"DHCP Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_6=SD USB Drive Mode=mmc list; if mmc dev 0; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 0; echo \"USB Drive Mode ended.\"; else echo \"SD not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_7=eMMC USB Drive Mode=mmc list; if mmc dev 1; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 1; echo \"USB Drive Mode ended.\"; else echo \"eMMC not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_8=Detect USB Devices; if usb reset; then echo \"USB detection complete.\"; else echo \"USB detection failed.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_9=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0" \
	"splashimage=0x08080000\0" \
	"splashpos=m,m\0" \
	"splashfile=boot.bmp\0" \
	"splashsource=mmc_fs\0" \
        BOOTENV

#define CONFIG_SPLASH_SOURCE
#define CONFIG_SPLASHIMAGE_GUARD
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (512*512*4)
#define CONFIG_VIDEO_BMP_GZIP
#define CONFIG_VIDEO_LOGO
#define CONFIG_SYS_MEMTEST_START 0x08000000
#define CONFIG_SYS_MEMTEST_END 0xbaffffff
#include <configs/meson64.h>

#endif /* __CONFIG_H */

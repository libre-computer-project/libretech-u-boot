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
	func(MMC, mmc, 0.1) \
	func(MMC, mmc, 0.2) \
	BOOT_TARGET_DEVICES_USB(func) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)

#define SPI_NOR_SIZE 0x1000000
#define SPI_NOR_SIZE_ENV "spi_nor_size=" __stringify(SPI_NOR_SIZE) "\0"

#define ETHEREALOS

#ifdef ETHEREALOS
#define ETHEREALOS_OFFSET 0x100000
#define ETHEREALOS_SIZE 0x0
#define ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_5=Boot LOST=env set bootargs \"noinitrd lost\"; run bootcmd_etherealos; echo \"LOST Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_6=Boot EtherealOS=env set bootargs noinitrd; run bootcmd_etherealos; echo \"EtherealOS Boot failed.\"; sleep 5; $menucmd -1\0"
#define ETHEREALOS_ENV "bootcmd_etherealos=if test " __stringify(ETHEREALOS_SIZE) " -gt 0; then if sf probe && sf read $pxefile_addr_r " __stringify(ETHEREALOS_OFFSET) " " __stringify (ETHEREALOS_SIZE) "; then bootm $pxefile_addr_r; fi; else echo OS not available.; fi\0"
#else
#define ETHEREALOS_ENV
#define ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_5==$menucmd -1\0" \
	"bootmenu_6==$menucmd -1\0"
#endif

#ifdef CONFIG_CMD_BOOTMENU
#define BOOTMENU_ITEMS_ENV \
	"bootmenu_0=Boot=boot; echo \"Boot failed.\"; sleep 5; $menucmd\0" \
	"bootmenu_1=Boot USB=run bootcmd_usb0; echo \"USB Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_2=Boot eMMC=run bootcmd_mmc0; echo \"eMMC Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_3=Boot PXE=run bootcmd_pxe; echo \"PXE Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_4=Boot DHCP=run bootcmd_dhcp; echo \"DHCP Boot failed.\"; sleep 5; $menucmd -1\0" \
	ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_7=eMMC USB Drive Mode=mmc list; if mmc dev 0; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 0; echo \"USB Drive Mode ended.\"; else echo \"eMMC not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_8=Detect USB Devices=if usb reset; then echo \"USB detection complete.\"; else echo \"USB detection failed.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_9=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0"
#else
#define BOOTMENU_ITEMS_ENV
#endif

#define SELF_TEST_SPI_READ \
	"self_test_spi_read=sf probe; " \
	"setexpr self_test_spi_read_i 0; " \
	"sf read $kernel_addr_r 0 $spi_nor_size; " \
	"crc32 $kernel_addr_r $spi_nor_size $ramdisk_addr_r; " \
	"while test $? -eq 0; do " \
		"setexpr self_test_spi_read_i $self_test_spi_read_i + 1; " \
		"sf read $kernel_addr_r 0 $spi_nor_size; " \
		"crc32 $kernel_addr_r $spi_nor_size $pxefile_addr_r; " \
		"cmp.w $ramdisk_addr_r $pxefile_addr_r 1; " \
	"done; " \
	"echo \"SELF TEST SPI FAILED AT ITERATION $self_test_spi_read_i\"\0" 

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
	"kernel_comp_addr_r=0x10000000\0" \
	"kernel_comp_size=0x03000000\0" \
	"pxefile_addr_r=0x01080000\0" \
	"ramdisk_addr_r=0x13000000\0" \
	"lc_fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
	"fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	SPI_NOR_SIZE_ENV \
	ETHEREALOS_ENV \
	BOOTMENU_ITEMS_ENV \
	SELF_TEST_SPI_READ \
	SPLASH_ENV \
	BOOTENV

#include <configs/meson64.h>

#endif /* __CONFIG_H */

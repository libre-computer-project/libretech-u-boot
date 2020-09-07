/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for LibreTech CC V2
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
	func(MMC, mmc, 1.1) \
	func(MMC, mmc, 1.2) \
	BOOT_TARGET_DEVICES_USB(func) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)

#define LC_SPI_NOR
#define LC_ETHEREALOS

#ifdef LC_SPI_NOR

#define LC_SPI_NOR_SIZE 0x1000000
#define LC_SPI_NOR_READ_TEST \
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
#define LC_SPI_NOR_ENV "spi_nor_size=" __stringify(LC_SPI_NOR_SIZE) "\0" \
	LC_SPI_NOR_READ_TEST

#else

#define LC_SPI_NOR_ENV

#endif

#ifdef LC_ETHEREALOS

#define LC_ETHEREALOS_OFFSET 0x100000
#define LC_ETHEREALOS_SIZE 0x0
#define LC_ETHEREALOS_TEST_LOAD_SPI "etherealos_test_load_spi=if test $boot_source = \"spi\"; then sf probe && sf read $pxefile_addr_r $etherealos_offset $etherealos_size; fi\0"
#define LC_ETHEREALOS_COMPUTE_BLK "etherealos_compute_blk=setexpr etherealos_offset_blk $etherealos_offset / 200; setexpr etherealos_size_blk $etherealos_size / 200\0"
#define LC_ETHEREALOS_TEST_LOAD_SD "etherealos_test_load_sd=if test $boot_source = \"sd\"; then run etherealos_compute_blk; read mmc 0 $pxefile_addr_r $etherealos_offset_blk $etherealos_size_blk; fi\0"
#define LC_ETHEREALOS_TEST_LOAD_EMMC "etherealos_test_load_emmc=if test $boot_source = \"emmc\"; then run etherealos_compute_blk; read mmc 1 $pxefile_addr_r $etherealos_offset_blk $etherealos_size_blk; fi\0"
#define LC_ETHEREALOS_TEST_LOAD_EMMC_BOOT0 "etherealos_test_load_emmc_boot0=if test $boot_source = \"emmc\"; then run etherealos_compute_blk; read mmc 1.1 $pxefile_addr_r $etherealos_offset_blk $etherealos_size_blk; fi\0"
#define LC_ETHEREALOS_TEST_LOAD_EMMC_BOOT1 "etherealos_test_load_emmc_boot1=if test $boot_source = \"emmc\"; then run etherealos_compute_blk; read mmc 1.2 $pxefile_addr_r $etherealos_offset_blk $etherealos_size_blk; fi\0"
#define LC_ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_6=Boot LOST=env set bootargs \"lost\"; run bootcmd_etherealos; echo \"LOST Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_7=Boot EtherealOS=run bootcmd_etherealos; echo \"EtherealOS Boot failed.\"; sleep 5; $menucmd -1\0"
#define LC_ETHEREALOS_BOOTCMD "bootcmd_etherealos=if test $etherealos_size -gt 0; then run etherealos_test_load_spi; if test $? -eq 0; then bootm $pxefile_addr_r; fi; else echo OS not available.; fi\0"
#define LC_ETHEREALOS_ENV \
	"etherealos_offset=" __stringify(LC_ETHEREALOS_OFFSET) "\0" \
	"etherealos_size=" __stringify(LC_ETHEREALOS_SIZE) "\0" \
	LC_ETHEREALOS_TEST_LOAD_SPI \
	LC_ETHEREALOS_BOOTCMD \
	LC_ETHEREALOS_BOOTMENU_ITEM \
	LC_ETHEREALOS_TEST_LOAD_EMMC_BOOT0 \
	LC_ETHEREALOS_TEST_LOAD_EMMC_BOOT1 \
	LC_ETHEREALOS_BOOTMENU_ITEM \
	LC_ETHEREALOS_BOOTCMD

#else

#define LC_ETHEREALOS_BOOTMENU_ITEM \
	"bootmenu_6==$menucmd -1\0" \
	"bootmenu_7==$menucmd -1\0"
#define LC_ETHEREALOS_ENV LC_ETHEREALOS_BOOTMENU_ITEM

#endif

#ifdef CONFIG_CMD_BOOTMENU
#define LC_BOOTMENU_ITEMS_ENV \
	"bootmenu_0=Boot=boot; echo \"Boot failed.\"; sleep 5; $menucmd\0" \
	"bootmenu_1=Boot USB=run bootcmd_usb0; echo \"USB Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_2=Boot eMMC=run bootcmd_mmc1; run bootcmd_mmc1.1; run bootcmd_mmc1.2; echo \"eMMC Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_3=Boot SD Card=run bootcmd_mmc0; echo \"SD Card Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_4=Boot PXE=run bootcmd_pxe; echo \"PXE Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_5=Boot DHCP=run bootcmd_dhcp; echo \"DHCP Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_8=eMMC USB Drive Mode=mmc list; if mmc dev 1; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 1; echo \"USB Drive Mode ended.\"; else echo \"eMMC not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_9=SD Card USB Drive Mode=mmc list; if mmc dev 0; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 0; echo \"USB Drive Mode ended.\"; else echo \"SD not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_10=Detect USB Devices=if usb reset; then echo \"USB detection complete.\"; else echo \"USB detection failed.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_11=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0"
#else
#define LC_BOOTMENU_ITEMS_ENV
#endif

#define LC_ENV LC_SPI_NOR_ENV LC_ETHEREALOS_ENV LC_BOOTMENU_ITEMS_ENV

#ifdef CONFIG_CMD_BMP
#define CONFIG_SPLASH_SOURCE
#define CONFIG_SPLASHIMAGE_GUARD
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (512*512*4)
#define CONFIG_VIDEO_BMP_GZIP
#define CONFIG_VIDEO_LOGO
#define SPLASH_ENV \
	"splashimage=0x01080000\0" \
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
	LC_ENV \
	SPLASH_ENV \
	BOOTENV

#include <configs/meson64.h>

#endif /* __CONFIG_H */

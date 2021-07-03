/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for Libre Computer boards
 * (C) Copyright 2021 Da Xue <da@libre.computer>
 */

#ifndef __LIBRETECH_CONFIG_H
#define __LIBRETECH_CONFIG_H

#define BOOTENV_EFI_SET_FDTFILE_FALLBACK \
	"for prefix in ${efi_dtb_prefixes}; do " \
		"if test -e ${devtype} " \
				"${devnum}:${distro_bootpart} " \
				"${prefix}${vendor}/${board}/platform.dtb; then " \
			"setenv efi_fdtfile ${vendor}/${board}/platform.dtb; " \
			"echo \"Using override dtb ${prefix}${vendor}/${board}/platform.dtb\"; " \
		"fi;" \
	"done;" \

#ifdef CONFIG_DM_VIDEO
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (512*512*4)
#define CONFIG_VIDEO_LOGO
#define CONFIG_VIDEO_BMP_LOGO
#endif

#ifdef CONFIG_SPLASH_SCREEN
#define SPLASH_ENV \
	"splashimage=0x01080000\0" \
	"splashpos=m,m\0" \
	"splashfile=boot.bmp\0" \
	"splashsource=mmc_fs\0"
#else
#define SPLASH_ENV
#endif

#ifdef LC_SPI_NOR
#define LC_SPI_NOR_READ_TEST \
	"self_test_spi_nor_read=" \
		"sf probe; " \
		"setexpr self_test_spi_nor_read_i 0; " \
		"time sf read $kernel_addr_r 0 $spi_nor_size; " \
		"crc32 $kernel_addr_r $spi_nor_size $ramdisk_addr_r; " \
		"while test $? -eq 0; do " \
			"setexpr self_test_spi_nor_read_i $self_test_spi_nor_read_i + 1; " \
			"time sf read $kernel_addr_r 0 $spi_nor_size; " \
			"crc32 $kernel_addr_r $spi_nor_size $pxefile_addr_r; " \
			"cmp.w $ramdisk_addr_r $pxefile_addr_r 1; " \
		"done; " \
		"echo \"SELF TEST SPI NOR READ FAILED AT ITERATION $self_test_spi_nor_read_i\"\0"
#define LC_SPI_NOR_ENV \
	LC_SPI_NOR_READ_TEST \
	"spi_nor_size=" __stringify(LC_SPI_NOR_SIZE) "\0"
#else
#define LC_SPI_NOR_ENV
#endif

#ifdef LC_ETHEREALOS
#define LC_ETHEREALOS_TEST_LOAD_SPI \
	"etherealos_test_load_spi=" \
		"if test $boot_source = \"spi\"; then " \
			"sf probe && time sf read $pxefile_addr_r $etherealos_offset $etherealos_size;" \
		"fi\0"
#define LC_ETHEREALOS_BOOTCMD \
	"bootcmd_etherealos=" \
		"if test $etherealos_size -gt 0; then " \
			"run etherealos_test_load_spi; " \
			"if test $? -eq 0; then " \
				"bootm $pxefile_addr_r; " \
			"fi; " \
		"else " \
			"echo EtherealOS not available.; " \
		"fi\0"
#define LC_ETHEREALOS_ENV \
	"etherealos_offset=" __stringify(LC_ETHEREALOS_OFFSET) "\0" \
	"etherealos_size=" __stringify(LC_ETHEREALOS_SIZE) "\0" \
	LC_ETHEREALOS_TEST_LOAD_SPI \
	LC_ETHEREALOS_BOOTCMD \
	LC_ETHEREALOS_BOOTMENU_ITEMS
#else
#define LC_ETHEREALOS_ENV
#endif

#define LC_ENV \
	LC_SPI_NOR_ENV \
	LC_ETHEREALOS_ENV \
	LC_BOOTMENU_ITEMS_ENV

#endif /* __LIBRETECH_CONFIG_H */

/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration settings for the Allwinner A64 (sun50i) CPU
 */

#ifndef __CONFIG_H
#define __CONFIG_H

/*
 * A64 specific configuration
 */

#ifndef CONFIG_MACH_SUN50I_H6
#define GICD_BASE		0x1c81000
#define GICC_BASE		0x1c82000
#else
#define GICD_BASE		0x3021000
#define GICC_BASE		0x3022000
#endif

#ifdef CONFIG_DM_VIDEO
#define CONFIG_VIDEO_BMP_RLE8
#define CONFIG_BMP_16BPP
#define CONFIG_BMP_24BPP
#define CONFIG_BMP_32BPP
#define CONFIG_SPLASH_SCREEN
#define CONFIG_SPLASH_SCREEN_ALIGN
#define CONFIG_SPLASH_SOURCE
#define CONFIG_SPLASHIMAGE_GUARD
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (512*512*4)
#define CONFIG_VIDEO_BMP_GZIP
#define CONFIG_VIDEO_BMP_LOGO
#define CONFIG_VIDEO_LOGO
#endif

#define CONFIG_SYS_MEMTEST_START 0x08000000
#define CONFIG_SYS_MEMTEST_END 0x17ffffff

#define BOOTMENU_ITEMS \
	"bootmenu_0=Boot=boot; echo \"Boot failed.\"; sleep 30; $menucmd\0" \
	"bootmenu_1=Boot USB=run bootcmd_usb0; echo \"USB Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_2=Boot SD Card=run bootcmd_mmc0; echo \"SD Card Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_3=Boot PXE=run bootcmd_pxe; echo \"PXE Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_4=Boot DHCP=run bootcmd_dhcp; echo \"DHCP Boot failed.\"; sleep 5; $menucmd -1\0" \
	"bootmenu_5=SD Card USB Drive Mode=mmc list; if mmc dev 0; then echo \"Press Control+C to end USB Drive Mode.\"; ums 0 mmc 0:0; echo \"USB Drive Mode ended.\"; else echo \"SD Card not detected.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_6=Detect USB Devices=if usb reset; then echo \"USB detection complete.\"; else echo \"USB detection failed.\"; fi; sleep 5; $menucmd -1\0" \
	"bootmenu_7=Reboot=reset\0" \
	"bootmenu_delay=30\0" \
	"menucmd=bootmenu\0" \

#define LC_EXTRA_ENV_SETTINGS \
	"lc_fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
	"splashimage=" KERNEL_ADDR_R "\0" \
	"splashpos=m,m\0" \
	"splashfile=boot.bmp\0" \
	"splashsource=mmc_fs\0"

/*
 * Include common sunxi configuration where most the settings are
 */
#include <configs/sunxi-common.h>

#endif /* __CONFIG_H */

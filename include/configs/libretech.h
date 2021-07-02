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


#endif /* __LIBRETECH_CONFIG_H */

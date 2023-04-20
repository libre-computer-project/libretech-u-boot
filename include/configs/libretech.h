/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for Libre Computer boards
 * (C) Copyright 2021 Da Xue <da@libre.computer>
 */

#ifndef __LIBRETECH_CONFIG_H
#define __LIBRETECH_CONFIG_H

#ifdef CONFIG_SPLASH_SCREEN
#define SPLASH_ENV \
	"splashimage=" __stringify(CONFIG_SYS_LOAD_ADDR) "\0" \
	"splashpos=m,m\0" \
	"splashfile=boot.bmp\0" \
	"splashsource=mmc_fs\0"
#else
#define SPLASH_ENV
#endif

#endif /* __LIBRETECH_CONFIG_H */

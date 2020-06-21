/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2020 Shenzhen Libre Technology Co., Ltd
 */

#ifndef __LIBRETECH_ROC_RK3328_CC_H
#define __LIBRETECH_ROC_RK3328_CC_H

#define CONFIG_SPL_MAX_SIZE             0x1c000
#define CONFIG_SPL_PAD_TO		114688

#include <configs/rk3328_common.h>

#define CONFIG_SYS_MMC_ENV_DEV 1

#define SDRAM_BANK_SIZE			(2UL << 30)

#define CONFIG_CONSOLE_SCROLL_LINES		10

#endif

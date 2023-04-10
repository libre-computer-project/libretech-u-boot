// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2019 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <bootstage.h>
#include <debug_uart.h>
#include <dm.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <ram.h>
#include <spl.h>
#include <version.h>
#include <asm/io.h>
#include <asm/arch-rockchip/bootrom.h>
#include <linux/bitops.h>
#include <linux/kconfig.h>

#if CONFIG_IS_ENABLED(BANNER_PRINT)
#include <timestamp.h>
#endif

#ifdef CONFIG_TPL_BUILD
#define GRF_GPIO2A_IOMUX	0xff100020
#define GRF_GPIO2A_P		0xff100120
#define GRF_GPIO1D_IOMUX	0xff10001c
#define GRF_GPIO1D_P		0xff10011c
#endif

#define TIMER_LOAD_COUNT_L	0x00
#define TIMER_LOAD_COUNT_H	0x04
#define TIMER_CONTROL_REG	0x10
#define TIMER_EN	0x1
#define	TIMER_FMODE	BIT(0)
#define	TIMER_RMODE	BIT(1)

__weak void rockchip_stimer_init(void)
{
#if defined(CONFIG_ROCKCHIP_STIMER_BASE)
	/* If Timer already enabled, don't re-init it */
	u32 reg = readl(CONFIG_ROCKCHIP_STIMER_BASE + TIMER_CONTROL_REG);

	if (reg & TIMER_EN)
		return;

#ifndef CONFIG_ARM64
	asm volatile("mcr p15, 0, %0, c14, c0, 0"
		     : : "r"(CONFIG_COUNTER_FREQUENCY));
#endif

	writel(0, CONFIG_ROCKCHIP_STIMER_BASE + TIMER_CONTROL_REG);
	writel(0xffffffff, CONFIG_ROCKCHIP_STIMER_BASE);
	writel(0xffffffff, CONFIG_ROCKCHIP_STIMER_BASE + 4);
	writel(TIMER_EN | TIMER_FMODE, CONFIG_ROCKCHIP_STIMER_BASE +
	       TIMER_CONTROL_REG);
#endif
}

#ifdef CONFIG_TPL_BUILD

void rockchip_pinctrl1_init(void)
{
/* setting mux of GPIO2-4 to 2, setting pull of GPIO2-4 to 1 */
        writel(0x3000200, GRF_GPIO2A_IOMUX); //setmux
        writel(0x3000000, GRF_GPIO2A_P); //setpull
/* setting mux of GPIO2-5 to 2, setting pull of GPIO2-5 to 1 */
        writel(0xc000800, GRF_GPIO2A_IOMUX); //setmux
        writel(0xc000000, GRF_GPIO2A_P); //setpull

/* setting mux of GPIO1-24 to 0, setting pull of GPIO1-24 to 5 */
        writel(0x30000, GRF_GPIO1D_IOMUX); //setmux
        writel(0x30001, GRF_GPIO1D_P); //setpull
}
#endif

void board_init_f(ulong dummy)
{
	struct udevice *dev;
	int ret;

#if defined(CONFIG_DEBUG_UART) && defined(CONFIG_TPL_SERIAL)
	/*
	 * Debug UART can be used from here if required:
	 *
	 * debug_uart_init();
	 * printch('a');
	 * printhex8(0x1234);
	 * printascii("string");
	 */
	debug_uart_init();
#ifdef CONFIG_TPL_BANNER_PRINT
	printascii("\nU-Boot TPL " PLAIN_VERSION " (" U_BOOT_DATE " - " \
				U_BOOT_TIME ")\n");
#endif
#endif
	/* Init secure timer */
	rockchip_stimer_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
		hang();
	}

	/* Init ARM arch timer */
	if (IS_ENABLED(CONFIG_SYS_ARCH_TIMER))
		timer_init();

#ifdef CONFIG_TPL_BUILD
	/* pinctrl for i2c1 and pmic interrupt */
	rockchip_pinctrl1_init();
#endif

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		printf("DRAM init failed: %d\n", ret);
		return;
	}
}

int board_return_to_bootrom(struct spl_image_info *spl_image,
			    struct spl_boot_device *bootdev)
{
#ifdef CONFIG_BOOTSTAGE_STASH
	int ret;

	bootstage_mark_name(BOOTSTAGE_ID_END_TPL, "end tpl");
	ret = bootstage_stash((void *)CONFIG_BOOTSTAGE_STASH_ADDR,
			      CONFIG_BOOTSTAGE_STASH_SIZE);
	if (ret)
		debug("Failed to stash bootstage: err=%d\n", ret);
#endif
	back_to_bootrom(BROM_BOOT_NEXTSTAGE);

	return 0;
}

u32 spl_boot_device(void)
{
	return BOOT_DEVICE_BOOTROM;
}

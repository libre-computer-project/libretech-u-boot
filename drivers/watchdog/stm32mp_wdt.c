// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2019, STMicroelectronics - All Rights Reserved
 */

#define LOG_CATEGORY UCLASS_WDT

#include <clk.h>
#include <dm.h>
#include <log.h>
#include <syscon.h>
#include <wdt.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <linux/printk.h>

/* IWDG registers */
#define IWDG_KR		0x00	/* Key register */
#define IWDG_PR		0x04	/* Prescaler Register */
#define IWDG_RLR	0x08	/* ReLoad Register */
#define IWDG_SR		0x0C	/* Status Register */
#define IWDG_VERR	0x3F4	/* Version Register */

/* IWDG_KR register bit mask */
#define KR_KEY_RELOAD	0xAAAA	/* Reload counter enable */
#define KR_KEY_ENABLE	0xCCCC	/* Peripheral enable */
#define KR_KEY_EWA	0x5555	/* Write access enable */
#define KR_KEY_DWA	0x0000	/* Write access disable*/

/* IWDG_PR register bit values */
#define PR_256		0x06	/* Prescaler set to 256 */

/* IWDG_RLR register values */
#define RLR_MAX		0xFFF	/* Max value supported by reload register */

/* IWDG_SR register bit values */
#define SR_PVU		BIT(0)	/* Watchdog prescaler value update */
#define SR_RVU		BIT(1)	/* Watchdog counter reload value update */
#define SR_ONF		BIT(8)	/* Watchdog enable status bit */

/* IWDG Compatibility */
#define ONF_MIN_VER	0x31

#define TIMEOUT_US	10000

struct stm32mp_wdt_priv {
	fdt_addr_t base;		/* registers addr in physical memory */
	unsigned long wdt_clk_rate;	/* Watchdog dedicated clock rate */
	unsigned int hw_version;	/* Peripheral version */
};

static int stm32mp_wdt_reset(struct udevice *dev)
{
	struct stm32mp_wdt_priv *priv = dev_get_priv(dev);

	writel(KR_KEY_RELOAD, priv->base + IWDG_KR);

	return 0;
}

static int stm32mp_wdt_start(struct udevice *dev, u64 timeout_ms, ulong flags)
{
	struct stm32mp_wdt_priv *priv = dev_get_priv(dev);
	int reload;
	u32 val;
	int ret;

	/* Prescaler fixed to 256 */
	reload = timeout_ms * priv->wdt_clk_rate / 256;
	if (reload > RLR_MAX + 1)
		/* Force to max watchdog counter reload value */
		reload = RLR_MAX + 1;
	else if (!reload)
		/* Force to min watchdog counter reload value */
		reload = priv->wdt_clk_rate / 256;

	/* Set prescaler & reload registers */
	writel(KR_KEY_EWA, priv->base + IWDG_KR);
	writel(PR_256, priv->base + IWDG_PR);
	writel(reload - 1, priv->base + IWDG_RLR);

	/* Enable watchdog */
	writel(KR_KEY_ENABLE, priv->base + IWDG_KR);

	/* Wait for the registers to be updated */
	ret = readl_poll_timeout(priv->base + IWDG_SR, val,
				 val & (SR_PVU | SR_RVU), CONFIG_SYS_HZ);

	if (ret < 0) {
		dev_err(dev, "Updating IWDG registers timeout");
		return -ETIMEDOUT;
	}

	return 0;
}

static int stm32mp_wdt_probe(struct udevice *dev)
{
	struct stm32mp_wdt_priv *priv = dev_get_priv(dev);
	u32 rlr, sr;
	struct clk clk;
	int ret;

	dev_dbg(dev, "IWDG init\n");

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	/* Enable clock */
	ret = clk_get_by_name(dev, "pclk", &clk);
	if (ret)
		return ret;

	ret = clk_enable(&clk);
	if (ret)
		return ret;

	/* Get LSI clock */
	ret = clk_get_by_name(dev, "lsi", &clk);
	if (ret)
		return ret;

	priv->wdt_clk_rate = clk_get_rate(&clk);

	priv->hw_version = readl(priv->base + IWDG_VERR);

	if (priv->hw_version >= ONF_MIN_VER) {
		if (readl(priv->base + IWDG_SR) & SR_ONF)
			wdt_set_force_autostart(dev);
	} else {
		/*
		 * Workaround for old versions without IWDG_SR_ONF bit:
		 * - write in IWDG_RLR_OFFSET
		 * - wait for sync
		 * - if sync succeeds, then iwdg is running
		 */
		writel(KR_KEY_EWA, priv->base + IWDG_KR);
		rlr = readl(priv->base + IWDG_RLR);
		writel(rlr, priv->base + IWDG_RLR);
		ret = readl_poll_timeout(priv->base + IWDG_SR, sr, sr & SR_RVU,
					 TIMEOUT_US);
		if (!ret)
			wdt_set_force_autostart(dev);

		writel(KR_KEY_DWA, priv->base + IWDG_KR);
	}

	dev_dbg(dev, "IWDG init done\n");

	return 0;
}

static const struct wdt_ops stm32mp_wdt_ops = {
	.start = stm32mp_wdt_start,
	.reset = stm32mp_wdt_reset,
};

static const struct udevice_id stm32mp_wdt_match[] = {
	{ .compatible = "st,stm32mp1-iwdg" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(stm32mp_wdt) = {
	.name = "stm32mp-wdt",
	.id = UCLASS_WDT,
	.of_match = stm32mp_wdt_match,
	.priv_auto	= sizeof(struct stm32mp_wdt_priv),
	.probe = stm32mp_wdt_probe,
	.ops = &stm32mp_wdt_ops,
};

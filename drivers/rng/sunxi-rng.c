// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Da Xue <da@libre.computer>
 *
 * Allwinner Crypto Engine PRNG driver for H3/H5 (sun8i-ce)
 *
 * The H3/H5 crypto engine uses DMA task descriptors for all operations
 * including PRNG. We build a task descriptor in memory, point the CE
 * at it, trigger it, and poll for completion.
 *
 * Based on Linux sun8i-ce-prng.c and sun8i-ce-core.c.
 */

#include <clk.h>
#include <dm.h>
#include <reset.h>
#include <rng.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <cpu_func.h>
#include <memalign.h>

/* CE registers */
#define CE_TDQ		0x00	/* Task Descriptor Queue address */
#define CE_ICR		0x08	/* Interrupt Control Register */
#define CE_ISR		0x0C	/* Interrupt Status Register */
#define CE_TLR		0x10	/* Task Load Register */
#define CE_TSR		0x14	/* Task Status Register */
#define CE_ESR		0x18	/* Error Status Register */

/* CE task descriptor */
struct ce_task {
	u32 t_id;		/* task ID / flow */
	u32 t_common_ctl;	/* algorithm, interrupt enable */
	u32 t_sym_ctl;		/* symmetric control (PRNG_LD) */
	u32 t_asym_ctl;		/* asymmetric control */
	u32 t_key;		/* key/seed physical address */
	u32 t_iv;		/* IV physical address (= seed for PRNG) */
	u32 t_ctr;		/* counter address */
	u32 t_dlen;		/* data length in words (H3/H5) */
	u32 t_src[8];		/* source scatter-gather */
	u32 t_dst[8];		/* destination scatter-gather */
	u32 t_next;		/* next task descriptor (0 = last) */
	u32 t_reserved[3];
} __aligned(8);

/* Algorithm IDs */
#define CE_ALG_PRNG	49

/* Common control bits */
#define CE_COMM_INT	BIT(31)

/* Symmetric control bits */
#define PRNG_LD		BIT(17)

/* PRNG sizes */
#define PRNG_SEED_SIZE	24	/* 192 bits = 6 words */
#define PRNG_DATA_SIZE	20	/* 160 bits = 5 words */

#define CE_FLOW_PRNG	0	/* use flow 0 for PRNG */

struct sunxi_rng_plat {
	void __iomem *base;
	struct clk clk_bus;
	struct clk clk_mod;
	struct reset_ctl reset;
	u8 seed[PRNG_SEED_SIZE];
	bool seeded;
};

static int sunxi_rng_generate(struct sunxi_rng_plat *pdata, void *dst, size_t len)
{
	ALLOC_CACHE_ALIGN_BUFFER(struct ce_task, task, 1);
	ALLOC_CACHE_ALIGN_BUFFER(u8, seed_buf, PRNG_SEED_SIZE);
	ALLOC_CACHE_ALIGN_BUFFER(u8, out_buf, PRNG_DATA_SIZE);
	u8 *output = dst;
	int timeout;

	while (len > 0) {
		size_t chunk = min(len, (size_t)PRNG_DATA_SIZE);

		memset(task, 0, sizeof(*task));
		memcpy(seed_buf, pdata->seed, PRNG_SEED_SIZE);

		/* Build task descriptor */
		task->t_id = CE_FLOW_PRNG;
		task->t_common_ctl = CE_ALG_PRNG | CE_COMM_INT;
		task->t_sym_ctl = PRNG_LD;
		task->t_dlen = PRNG_DATA_SIZE / 4;	/* length in words for H3/H5 */
		task->t_key = virt_to_phys(seed_buf);
		task->t_iv = virt_to_phys(seed_buf);
		task->t_dst[0] = virt_to_phys(out_buf);
		task->t_dst[1] = PRNG_DATA_SIZE / 4;	/* length in words */
		task->t_next = 0;

		/* Flush caches */
		flush_dcache_range((ulong)task,
				   (ulong)task + ARCH_DMA_MINALIGN);
		flush_dcache_range((ulong)seed_buf,
				   (ulong)seed_buf + ARCH_DMA_MINALIGN);
		flush_dcache_range((ulong)out_buf,
				   (ulong)out_buf + ARCH_DMA_MINALIGN);

		/* Clear interrupt status */
		writel(0xf, pdata->base + CE_ISR);

		/* Enable interrupt for flow 0 */
		writel(BIT(CE_FLOW_PRNG), pdata->base + CE_ICR);

		/* Write task descriptor address */
		writel(virt_to_phys(task), pdata->base + CE_TDQ);

		/* Trigger task */
		writel(1 | (CE_ALG_PRNG << 8), pdata->base + CE_TLR);

		/* Poll for completion */
		timeout = 10000;
		while (!(readl(pdata->base + CE_ISR) & BIT(CE_FLOW_PRNG))) {
			if (--timeout <= 0) {
				pr_err("sunxi-rng: PRNG timeout\n");
				return -ETIMEDOUT;
			}
			udelay(1);
		}

		/* Clear interrupt */
		writel(BIT(CE_FLOW_PRNG), pdata->base + CE_ISR);

		/* Check for errors */
		if (readl(pdata->base + CE_ESR)) {
			pr_err("sunxi-rng: PRNG error 0x%x\n",
			       readl(pdata->base + CE_ESR));
			return -EIO;
		}

		/* Invalidate output cache */
		invalidate_dcache_range((ulong)out_buf,
					(ulong)out_buf + ARCH_DMA_MINALIGN);

		/* Copy output */
		memcpy(output, out_buf, chunk);
		output += chunk;
		len -= chunk;

		/* Update seed from output for next iteration */
		memcpy(pdata->seed, out_buf, min((size_t)PRNG_SEED_SIZE,
						 (size_t)PRNG_DATA_SIZE));
		pdata->seeded = true;
	}

	return 0;
}

static int sunxi_rng_read(struct udevice *dev, void *data, size_t len)
{
	struct sunxi_rng_plat *pdata = dev_get_plat(dev);

	if (!pdata->seeded) {
		/* Initial seed from timer + SID */
		u32 *seed = (u32 *)pdata->seed;
		int i;

		for (i = 0; i < PRNG_SEED_SIZE / 4; i++)
			seed[i] = get_timer(0) ^ (i * 0x5DEECE66D + 0xB);
		pdata->seeded = true;
	}

	return sunxi_rng_generate(pdata, data, len);
}

static int sunxi_rng_probe(struct udevice *dev)
{
	struct sunxi_rng_plat *pdata = dev_get_plat(dev);
	int ret;

	ret = clk_enable(&pdata->clk_bus);
	if (ret)
		return ret;

	ret = clk_enable(&pdata->clk_mod);
	if (ret)
		return ret;

	ret = reset_deassert(&pdata->reset);
	if (ret)
		return ret;

	return 0;
}

static int sunxi_rng_remove(struct udevice *dev)
{
	struct sunxi_rng_plat *pdata = dev_get_plat(dev);

	reset_assert(&pdata->reset);
	clk_disable(&pdata->clk_mod);
	clk_disable(&pdata->clk_bus);

	return 0;
}

static int sunxi_rng_of_to_plat(struct udevice *dev)
{
	struct sunxi_rng_plat *pdata = dev_get_plat(dev);
	int ret;

	pdata->base = dev_read_addr_ptr(dev);
	if (!pdata->base)
		return -ENODEV;

	ret = clk_get_by_name(dev, "bus", &pdata->clk_bus);
	if (ret)
		return ret;

	ret = clk_get_by_name(dev, "mod", &pdata->clk_mod);
	if (ret)
		return ret;

	ret = reset_get_by_index(dev, 0, &pdata->reset);
	if (ret)
		return ret;

	return 0;
}

static const struct dm_rng_ops sunxi_rng_ops = {
	.read = sunxi_rng_read,
};

static const struct udevice_id sunxi_rng_match[] = {
	{ .compatible = "allwinner,sun8i-h3-crypto" },
	{ .compatible = "allwinner,sun50i-h5-crypto" },
	{ },
};

U_BOOT_DRIVER(sunxi_rng) = {
	.name = "sunxi-rng",
	.id = UCLASS_RNG,
	.of_match = sunxi_rng_match,
	.ops = &sunxi_rng_ops,
	.probe = sunxi_rng_probe,
	.remove = sunxi_rng_remove,
	.plat_auto = sizeof(struct sunxi_rng_plat),
	.of_to_plat = sunxi_rng_of_to_plat,
};

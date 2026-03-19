// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson SPICC controller driver
 *
 * Copyright (C) 2024 Da Xue <da@libre.computer>
 *
 * Ported from Linux driver by Neil Armstrong <narmstrong@baylibre.com>
 * and Da Xue.
 *
 * Transfer modes following the Linux driver:
 * - PIO: For small transfers (< fifo_size * 2 words). Uses XCH bit to
 *   trigger bursts of up to fifo_size words. Polls TC for completion.
 * - DMA: For large transfers. The SPICC DMA engine reads/writes 64-bit
 *   words from memory. Data is padded to 64-bit alignment. Uses SMC bit,
 *   LD_CNTL counters for burst management. Remainder handled via PIO.
 */

#include <clk.h>
#include <cpu_func.h>
#include <dm.h>
#include <malloc.h>
#include <memalign.h>
#include <spi.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <dm/device_compat.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/log2.h>

/*
 * HHI_SPICC_CLK_CNTL register for direct clock setup when the U-Boot
 * clock driver doesn't support SPICC_SCLK clocks (IDs >= 256).
 *
 * SPICC1 bit fields (bits 16-31):
 *   [21:16] - divider (div = N+1)
 *   [22]    - gate enable
 *   [25:23] - mux select: 0=xtal, 1=clk81, 2=fclk_div4, 3=fclk_div3,
 *                          4=fclk_div2, 5=fclk_div5, 6=fclk_div7
 */
#define G12A_HHI_SPICC_CLK_CNTL	0xff63c3dcUL
#define SPICC1_SCLK_MUX_SHIFT		23
#define SPICC1_SCLK_MUX_MASK		(0x7 << 23)
#define SPICC1_SCLK_DIV_SHIFT		16
#define SPICC1_SCLK_DIV_MASK		(0x3f << 16)
#define SPICC1_SCLK_GATE		BIT(22)
#define SPICC1_SCLK_MUX_CLK81		1
#define SPICC1_SCLK_MUX_FCLK_DIV4	2
#define SPICC1_SCLK_FCLK_DIV4_HZ	500000000UL

/* Register offsets */
#define SPICC_RXDATA		0x00
#define SPICC_TXDATA		0x04
#define SPICC_CONREG		0x08
#define SPICC_INTREG		0x0c
#define SPICC_DMAREG		0x10
#define SPICC_STATREG		0x14
#define SPICC_PERIODREG		0x18
#define SPICC_TESTREG		0x1c
#define SPICC_DRADDR		0x20
#define SPICC_DWADDR		0x24
#define SPICC_LD_CNTL0		0x28
#define SPICC_LD_CNTL1		0x2c
#define SPICC_ENH_CTL0		0x38
#define SPICC_ENH_CTL1		0x3c
#define SPICC_ENH_CTL2		0x40

/* SPICC_CONREG bits */
#define SPICC_ENABLE		BIT(0)
#define SPICC_MODE_MASTER	BIT(1)
#define SPICC_XCH		BIT(2)
#define SPICC_SMC		BIT(3)
#define SPICC_POL		BIT(4)
#define SPICC_PHA		BIT(5)
#define SPICC_SSCTL		BIT(6)
#define SPICC_SSPOL		BIT(7)
#define SPICC_DRCTL_MASK	GENMASK(9, 8)
#define SPICC_CS_MASK		GENMASK(13, 12)
#define SPICC_DATARATE_MASK	GENMASK(18, 16)
#define SPICC_BITLENGTH_MASK	GENMASK(24, 19)
#define SPICC_BURSTLENGTH_MASK	GENMASK(31, 25)

/* SPICC_DMAREG bits */
#define SPICC_DMA_ENABLE		BIT(0)
#define SPICC_TXFIFO_THRESHOLD_MASK	GENMASK(5, 1)
#define SPICC_RXFIFO_THRESHOLD_MASK	GENMASK(10, 6)
#define SPICC_READ_BURST_MASK		GENMASK(14, 11)
#define SPICC_WRITE_BURST_MASK		GENMASK(18, 15)
#define SPICC_DMA_URGENT		BIT(19)

/* SPICC_STATREG bits */
#define SPICC_TE		BIT(0)	/* TX FIFO Empty */
#define SPICC_TH		BIT(1)	/* TX FIFO Half-Full */
#define SPICC_TF		BIT(2)	/* TX FIFO Full */
#define SPICC_RR		BIT(3)	/* RX FIFO Ready */
#define SPICC_RH		BIT(4)	/* RX FIFO Half-Full */
#define SPICC_RF		BIT(5)	/* RX FIFO Full */
#define SPICC_RO		BIT(6)	/* RX FIFO Overflow */
#define SPICC_TC		BIT(7)	/* Transfer Complete */

/*
 * SPICC_TESTREG layout (characterized on G12A/SM1):
 *
 * Bits 0-4:   TXCNT (read-only) — TX FIFO word count
 * Bits 5-9:   RXCNT (read-only) — RX FIFO word count (reads N-1 after N-word xfer)
 * Bits 10-13: read-only / reserved
 * Bit 14:     LBC — loopback control (regular R/W, NOT W1 toggle)
 * Bit 15:     writable, purpose unknown (does not affect loopback)
 * Bits 16-17: MO_DELAY — master output delay
 * Bits 18-19: MI_DELAY — master input delay
 * Bits 20-21: MI_CAP_DELAY — master input capture delay
 * Bits 22-23: FIFORST — FIFO soft-reset (write-1-to-trigger, auto-clears)
 *
 * FIFO geometry: 15 entries, each 64 bits wide. For bpw <= 32, one
 * TXDATA/RXDATA access per entry. For bpw > 32, two accesses (MSB first,
 * then LSB) are paired into a single 64-bit entry. TXCNT/RXCNT always
 * count logical words (entries), not individual 32-bit accesses.
 * BURSTLENGTH likewise counts logical words.
 *
 * The LBC bit position may vary across SoC variants and is detected at
 * probe time. Use RMW via spicc_setbits for all TESTREG writes to
 * preserve the LBC state.
 */
#define SPICC_TXCNT_MASK	GENMASK(4, 0)
#define SPICC_RXCNT_MASK	GENMASK(9, 5)
#define SPICC_LBC		BIT(14)	/* Loopback control (default position) */
#define SPICC_MO_DELAY_MASK	GENMASK(17, 16)
#define SPICC_MI_DELAY_MASK	GENMASK(19, 18)
#define SPICC_MI_CAP_DELAY_MASK	GENMASK(21, 20)
#define SPICC_FIFORST_W1_MASK	GENMASK(23, 22)

/* MI_DELAY values */
#define SPICC_MI_NO_DELAY	0
#define SPICC_MI_DELAY_1_CYCLE	1
#define SPICC_MI_DELAY_2_CYCLE	2
#define SPICC_MI_DELAY_3_CYCLE	3

/* MI_CAP_DELAY values */
#define SPICC_CAP_AHEAD_2_CYCLE	0
#define SPICC_CAP_AHEAD_1_CYCLE	1
#define SPICC_CAP_NO_DELAY	2
#define SPICC_CAP_DELAY_1_CYCLE	3

/* SPICC_LD_CNTL0 bits */
#define DMA_READ_COUNTER_EN	BIT(4)
#define DMA_WRITE_COUNTER_EN	BIT(5)

/* SPICC_LD_CNTL1 bits */
#define DMA_READ_COUNTER_MASK	GENMASK(15, 0)
#define DMA_WRITE_COUNTER_MASK	GENMASK(31, 16)

/* SPICC_ENH_CTL0 bits */
#define SPICC_ENH_DATARATE_MASK		GENMASK(23, 16)
#define SPICC_ENH_DATARATE_EN		BIT(24)
#define SPICC_ENH_MOSI_OEN		BIT(25)
#define SPICC_ENH_CLK_OEN		BIT(26)
#define SPICC_ENH_CS_OEN		BIT(27)
#define SPICC_ENH_MAIN_CLK_AO		BIT(29)

/*
 * DMA constants following the Linux driver.
 * The SPICC DMA engine always transfers 64-bit words from memory.
 * For 8-bit bpw, each byte is padded to an 8-byte DMA word.
 */
#define SPICC_DMA_WIDTH_BYTES	8
#define SPICC_DMA_MIN_FIFOS	2
#define DMA_BURST_WORDS_DEFAULT	8

#define SPICC_TIMEOUT_MS	100

/*
 * Maximum actual data bytes per DMA setup.
 * Padded DMA buffer is 8x this size.
 * 320KB covers a 320x480x2 framebuffer (307,200 bytes).
 */
#define SPICC_DMA_MAX_BYTES	(320 * 1024)

/* Default frequency-switch settling delay in clock cycles */
#define FREQ_SWITCH_DELAY_CYCLES	2

struct meson_spicc_data {
	unsigned int max_speed_hz;
	unsigned int min_speed_hz;
	unsigned int fifo_size;
	unsigned int dma_bursts_max;
	bool has_oen;
	bool has_enhance_clk_div;
	bool has_pclk;
};

struct meson_spicc_priv {
	void __iomem *base;
	struct clk core_clk;
	struct clk pclk;
	bool pclk_enabled;
	unsigned long pclk_rate;
	const struct meson_spicc_data *data;
	unsigned int speed_hz;
	unsigned int mode;
	unsigned int bits_per_word;
	unsigned int xfer_bpw;		/* current BITLENGTH in CONREG */
	unsigned int clk_ns;		/* nanoseconds per SPI clock cycle */
	unsigned long last_speed_hz;	/* for freq-switch delay */
	u32 lbc_bit;			/* detected LBC bit position */
	bool bus_claimed;		/* bus in use, reject re-entrant claims */
	struct gpio_desc cs_gpio;
	/* DMA padded buffers (cache-line aligned, 8x actual data) */
	void *dma_tx_buf;
	void *dma_rx_buf;
	size_t dma_buf_size;
};

/* --- Low-level register helpers --- */

static inline u32 spicc_read(struct meson_spicc_priv *priv, u32 reg)
{
	return readl(priv->base + reg);
}

static inline void spicc_write(struct meson_spicc_priv *priv, u32 reg,
			       u32 val)
{
	writel(val, priv->base + reg);
}

/* Read-modify-write helper — safe for CONREG, ENH_CTL0, etc. */
static inline void spicc_setbits(struct meson_spicc_priv *priv, u32 reg,
				 u32 mask, u32 val)
{
	u32 tmp = spicc_read(priv, reg);

	tmp = (tmp & ~mask) | (val & mask);
	spicc_write(priv, reg, tmp);
}

/* --- FIFO reset --- */

/*
 * Reset TX and RX FIFOs. Uses direct writes to TESTREG to avoid
 * read-modify-write hazards with W1 bits at shifted positions.
 */
static void spicc_reset_fifo(struct meson_spicc_priv *priv)
{
	if (priv->data->has_oen)
		spicc_setbits(priv, SPICC_ENH_CTL0,
			      SPICC_ENH_MAIN_CLK_AO, SPICC_ENH_MAIN_CLK_AO);

	/* RMW to set FIFORST bits while preserving LBC and delay settings */
	spicc_setbits(priv, SPICC_TESTREG,
		      SPICC_FIFORST_W1_MASK, SPICC_FIFORST_W1_MASK);

	while (spicc_read(priv, SPICC_STATREG) & SPICC_RR)
		spicc_read(priv, SPICC_RXDATA);

	if (priv->data->has_oen)
		spicc_setbits(priv, SPICC_ENH_CTL0,
			      SPICC_ENH_MAIN_CLK_AO, 0);
}

/*
 * Detect which TESTREG bit controls loopback on this SoC.
 *
 * The LBC bit is a regular R/W bit (not W1 toggle). We detect its position
 * by writing candidate bits and checking which one reads back as set.
 * Tested candidates: BIT(14) (G12A/SM1), BIT(13) (possible on other SoCs).
 */
static void spicc_detect_lbc(struct udevice *bus)
{
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	static const u32 candidates[] = { BIT(14), BIT(13), BIT(15) };
	u32 val;
	int i;

	/* Enable controller temporarily for TESTREG access */
	spicc_write(priv, SPICC_CONREG, SPICC_ENABLE | SPICC_MODE_MASTER);
	spicc_write(priv, SPICC_TESTREG, 0);

	for (i = 0; i < ARRAY_SIZE(candidates); i++) {
		/* Write the candidate bit */
		spicc_write(priv, SPICC_TESTREG, candidates[i]);
		val = spicc_read(priv, SPICC_TESTREG);

		if (val & candidates[i]) {
			priv->lbc_bit = candidates[i];

			/* Clear it back */
			spicc_write(priv, SPICC_TESTREG, 0);

			printf("spicc: LBC detected at bit %d\n",
			       ffs(priv->lbc_bit) - 1);
			goto done;
		}

		/* Clear before trying next candidate */
		spicc_write(priv, SPICC_TESTREG, 0);
	}

	/* Fallback */
	priv->lbc_bit = SPICC_LBC;
	printf("spicc: LBC detection failed, using default bit 14\n");

done:
	spicc_write(priv, SPICC_CONREG, 0);
}

static void spicc_oen_enable(struct meson_spicc_priv *priv)
{
	u32 conf;

	if (!priv->data->has_oen)
		return;

	conf = spicc_read(priv, SPICC_ENH_CTL0);
	conf |= SPICC_ENH_MOSI_OEN | SPICC_ENH_CLK_OEN;
	if (!dm_gpio_is_valid(&priv->cs_gpio))
		conf |= SPICC_ENH_CS_OEN;
	else
		conf &= ~SPICC_ENH_CS_OEN;
	spicc_write(priv, SPICC_ENH_CTL0, conf);
}

/* --- Auto IO delay — following Linux meson_spicc_auto_io_delay --- */

static void spicc_auto_io_delay(struct meson_spicc_priv *priv)
{
	unsigned int speed_hz = priv->speed_hz;
	u32 mi_delay, cap_delay;
	u32 div, conf;

	if (priv->data->has_enhance_clk_div) {
		div = FIELD_GET(SPICC_ENH_DATARATE_MASK,
				spicc_read(priv, SPICC_ENH_CTL0));
		div++;
		div <<= 1;
	} else {
		div = FIELD_GET(SPICC_DATARATE_MASK,
				spicc_read(priv, SPICC_CONREG));
		div += 2;
		div = 1 << div;
	}

	mi_delay = SPICC_MI_NO_DELAY;
	cap_delay = SPICC_CAP_AHEAD_2_CYCLE;

	/*
	 * Delay table from Linux driver (LBC-validated values).
	 * These empirical settings ensure correct MISO capture timing.
	 */
	if (speed_hz >= 166666664)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (speed_hz >= 124999998)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (speed_hz >= 99999999)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (speed_hz >= 83333332)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (speed_hz >= 33333333)
		cap_delay = SPICC_CAP_AHEAD_1_CYCLE;
	else if (speed_hz <= 200000)
		mi_delay = SPICC_MI_NO_DELAY;
	else if (speed_hz <= 8000000 && div == 2)
		mi_delay = SPICC_MI_DELAY_3_CYCLE;
	else if (div >= 16)
		mi_delay = SPICC_MI_DELAY_3_CYCLE;
	else if (div >= 8)
		mi_delay = SPICC_MI_DELAY_2_CYCLE;
	else if (div >= 6)
		mi_delay = SPICC_MI_DELAY_1_CYCLE;

apply:
	/* RMW to update delay fields while preserving LBC state */
	conf = FIELD_PREP(SPICC_MI_DELAY_MASK, mi_delay) |
	       FIELD_PREP(SPICC_MI_CAP_DELAY_MASK, cap_delay);
	spicc_setbits(priv, SPICC_TESTREG,
		      SPICC_MI_DELAY_MASK | SPICC_MI_CAP_DELAY_MASK, conf);
}

/* --- DMA helpers --- */

static void spicc_pad_data(const u8 *src, u8 *dst, unsigned int words)
{
	unsigned int i;

	memset(dst, 0, (size_t)words * SPICC_DMA_WIDTH_BYTES);
	for (i = 0; i < words; i++)
		dst[i * SPICC_DMA_WIDTH_BYTES] = src[i];
}

static void spicc_unpad_data(const u8 *src, u8 *dst, unsigned int words)
{
	unsigned int i;

	for (i = 0; i < words; i++)
		dst[i] = src[i * SPICC_DMA_WIDTH_BYTES];
}

/* --- Clock configuration --- */

static int meson_spicc_set_speed(struct udevice *bus, uint speed)
{
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	unsigned long parent_rate;
	u32 div;

	if (speed > priv->data->max_speed_hz)
		speed = priv->data->max_speed_hz;
	if (speed < priv->data->min_speed_hz)
		speed = priv->data->min_speed_hz;

	priv->speed_hz = speed;

	parent_rate = priv->pclk_rate ? priv->pclk_rate :
		      clk_get_rate(&priv->core_clk);

	if (priv->data->has_enhance_clk_div) {
		div = DIV_ROUND_UP(parent_rate, speed * 2) - 1;
		if (div > 255)
			div = 255;

		spicc_setbits(priv, SPICC_ENH_CTL0,
			      SPICC_ENH_DATARATE_MASK | SPICC_ENH_DATARATE_EN,
			      FIELD_PREP(SPICC_ENH_DATARATE_MASK, div) |
			      SPICC_ENH_DATARATE_EN);

		debug("spicc: parent %lu, speed %u, enh div %u, actual %lu\n",
		      parent_rate, speed, div, parent_rate / 2 / (div + 1));
	} else {
		div = DIV_ROUND_UP(parent_rate, speed * 4);
		div = div ? ilog2(div) : 0;
		if (div > 7)
			div = 7;

		spicc_setbits(priv, SPICC_CONREG, SPICC_DATARATE_MASK,
			      FIELD_PREP(SPICC_DATARATE_MASK, div));

		debug("spicc: parent %lu, speed %u, div %u, actual %lu\n",
		      parent_rate, speed, div,
		      parent_rate / 4 / (1 << div));
	}

	/*
	 * Frequency-switch settling delay following Linux driver.
	 * The IP needs old-frequency clock cycles to propagate the change.
	 */
	if (priv->last_speed_hz && speed != priv->last_speed_hz) {
		unsigned long min_hz = min((unsigned long)speed,
					   priv->last_speed_hz);
		unsigned int delay_ns = DIV_ROUND_UP(
			(u64)FREQ_SWITCH_DELAY_CYCLES * 1000000000ULL, min_hz);
		ndelay(delay_ns);
	}
	priv->last_speed_hz = speed;

	/* Compute nanoseconds per clock cycle for PIO delays */
	priv->clk_ns = speed ? DIV_ROUND_UP(1000000000U, speed) : 1000;

	return 0;
}

/*
 * Set SPI mode (CPOL, CPHA, CS_HIGH, LOOP).
 *
 * LBC (loopback) is a regular R/W bit in TESTREG. Position detected at probe.
 */
static int meson_spicc_set_mode(struct udevice *bus, uint mode)
{
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	u32 conf = 0;

	priv->mode = mode;

	if (mode & SPI_CPOL)
		conf |= SPICC_POL;
	if (mode & SPI_CPHA)
		conf |= SPICC_PHA;
	if (mode & SPI_CS_HIGH)
		conf |= SPICC_SSPOL;

	spicc_setbits(priv, SPICC_CONREG,
		      SPICC_POL | SPICC_PHA | SPICC_SSPOL, conf);

	/* Set or clear LBC bit via RMW */
	spicc_setbits(priv, SPICC_TESTREG, priv->lbc_bit,
		      (mode & SPI_LOOP) ? priv->lbc_bit : 0);

	return 0;
}

/* --- Bus claim/release --- */

static int meson_spicc_claim_bus(struct udevice *dev)
{
	struct udevice *bus = dev->parent;
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	struct dm_spi_slave_plat *slave = dev_get_parent_plat(dev);
	u32 conf;

	/*
	 * Reject re-entrant claims. The cyclic video sync can trigger
	 * dm_spi_claim_bus via udelay/get_timer during another transfer,
	 * which would reconfigure the controller mid-transfer.
	 */
	if (priv->bus_claimed)
		return -EBUSY;

	priv->bus_claimed = true;

	priv->bits_per_word = 8;

	priv->xfer_bpw = priv->bits_per_word;

	/*
	 * Build CONREG preserving DATARATE field (matches Linux driver).
	 */
	conf = spicc_read(priv, SPICC_CONREG) & SPICC_DATARATE_MASK;
	conf |= SPICC_ENABLE | SPICC_MODE_MASTER;
	conf |= FIELD_PREP(SPICC_CS_MASK, slave->cs[0] & 0x3);
	conf |= FIELD_PREP(SPICC_BITLENGTH_MASK, priv->bits_per_word - 1);
	spicc_write(priv, SPICC_CONREG, conf);

	/* Reset FIFOs */
	spicc_reset_fifo(priv);

	/* Re-apply mode and speed */
	meson_spicc_set_mode(bus, priv->mode);
	meson_spicc_set_speed(bus, priv->speed_hz);

	/* Configure IO delays for correct capture timing */
	spicc_auto_io_delay(priv);

	/* Disable interrupts - we poll */
	spicc_write(priv, SPICC_INTREG, 0);
	spicc_write(priv, SPICC_PERIODREG, 0);
	spicc_write(priv, SPICC_DMAREG, 0);

	return 0;
}

static int meson_spicc_release_bus(struct udevice *dev)
{
	struct udevice *bus = dev->parent;
	struct meson_spicc_priv *priv = dev_get_priv(bus);

	if (dm_gpio_is_valid(&priv->cs_gpio))
		dm_gpio_set_value(&priv->cs_gpio, 0);

	spicc_write(priv, SPICC_INTREG, 0);
	spicc_write(priv, SPICC_DMAREG, 0);

	/* Disable controller, preserving DATARATE */
	spicc_write(priv, SPICC_CONREG,
		    spicc_read(priv, SPICC_CONREG) & SPICC_DATARATE_MASK);

	priv->bus_claimed = false;

	return 0;
}

/* --- PIO TX/RX following Linux driver bpw ranges --- */

/*
 * Fill TX FIFO with @count words from @buf.
 *
 * bpw 1-8:   1 byte per word, lower bpw bits used
 * bpw 9-16:  2 bytes per word (u16 little-endian)
 * bpw 17-24: 3 bytes per word (assembled byte-by-byte)
 * bpw 25-32: 4 bytes per word (u32 little-endian)
 * bpw 33-56: 4+N bytes per word, two TXDATA writes (MSB first, LSB second)
 * bpw 57-64: 8 bytes per word, two TXDATA writes (MSB first, LSB second)
 */
static void spicc_pio_fill_tx(struct meson_spicc_priv *priv,
			      const u8 **txp, unsigned int count,
			      unsigned int bpw, unsigned int Bpw)
{
	unsigned int i, j;
	u32 data, data2;

	if (bpw <= 8) {
		for (i = 0; i < count; i++) {
			data = *txp ? *(*txp)++ : 0;
			spicc_write(priv, SPICC_TXDATA, data);
		}
	} else if (bpw <= 16) {
		for (i = 0; i < count; i++) {
			if (*txp) {
				data = *(const u16 *)*txp;
				*txp += 2;
			} else {
				data = 0;
			}
			spicc_write(priv, SPICC_TXDATA, data);
		}
	} else if (bpw <= 24) {
		/* 17-24 bpw: assemble from bytes (Linux approach) */
		for (i = 0; i < count; i++) {
			data = 0;
			if (*txp) {
				for (j = 0; j < Bpw; j++)
					data |= (u32)(*txp)[j] << (j * 8);
				*txp += Bpw;
			}
			spicc_write(priv, SPICC_TXDATA, data);
		}
	} else if (bpw <= 32) {
		for (i = 0; i < count; i++) {
			if (*txp) {
				data = *(const u32 *)*txp;
				*txp += 4;
			} else {
				data = 0;
			}
			spicc_write(priv, SPICC_TXDATA, data);
		}
	} else if (bpw <= 56) {
		/* 33-56 bpw: LSB u32 + MSB bytes, write MSB first */
		for (i = 0; i < count; i++) {
			if (*txp) {
				data2 = *(const u32 *)*txp;	/* LSB */
				data = 0;			/* MSB */
				for (j = 0; j < Bpw - 4; j++)
					data |= (u32)(*txp)[4 + j] << (j * 8);
				*txp += Bpw;
			} else {
				data = data2 = 0;
			}
			spicc_write(priv, SPICC_TXDATA, data);  /* MSB */
			spicc_write(priv, SPICC_TXDATA, data2); /* LSB */
		}
	} else {
		/* 57-64 bpw: two u32, write MSB first */
		for (i = 0; i < count; i++) {
			if (*txp) {
				data2 = *(const u32 *)*txp;	 /* LSB */
				data = *(const u32 *)(*txp + 4); /* MSB */
				*txp += 8;
			} else {
				data = data2 = 0;
			}
			spicc_write(priv, SPICC_TXDATA, data);  /* MSB */
			spicc_write(priv, SPICC_TXDATA, data2); /* LSB */
		}
	}
}

/*
 * Drain RX FIFO of @count words into @buf.
 * Mirror of spicc_pio_fill_tx byte layout.
 */
static void spicc_pio_drain_rx(struct meson_spicc_priv *priv,
			       u8 **rxp, unsigned int count,
			       unsigned int bpw, unsigned int Bpw)
{
	unsigned int i, j;
	u32 data, data2;
	u32 mask = GENMASK(min(bpw, 32U) - 1, 0);
	u32 msb_mask = (bpw > 32) ? GENMASK(bpw - 32 - 1, 0) : 0;

	if (bpw <= 8) {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & mask;
			if (*rxp)
				*(*rxp)++ = data;
		}
	} else if (bpw <= 16) {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & mask;
			if (*rxp) {
				*(u16 *)*rxp = data;
				*rxp += 2;
			}
		}
	} else if (bpw <= 24) {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & mask;
			if (*rxp) {
				for (j = 0; j < Bpw; j++)
					(*rxp)[j] = data >> (j * 8);
				*rxp += Bpw;
			}
		}
	} else if (bpw <= 32) {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & mask;
			if (*rxp) {
				*(u32 *)*rxp = data;
				*rxp += 4;
			}
		}
	} else if (bpw <= 56) {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & msb_mask; /* MSB */
			data2 = spicc_read(priv, SPICC_RXDATA);  /* LSB */
			if (*rxp) {
				*(u32 *)*rxp = data2;		  /* LSB */
				for (j = 0; j < Bpw - 4; j++)
					(*rxp)[4 + j] = data >> (j * 8);
				*rxp += Bpw;
			}
		}
	} else {
		for (i = 0; i < count; i++) {
			data = spicc_read(priv, SPICC_RXDATA) & msb_mask; /* MSB */
			data2 = spicc_read(priv, SPICC_RXDATA);  /* LSB */
			if (*rxp) {
				*(u32 *)*rxp = data2;		  /* LSB */
				*(u32 *)(*rxp + 4) = data;	  /* MSB */
				*rxp += 8;
			}
		}
	}
}

/*
 * PIO transfer following the Linux driver.
 * Transfers up to fifo_size words per burst using XCH, polls TC.
 */
static int meson_spicc_xfer_pio(struct udevice *dev, const void *tx, void *rx,
				unsigned int words, unsigned int bpw,
				unsigned int Bpw)
{
	struct udevice *bus = dev->parent;
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	const u8 *txp = tx;
	u8 *rxp = rx;
	unsigned int burst, fifo_active;
	unsigned long start;

	/*
	 * The FIFO is 64 bits wide. For bpw > 32, two TXDATA/RXDATA
	 * accesses (MSB + LSB) are paired into one 64-bit FIFO entry.
	 * TXCNT counts logical words, so fifo_active is the same
	 * regardless of bpw.
	 */
	fifo_active = priv->data->fifo_size;

	while (words) {
		burst = min_t(unsigned int, words, fifo_active);

		/* Set burst length (Linux: meson_spicc_setup_pio) */
		spicc_setbits(priv, SPICC_CONREG, SPICC_BURSTLENGTH_MASK,
			      FIELD_PREP(SPICC_BURSTLENGTH_MASK, burst - 1));

		/* Wait one SPI clock cycle for CONREG to take effect */
		ndelay(priv->clk_ns);

		/* Fill TX FIFO */
		spicc_pio_fill_tx(priv, &txp, burst, bpw, Bpw);

		/* Wait for TX FIFO to settle before XCH (Linux: ndelay after tx) */
		ndelay(priv->clk_ns);

		/* Clear TC from any previous transfer before starting */
		spicc_write(priv, SPICC_STATREG, SPICC_TC);

		/* Start burst via XCH */
		spicc_setbits(priv, SPICC_CONREG, SPICC_XCH, SPICC_XCH);

		/* Poll for Transfer Complete */
		start = get_timer(0);
		for (;;) {
			if (spicc_read(priv, SPICC_STATREG) & SPICC_TC)
				break;
			if (get_timer(start) > SPICC_TIMEOUT_MS) {
				dev_err(bus, "PIO timeout (%u words)\n", words);
				return -ETIMEDOUT;
			}
			udelay(1);
		}

		/*
		 * XCH quirk: TC may fire while TX FIFO still has data.
		 * Re-trigger XCH and wait again (Linux IRQ handler does this).
		 */
		if (FIELD_GET(SPICC_TXCNT_MASK,
			      spicc_read(priv, SPICC_TESTREG))) {
			spicc_setbits(priv, SPICC_CONREG,
				      SPICC_XCH, SPICC_XCH);
			start = get_timer(0);
			while (!(spicc_read(priv, SPICC_STATREG) & SPICC_TC)) {
				if (get_timer(start) > SPICC_TIMEOUT_MS) {
					dev_err(bus, "PIO XCH retry timeout\n");
					return -ETIMEDOUT;
				}
				udelay(1);
			}
		}

		/* Wait for RX to be ready (Linux: ndelay before rx) */
		ndelay(priv->clk_ns);

		/* Drain RX FIFO */
		spicc_pio_drain_rx(priv, &rxp, burst, bpw, Bpw);

		words -= burst;
	}

	return 0;
}

/* --- DMA transfer --- */

static u32 spicc_calc_dma_burst(struct meson_spicc_priv *priv,
				u32 words, u32 *burst_words)
{
	u32 dma_words_max;
	u32 burst_count;
	unsigned int i;

	if (words <= priv->data->fifo_size) {
		*burst_words = words;
		return 1;
	}

	*burst_words = DMA_BURST_WORDS_DEFAULT;

	dma_words_max = priv->data->dma_bursts_max * DMA_BURST_WORDS_DEFAULT;
	if (words >= dma_words_max)
		return priv->data->dma_bursts_max;

	for (i = DMA_BURST_WORDS_DEFAULT; i > 1; i--) {
		if ((words % i) == 0) {
			*burst_words = i;
			burst_count = words / i;
			if (burst_count <= priv->data->dma_bursts_max)
				return burst_count;
			return priv->data->dma_bursts_max;
		}
	}

	return words / DMA_BURST_WORDS_DEFAULT;
}

static int meson_spicc_xfer_dma(struct udevice *dev, const u8 *tx, u8 *rx,
				unsigned int bytes)
{
	struct udevice *bus = dev->parent;
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	u32 dma_burst_words, dma_burst_count, xfers;
	u32 txfifo_thres, rxfifo_thres;
	size_t padded_len;
	ulong tx_phys, rx_phys;
	unsigned long start;
	u32 dmareg;

	padded_len = (size_t)bytes * SPICC_DMA_WIDTH_BYTES;
	if (padded_len > priv->dma_buf_size) {
		dev_err(bus, "DMA buf too small: need %zu, have %zu\n",
			padded_len, priv->dma_buf_size);
		return -EINVAL;
	}

	dma_burst_count = spicc_calc_dma_burst(priv, bytes, &dma_burst_words);
	xfers = dma_burst_count * dma_burst_words;

	if (tx)
		spicc_pad_data(tx, priv->dma_tx_buf, xfers);
	else
		memset(priv->dma_tx_buf, 0, (size_t)xfers * SPICC_DMA_WIDTH_BYTES);

	tx_phys = (ulong)priv->dma_tx_buf;
	rx_phys = (ulong)priv->dma_rx_buf;
	padded_len = (size_t)xfers * SPICC_DMA_WIDTH_BYTES;

	flush_dcache_range(tx_phys, tx_phys + padded_len);
	invalidate_dcache_range(rx_phys, rx_phys + padded_len);

	/* Reset FIFO before DMA setup (matches Linux driver) */
	spicc_reset_fifo(priv);

	/* Write DMA addresses */
	spicc_write(priv, SPICC_DRADDR, tx_phys);
	spicc_write(priv, SPICC_DWADDR, rx_phys);

	dma_burst_words--;

	if (!priv->data->has_oen) {
		u32 bl = xfers > 128 ? 127 : xfers - 1;

		spicc_setbits(priv, SPICC_CONREG, SPICC_BURSTLENGTH_MASK,
			      FIELD_PREP(SPICC_BURSTLENGTH_MASK, bl));
	}

	spicc_write(priv, SPICC_LD_CNTL0,
		    DMA_READ_COUNTER_EN | DMA_WRITE_COUNTER_EN);
	spicc_write(priv, SPICC_LD_CNTL1,
		    FIELD_PREP(DMA_READ_COUNTER_MASK, dma_burst_count) |
		    FIELD_PREP(DMA_WRITE_COUNTER_MASK, dma_burst_count));

	txfifo_thres = priv->data->fifo_size - dma_burst_words;
	rxfifo_thres = dma_burst_words;

	dmareg = SPICC_DMA_ENABLE | SPICC_DMA_URGENT |
		 FIELD_PREP(SPICC_TXFIFO_THRESHOLD_MASK, txfifo_thres) |
		 FIELD_PREP(SPICC_RXFIFO_THRESHOLD_MASK, rxfifo_thres) |
		 FIELD_PREP(SPICC_READ_BURST_MASK, dma_burst_words) |
		 FIELD_PREP(SPICC_WRITE_BURST_MASK, dma_burst_words);

	spicc_write(priv, SPICC_DMAREG, dmareg);

	spicc_setbits(priv, SPICC_CONREG, SPICC_SMC, SPICC_SMC);

	start = get_timer(0);
	while (spicc_read(priv, SPICC_DMAREG) & SPICC_DMA_ENABLE) {
		if (get_timer(start) > SPICC_TIMEOUT_MS) {
			dev_err(bus, "DMA timeout (%u words, %u bursts)\n",
				xfers, dma_burst_count + 1);
			spicc_setbits(priv, SPICC_CONREG, SPICC_SMC, 0);
			spicc_write(priv, SPICC_DMAREG, 0);
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	spicc_setbits(priv, SPICC_CONREG, SPICC_SMC, 0);
	spicc_write(priv, SPICC_DMAREG, 0);

	invalidate_dcache_range(rx_phys, rx_phys + padded_len);
	if (rx)
		spicc_unpad_data(priv->dma_rx_buf, rx, xfers);

	return xfers;
}

/* --- Main transfer function --- */

static int meson_spicc_xfer(struct udevice *dev, unsigned int bitlen,
			    const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev->parent;
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	unsigned int bpw = priv->bits_per_word;
	unsigned int words, Bpw;
	int ret;

	if (!bitlen)
		return 0;

	if (bitlen < bpw) {
		bpw = bitlen;
	} else if (bitlen % bpw) {
		dev_err(bus, "bitlen %u not aligned to bpw %u\n", bitlen, bpw);
		return -EINVAL;
	}

	words = bitlen / bpw;
	Bpw = DIV_ROUND_UP(bpw, 8);

	/* Update BITLENGTH in CONREG if changed */
	if (bpw != priv->xfer_bpw) {
		spicc_setbits(priv, SPICC_CONREG, SPICC_BITLENGTH_MASK,
			      FIELD_PREP(SPICC_BITLENGTH_MASK, bpw - 1));
		priv->xfer_bpw = bpw;
	}

	if ((flags & SPI_XFER_BEGIN) && dm_gpio_is_valid(&priv->cs_gpio))
		dm_gpio_set_value(&priv->cs_gpio, 1);

	/*
	 * Use DMA for 8-bit transfers >= 2*fifo_size words when buffers
	 * are available. DMA requires minimum 2 FIFO-fulls of data.
	 */
	if (bpw == 8 && words >= priv->data->fifo_size * SPICC_DMA_MIN_FIFOS &&
	    priv->dma_tx_buf) {
		ret = meson_spicc_xfer_dma(dev, dout, din, words);
		if (ret < 0) {
			dev_err(bus, "DMA failed (%d), fallback to PIO\n", ret);
			spicc_reset_fifo(priv);
			ret = meson_spicc_xfer_pio(dev, dout, din, words,
						   bpw, Bpw);
		} else if (ret < (int)words) {
			/* DMA transferred partial, finish remainder via PIO */
			unsigned int done = ret;
			const u8 *tx_rem = dout ? (const u8 *)dout + done : NULL;
			u8 *rx_rem = din ? (u8 *)din + done : NULL;

			spicc_reset_fifo(priv);
			ret = meson_spicc_xfer_pio(dev, tx_rem, rx_rem,
						   words - done, bpw, Bpw);
		} else {
			ret = 0;
		}
	} else {
		ret = meson_spicc_xfer_pio(dev, dout, din, words, bpw, Bpw);
	}

	if ((flags & SPI_XFER_END) && dm_gpio_is_valid(&priv->cs_gpio))
		dm_gpio_set_value(&priv->cs_gpio, 0);

	return ret;
}

/* --- Probe and platform data --- */

static int meson_spicc_probe(struct udevice *bus)
{
	struct meson_spicc_priv *priv = dev_get_priv(bus);
	int ret;

	priv->base = dev_read_addr_ptr(bus);
	if (!priv->base)
		return -EINVAL;

	priv->data = (const struct meson_spicc_data *)dev_get_driver_data(bus);
	if (!priv->data)
		return -EINVAL;

	ret = clk_get_by_name(bus, "core", &priv->core_clk);
	if (ret) {
		dev_err(bus, "failed to get core clock\n");
		return ret;
	}

	ret = clk_enable(&priv->core_clk);
	if (ret) {
		dev_err(bus, "failed to enable core clock\n");
		return ret;
	}

	priv->pclk_enabled = false;
	priv->pclk_rate = 0;
	if (priv->data->has_pclk) {
		ret = clk_get_by_name(bus, "pclk", &priv->pclk);
		if (!ret) {
			ret = clk_enable(&priv->pclk);
			if (!ret) {
				priv->pclk_enabled = true;
				priv->pclk_rate = clk_get_rate(&priv->pclk);
			}
		}

		if (!priv->pclk_enabled) {
			void __iomem *hhi = (void __iomem *)G12A_HHI_SPICC_CLK_CNTL;
			u32 val;

			val = readl(hhi);
			val &= ~(SPICC1_SCLK_MUX_MASK | SPICC1_SCLK_DIV_MASK |
				  SPICC1_SCLK_GATE);
			val |= (SPICC1_SCLK_MUX_FCLK_DIV4 << SPICC1_SCLK_MUX_SHIFT) |
			       SPICC1_SCLK_GATE;
			writel(val, hhi);
			priv->pclk_rate = SPICC1_SCLK_FCLK_DIV4_HZ;
			dev_dbg(bus, "pclk: direct fclk_div4 setup, rate %lu\n",
				priv->pclk_rate);
		}
	}

	/* Allocate DMA padded buffers (8 bytes per word) */
	priv->dma_buf_size = SPICC_DMA_MAX_BYTES * SPICC_DMA_WIDTH_BYTES;
	priv->dma_tx_buf = memalign(64, priv->dma_buf_size);
	priv->dma_rx_buf = memalign(64, priv->dma_buf_size);
	if (!priv->dma_tx_buf || !priv->dma_rx_buf) {
		dev_warn(bus, "DMA buffer alloc failed, DMA disabled\n");
		free(priv->dma_tx_buf);
		free(priv->dma_rx_buf);
		priv->dma_tx_buf = NULL;
		priv->dma_rx_buf = NULL;
		priv->dma_buf_size = 0;
	}

	ret = gpio_request_by_name(bus, "cs-gpios", 0, &priv->cs_gpio,
				   GPIOD_IS_OUT);
	if (ret && ret != -ENOENT) {
		dev_err(bus, "failed to get cs-gpios: %d\n", ret);
		return ret;
	}

	/* Detect LBC bit positions before clearing registers */
	spicc_detect_lbc(bus);

	/* Clear all registers to known state */
	spicc_write(priv, SPICC_CONREG, 0);
	spicc_write(priv, SPICC_INTREG, 0);
	spicc_write(priv, SPICC_DMAREG, 0);
	spicc_write(priv, SPICC_TESTREG, 0);

	spicc_oen_enable(priv);

	priv->mode = SPI_MODE_0;
	priv->speed_hz = 1000000;
	priv->last_speed_hz = 0;
	priv->clk_ns = 1000;

	return 0;
}

static int meson_spicc_remove(struct udevice *bus)
{
	struct meson_spicc_priv *priv = dev_get_priv(bus);

	free(priv->dma_tx_buf);
	free(priv->dma_rx_buf);

	return 0;
}

static const struct dm_spi_ops meson_spicc_ops = {
	.claim_bus	= meson_spicc_claim_bus,
	.release_bus	= meson_spicc_release_bus,
	.xfer		= meson_spicc_xfer,
	.set_speed	= meson_spicc_set_speed,
	.set_mode	= meson_spicc_set_mode,
};

static const struct meson_spicc_data meson_spicc_gx_data = {
	.max_speed_hz		= 41666666,
	.min_speed_hz		= 325521,
	.fifo_size		= 16,
	.dma_bursts_max		= 64,
	.has_oen		= false,
	.has_enhance_clk_div	= false,
	.has_pclk		= false,
};

static const struct meson_spicc_data meson_spicc_axg_data = {
	.max_speed_hz		= 83333332,
	.min_speed_hz		= 325000,
	.fifo_size		= 16,
	.dma_bursts_max		= 65535,
	.has_oen		= true,
	.has_enhance_clk_div	= true,
	.has_pclk		= false,
};

static const struct meson_spicc_data meson_spicc_g12a_data = {
	.max_speed_hz		= 166666664,
	.min_speed_hz		= 50000,
	.fifo_size		= 15,	/* 15 x 64-bit entries; 16 causes stalls */
	.dma_bursts_max		= 65535,
	.has_oen		= true,
	.has_enhance_clk_div	= true,
	.has_pclk		= true,
};

static const struct udevice_id meson_spicc_ids[] = {
	{ .compatible = "amlogic,meson-gx-spicc",
	  .data = (ulong)&meson_spicc_gx_data },
	{ .compatible = "amlogic,meson-axg-spicc",
	  .data = (ulong)&meson_spicc_axg_data },
	{ .compatible = "amlogic,meson-g12a-spicc",
	  .data = (ulong)&meson_spicc_g12a_data },
	{ }
};

U_BOOT_DRIVER(meson_spicc) = {
	.name		= "meson_spicc",
	.id		= UCLASS_SPI,
	.of_match	= meson_spicc_ids,
	.ops		= &meson_spicc_ops,
	.probe		= meson_spicc_probe,
	.remove		= meson_spicc_remove,
	.priv_auto	= sizeof(struct meson_spicc_priv),
};

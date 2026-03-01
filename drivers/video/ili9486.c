// SPDX-License-Identifier: GPL-2.0+
/*
 * ILI9486 SPI TFT display driver for U-Boot
 *
 * Copyright (C) 2024 Da Xue <da@libre.computer>
 *
 * Supports 480x320 RGB565 displays using the ILI9486 controller
 * behind a SPI-to-16-bit-parallel converter (Waveshare/PiScreen).
 *
 * All commands and parameters are sent as 16-bit words (zero-padded)
 * because the converter latches 16 SPI clocks into a parallel word.
 * Pixel data is byte-swapped from LE framebuffer to BE SPI order.
 *
 * Init sequence follows Linux DRM drivers/gpu/drm/tiny/ili9486.c.
 */

#include <asm/byteorder.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <malloc.h>
#include <memalign.h>
#include <spi.h>
#include <video.h>
#include <asm/gpio.h>
#include <dm/device_compat.h>
#include <linux/delay.h>

#define ILI9486_WIDTH		480
#define ILI9486_HEIGHT		320

/* ILI9486 commands */
#define ILI9486_SLPOUT		0x11
#define ILI9486_DISPON		0x29
#define ILI9486_CASET		0x2A
#define ILI9486_PASET		0x2B
#define ILI9486_RAMWR		0x2C
#define ILI9486_MADCTL		0x36
#define ILI9486_PIXFMT		0x3A
#define ILI9486_ITFCTR1		0xB0
#define ILI9486_PWCTRL1		0xC2
#define ILI9486_VMCTRL1		0xC5
#define ILI9486_PGAMCTRL	0xE0
#define ILI9486_NGAMCTRL	0xE1
#define ILI9486_DGAMCTRL	0xE2

/* MADCTL bits */
#define MADCTL_MV	BIT(5)
#define MADCTL_MX	BIT(6)
#define MADCTL_MY	BIT(7)
#define MADCTL_BGR	BIT(3)

struct ili9486_priv {
	struct gpio_desc reset_gpio;
	struct gpio_desc dc_gpio;
	struct udevice *dev;
	u8 *tx_buf;
};

/*
 * Send a 16-bit-padded command (DC=0).
 * The SPI-to-parallel converter latches 16 clocks per word.
 */
static int ili9486_write_cmd(struct udevice *dev, u8 cmd)
{
	struct ili9486_priv *priv = dev_get_priv(dev);
	u8 buf[2] = { 0x00, cmd };

	dm_gpio_set_value(&priv->dc_gpio, 0);
	return dm_spi_xfer(dev, 16, buf, NULL, SPI_XFER_BEGIN | SPI_XFER_END);
}

/*
 * Send command + parameters, each 16-bit padded, in one CS transaction.
 */
static int ili9486_cmd_data(struct udevice *dev, u8 cmd, const u8 *data,
			    size_t len)
{
	struct ili9486_priv *priv = dev_get_priv(dev);
	u8 buf[64];
	int ret;
	size_t i;

	buf[0] = 0x00;
	buf[1] = cmd;
	dm_gpio_set_value(&priv->dc_gpio, 0);

	if (len > 0) {
		ret = dm_spi_xfer(dev, 16, buf, NULL, SPI_XFER_BEGIN);
		if (ret)
			return ret;

		for (i = 0; i < len; i++) {
			buf[i * 2] = 0x00;
			buf[i * 2 + 1] = data[i];
		}
		dm_gpio_set_value(&priv->dc_gpio, 1);
		ret = dm_spi_xfer(dev, len * 16, buf, NULL, SPI_XFER_END);
	} else {
		ret = dm_spi_xfer(dev, 16, buf, NULL,
				  SPI_XFER_BEGIN | SPI_XFER_END);
	}

	return ret;
}

static int ili9486_set_window(struct udevice *dev, u16 x0, u16 y0,
			      u16 x1, u16 y1)
{
	u8 data[4];
	int ret;

	data[0] = x0 >> 8;
	data[1] = x0 & 0xff;
	data[2] = x1 >> 8;
	data[3] = x1 & 0xff;
	ret = ili9486_cmd_data(dev, ILI9486_CASET, data, 4);
	if (ret)
		return ret;

	data[0] = y0 >> 8;
	data[1] = y0 & 0xff;
	data[2] = y1 >> 8;
	data[3] = y1 & 0xff;
	return ili9486_cmd_data(dev, ILI9486_PASET, data, 4);
}

/*
 * Init sequence from Linux DRM drivers/gpu/drm/tiny/ili9486.c
 */
static int ili9486_display_init(struct udevice *dev)
{
	int ret;

	/* Interface Mode Control - no params */
	ret = ili9486_write_cmd(dev, ILI9486_ITFCTR1);
	if (ret)
		return ret;

	/* Sleep Out */
	ret = ili9486_write_cmd(dev, ILI9486_SLPOUT);
	if (ret)
		return ret;
	mdelay(250);

	/* Pixel format: 16bpp (RGB565) */
	ret = ili9486_cmd_data(dev, ILI9486_PIXFMT, (u8[]){0x55}, 1);
	if (ret)
		return ret;

	/* Power Control 3 */
	ret = ili9486_cmd_data(dev, ILI9486_PWCTRL1, (u8[]){0x44}, 1);
	if (ret)
		return ret;

	/* VCOM Control */
	ret = ili9486_cmd_data(dev, ILI9486_VMCTRL1,
			       (u8[]){0x00, 0x00, 0x00, 0x00}, 4);
	if (ret)
		return ret;

	/* Positive Gamma Correction */
	ret = ili9486_cmd_data(dev, ILI9486_PGAMCTRL,
		(u8[]){0x0F, 0x1F, 0x1C, 0x0C, 0x0F, 0x08, 0x48, 0x98,
		       0x37, 0x0A, 0x13, 0x04, 0x11, 0x0D, 0x00}, 15);
	if (ret)
		return ret;

	/* Negative Gamma Correction */
	ret = ili9486_cmd_data(dev, ILI9486_NGAMCTRL,
		(u8[]){0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
		       0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00}, 15);
	if (ret)
		return ret;

	/* Digital Gamma Control */
	ret = ili9486_cmd_data(dev, ILI9486_DGAMCTRL,
		(u8[]){0x0F, 0x32, 0x2E, 0x0B, 0x0D, 0x05, 0x47, 0x75,
		       0x37, 0x06, 0x10, 0x03, 0x24, 0x20, 0x00}, 15);
	if (ret)
		return ret;

	/* Display On */
	ret = ili9486_write_cmd(dev, ILI9486_DISPON);
	if (ret)
		return ret;
	mdelay(100);

	/* Memory Access Control - landscape, BGR (after DISPON per Linux) */
	ret = ili9486_cmd_data(dev, ILI9486_MADCTL,
			       (u8[]){MADCTL_MV | MADCTL_MY | MADCTL_MX |
				      MADCTL_BGR}, 1);
	if (ret)
		return ret;

	return 0;
}

static int ili9486_sync(struct udevice *vid)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(vid);
	struct ili9486_priv *priv = dev_get_priv(vid);
	struct udevice *dev = priv->dev;
	u16 *fb = (u16 *)uc_priv->fb;
	size_t npixels = (size_t)uc_priv->xsize * uc_priv->ysize;
	size_t fb_size = npixels * 2;
	u8 cmd_buf[2] = { 0x00, ILI9486_RAMWR };
	u16 *tx = (u16 *)priv->tx_buf;
	size_t i;
	int ret;

	ret = dm_spi_claim_bus(dev);
	if (ret)
		return 0;

	ret = ili9486_set_window(dev, 0, 0,
				 uc_priv->xsize - 1, uc_priv->ysize - 1);
	if (ret)
		goto out;

	/* RAMWR command (16-bit padded), DC=0 */
	dm_gpio_set_value(&priv->dc_gpio, 0);
	ret = dm_spi_xfer(dev, 16, cmd_buf, NULL, SPI_XFER_BEGIN);
	if (ret)
		goto out;

	/* Byte-swap pixels: LE framebuffer → BE SPI byte order */
	for (i = 0; i < npixels; i++)
		tx[i] = __swab16(fb[i]);

	dm_gpio_set_value(&priv->dc_gpio, 1);
	flush_dcache_range((ulong)priv->tx_buf,
			   (ulong)priv->tx_buf + fb_size);
	ret = dm_spi_xfer(dev, fb_size * 8, priv->tx_buf, NULL, SPI_XFER_END);

out:
	dm_spi_release_bus(dev);
	return ret;
}

static void ili9486_hw_reset(struct ili9486_priv *priv)
{
	/* Match Linux mipi_dbi_hw_reset(): assert low, release high */
	dm_gpio_set_value(&priv->reset_gpio, 0);
	mdelay(1);
	dm_gpio_set_value(&priv->reset_gpio, 1);
	mdelay(120);
}

static int ili9486_probe(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct ili9486_priv *priv = dev_get_priv(dev);
	int ret;

	ret = gpio_request_by_name(dev, "reset-gpios", 0,
				   &priv->reset_gpio, GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "missing reset GPIO: %d\n", ret);
		return ret;
	}

	ret = gpio_request_by_name(dev, "dc-gpios", 0,
				   &priv->dc_gpio, GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "missing dc GPIO: %d\n", ret);
		return ret;
	}

	uc_priv->bpix = VIDEO_BPP16;
	uc_priv->xsize = ILI9486_WIDTH;
	uc_priv->ysize = ILI9486_HEIGHT;
	uc_priv->rot = 0;

	priv->dev = dev;

	priv->tx_buf = memalign(64, ILI9486_WIDTH * ILI9486_HEIGHT * 2);
	if (!priv->tx_buf) {
		dev_err(dev, "Failed to allocate TX buffer\n");
		return -ENOMEM;
	}

	ili9486_hw_reset(priv);

	ret = dm_spi_claim_bus(dev);
	if (ret) {
		dev_err(dev, "Failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	ret = ili9486_display_init(dev);
	if (ret) {
		dev_err(dev, "Display init failed: %d\n", ret);
		dm_spi_release_bus(dev);
		return ret;
	}

	dm_spi_release_bus(dev);

	printf("ILI9486: %dx%d SPI display initialized\n",
	       ILI9486_WIDTH, ILI9486_HEIGHT);

	return 0;
}

static int ili9486_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = ILI9486_WIDTH * ILI9486_HEIGHT * 2;

	/*
	 * SPI display devices bind after relocation, so video_reserve()
	 * never sees them. Self-allocate the framebuffer so video_post_bind()
	 * skips the reserved-memory check.
	 */
	if (gd->flags & GD_FLG_RELOC) {
		void *fb = memalign(64, plat->size);

		if (!fb)
			return -ENOMEM;
		plat->base = (ulong)fb;
	}

	return 0;
}

static const struct video_ops ili9486_ops = {
	.video_sync = ili9486_sync,
};

static const struct udevice_id ili9486_ids[] = {
	{ .compatible = "ilitek,ili9486" },
	{ .compatible = "waveshare,rpi-lcd-35" },
	{ }
};

U_BOOT_DRIVER(ili9486_video) = {
	.name		= "ili9486_video",
	.id		= UCLASS_VIDEO,
	.of_match	= ili9486_ids,
	.ops		= &ili9486_ops,
	.plat_auto	= sizeof(struct video_uc_plat),
	.bind		= ili9486_bind,
	.probe		= ili9486_probe,
	.priv_auto	= sizeof(struct ili9486_priv),
};

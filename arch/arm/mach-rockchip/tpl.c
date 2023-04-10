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
#include <linux/delay.h>
#include <stdlib.h>

#define GRF_GPIO2A_IOMUX	0xff100020
#define GRF_GPIO2A_P		0xff100120
#define GRF_GPIO1D_IOMUX	0xff10001c
#define GRF_GPIO1D_P		0xff10011c

struct i2c_regs {
        u32 con;
        u32 clkdiv;
        u32 mrxaddr;
        u32 mrxraddr;
        u32 mtxcnt;
        u32 mrxcnt;
        u32 ien;
        u32 ipd;
        u32 fcnt;
        u32 reserved0[0x37];
        u32 txdata[8];
        u32 reserved1[0x38];
        u32 rxdata[8];
};

#define I2C1_REG_BASE           0xFF160000
#define I2C_MAX_OFFSET_LEN	4

#define I2C_CLKDIV_VAL(divl, divh) \
	(((divl) & 0xffff) | (((divh) << 16) & 0xffff0000))

#define RK8XX_ID_MSK		0xfff0
#define RK8XX_ON_SOURCE		0xae
#define RK8XX_OFF_SOURCE	0xaf

enum {
	RK816_REG_DCDC_EN1 = 0x23,
	RK816_REG_DCDC_EN2,
	RK816_REG_DCDC_SLP_EN,
	RK816_REG_LDO_SLP_EN,
	RK816_REG_LDO_EN1 = 0x27,
	RK816_REG_LDO_EN2,
};

/* the slave address accessed  for master rx mode */
#define I2C_MRXADDR_SET(vld, addr)		(((vld) << 24) | (addr))

/* the slave register address accessed  for master rx mode */
#define I2C_MRXRADDR_SET(vld, raddr)		(((vld) << 24) | (raddr))

/* i2c timerout */
#define I2C_TIMEOUT_MS          100

/* rk i2c fifo max transfer bytes */
#define RK_I2C_FIFO_SIZE        32

#define I2C_CON_EN              (1 << 0)
#define I2C_CON_MOD(mod)        ((mod) << 1)
#define I2C_MODE_TX             0x00
#define I2C_MODE_TRX            0x01
#define I2C_MODE_RX             0x02

#define I2C_CON_START           (1 << 3)
#define I2C_CON_STOP            (1 << 4)
#define I2C_CON_LASTACK         (1 << 5)
#define I2C_CON_ACTACK          (1 << 6)

/* interrupt enable register */
#define I2C_MBTFIEN             (1 << 2)
#define I2C_MBRFIEN             (1 << 3)
#define I2C_STARTIEN            (1 << 4)
#define I2C_NAKRCVIEN           (1 << 6)

/* interrupt pending register */
#define I2C_MBTFIPD		(1 << 2)
#define I2C_MBRFIPD             (1 << 3)
#define I2C_STARTIPD            (1 << 4)
#define I2C_STOPIPD             (1 << 5)
#define I2C_NAKRCVIPD           (1 << 6)
#define I2C_IPD_ALL_CLEAN       0x7f

enum i2c_msg_flags {
        I2C_M_TEN               = 0x0010, /* ten-bit chip address */
        I2C_M_RD                = 0x0001, /* read data, from slave to master */
        I2C_M_STOP              = 0x8000, /* send stop after this message */
        I2C_M_NOSTART           = 0x4000, /* no start before this message */
        I2C_M_REV_DIR_ADDR      = 0x2000, /* invert polarity of R/W bit */
        I2C_M_IGNORE_NAK        = 0x1000, /* continue after NAK */
        I2C_M_NO_RD_ACK         = 0x0800, /* skip the Ack bit on reads */
        I2C_M_RECV_LEN          = 0x0400, /* length is first received byte */
};

struct i2c_msg {
        uint addr;
        uint flags;
        uint len;
        u8 *buf;
};
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
int i2c1_send_start_bit(void)
{
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;
	ulong start;

	writel(I2C_IPD_ALL_CLEAN, &iregs->ipd); //0x001c 0xFF160004
	writel(I2C_CON_EN | I2C_CON_START, &iregs->con); //0x0000
	writel(I2C_STARTIEN, &iregs->ien); //0x0018

	start = get_timer(0);
	while (1) {
		if (readl(&iregs->ipd) & I2C_STARTIPD) {
			writel(I2C_STARTIPD, &iregs->ipd); //0x001c
			break;
		}
		if (get_timer(start) > I2C_TIMEOUT_MS)
			return -ETIMEDOUT;
		udelay(1);
	}
	return 0;
}

int i2c1_send_stop_bit(void)
{
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;
	ulong start;

	writel(I2C_IPD_ALL_CLEAN, &iregs->ipd);
	writel(I2C_CON_EN | I2C_CON_STOP, &iregs->con);
	writel(I2C_CON_STOP, &iregs->ien);

	start = get_timer(0);
	while (1) {
		if (readl(&iregs->ipd) & I2C_STOPIPD) {
			writel(I2C_STOPIPD, &iregs->ipd);
			break;
		}
		if (get_timer(start) > I2C_TIMEOUT_MS) {
			printf("I2C Send Start Bit Timeout\n");
			return -ETIMEDOUT;
		}
		udelay(1);
	}

	return 0;
}

inline void i2c1_disable(void)
{
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;

	writel(0, &iregs->con);
}

int i2c1_read(uchar chip, uint reg, uint r_len, uchar *buf, uint b_len)
{
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;
	uchar *pbuf = buf;
	uint bytes_remain_len = b_len;
	uint bytes_xferred = 0;
	uint words_xferred = 0;
	ulong start;
	uint con = 0;
	uint rxdata;
	uint i, j;
	int err;
	bool snd_chunk = false;

	err = i2c1_send_start_bit();
	if (err)
		return err;

	writel(I2C_MRXADDR_SET(1, chip << 1 | 1), &iregs->mrxaddr);
	if (r_len == 0) {
		writel(0, &iregs->mrxraddr);
	} else if (r_len < 4) {
		writel(I2C_MRXRADDR_SET(r_len, reg), &iregs->mrxraddr);
	} else {
		printf("I2C Read: addr len %d not supported\n", r_len);
		return -EIO;
	}

	while (bytes_remain_len) {
		if (bytes_remain_len > RK_I2C_FIFO_SIZE) {
			con = I2C_CON_EN;
			bytes_xferred = 32;
		} else {
			/*
			 * The hw can read up to 32 bytes at a time. If we need
			 * more than one chunk, send an ACK after the last byte.
			 */
			con = I2C_CON_EN | I2C_CON_LASTACK;
			bytes_xferred = bytes_remain_len;
		}
		words_xferred = DIV_ROUND_UP(bytes_xferred, 4);

		/*
		 * make sure we are in plain RX mode if we read a second chunk
		 */
		if (snd_chunk)
			con |= I2C_CON_MOD(I2C_MODE_RX);
		else
			con |= I2C_CON_MOD(I2C_MODE_TRX);

		writel(con, &iregs->con);
		writel(bytes_xferred, &iregs->mrxcnt);
		writel(I2C_MBRFIEN | I2C_NAKRCVIEN, &iregs->ien);

		start = get_timer(0);
		while (1) {
			if (readl(&iregs->ipd) & I2C_NAKRCVIPD) {
				writel(I2C_NAKRCVIPD, &iregs->ipd);
				err = -EREMOTEIO;
			}
			if (readl(&iregs->ipd) & I2C_MBRFIPD) {
				writel(I2C_MBRFIPD, &iregs->ipd);
				break;
			}
			if (get_timer(start) > I2C_TIMEOUT_MS) {
				printf("I2C Read Data Timeout\n");
				err =  -ETIMEDOUT;
				goto i2c_exit;
			}
			udelay(1);
		}

		for (i = 0; i < words_xferred; i++) {
			rxdata = readl(&iregs->rxdata[i]);
			for (j = 0; j < 4; j++) {
				if ((i * 4 + j) == bytes_xferred)
					break;
				*pbuf++ = (rxdata >> (j * 8)) & 0xff;
			}
		}

		bytes_remain_len -= bytes_xferred;
		snd_chunk = true;
	}

i2c_exit:
	i2c1_disable();

	return err;
}

int i2c1_write(uchar chip, uint reg, uint r_len, uchar *buf, uint b_len)
{
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;
	int err;
	uchar *pbuf = buf;
	uint bytes_remain_len = b_len + r_len + 1;
	uint bytes_xferred = 0;
	uint words_xferred = 0;
	ulong start;
	uint txdata;
	uint i, j;

	err = i2c1_send_start_bit();
	if (err)
		return err;

	while (bytes_remain_len) {
		if (bytes_remain_len > RK_I2C_FIFO_SIZE)
			bytes_xferred = RK_I2C_FIFO_SIZE;
		else
			bytes_xferred = bytes_remain_len;
		words_xferred = DIV_ROUND_UP(bytes_xferred, 4);

		for (i = 0; i < words_xferred; i++) {
			txdata = 0;
			for (j = 0; j < 4; j++) {
				if ((i * 4 + j) == bytes_xferred)
					break;

				if (i == 0 && j == 0 && pbuf == buf) {
					txdata |= (chip << 1);
				} else if (i == 0 && j <= r_len && pbuf == buf) {
					txdata |= (reg &
						(0xff << ((j - 1) * 8))) << 8;
				} else {
					txdata |= (*pbuf++)<<(j * 8);
				}
			}
			writel(txdata, &iregs->txdata[i]);
		}

		writel(I2C_CON_EN | I2C_CON_MOD(I2C_MODE_TX), &iregs->con);
		writel(bytes_xferred, &iregs->mtxcnt);
		writel(I2C_MBTFIEN | I2C_NAKRCVIEN, &iregs->ien);

		start = get_timer(0);

		while (1) {
			if (readl(&iregs->ipd) & I2C_NAKRCVIPD) {
				writel(I2C_NAKRCVIPD, &iregs->ipd);
				err = -EREMOTEIO;
			}
			if (readl(&iregs->ipd) & I2C_MBTFIPD) {
				writel(I2C_MBTFIPD, &iregs->ipd);
				break;
			}
			if (get_timer(start) > I2C_TIMEOUT_MS) {
				printf("I2C Write Data Timeout\n");
				err =  -ETIMEDOUT;
				goto i2c_exit;
			}
			udelay(1);
		}

		bytes_remain_len -= bytes_xferred;

	}

i2c_exit:
	i2c1_disable();

	return err;
}

int i2c1_xfer(struct i2c_msg *msg, int nmsgs)
{
	int ret;


	for (; nmsgs > 0; nmsgs--, msg++) {
		if (msg->flags & I2C_M_RD) {
			ret = i2c1_read(msg->addr, 0, 0, msg->buf, msg->len);
		} else {
			ret = i2c1_write(msg->addr, 0, 0, msg->buf, msg->len);
		}
		if (ret) {
			printf("i2c_write: error sending\n");
			return -EREMOTEIO;
		}
	}

	i2c1_send_stop_bit();
	i2c1_disable();

	return 0;
}

int i2c1_setup_offset(uint offset, uint8_t offset_buf[], struct i2c_msg *msg)
{
        int offset_len = 1;

        msg->addr = 0x18; //24
        msg->flags = 0;
        msg->len = 1;

        msg->buf = offset_buf;
        if (!offset_len)
                return -EADDRNOTAVAIL;

        assert(offset_len <= I2C_MAX_OFFSET_LEN);

        while (offset_len--)
                *offset_buf++ = offset >> (8 * offset_len);

        return 0;
}

void rk805_read(uint reg, uint8_t *buff, int len)
{
	struct i2c_msg msg[2], *ptr;
	uint8_t offset_buf[I2C_MAX_OFFSET_LEN];
	int msg_count, ret;

	ptr = msg;

	if (!i2c1_setup_offset(reg, offset_buf, ptr))
		ptr++;

	if (len) {
		ptr->addr = msg->addr;
		ptr->flags = I2C_M_RD;
		ptr->len = len;
		ptr->buf = buff;
		ptr++;
	}
	msg_count = ptr - msg;

	ret = i2c1_xfer(msg, msg_count);
	if(ret)
		printf("i2c_write: error sending\n");
}

int rk805_write(uint reg, const uint8_t *buffer, int len)
{
	struct i2c_msg msg[1];
	uint8_t _buf[I2C_MAX_OFFSET_LEN + 64];
	uint8_t *buf = _buf;
	int ret;

	if (len > sizeof(_buf) - I2C_MAX_OFFSET_LEN) {
		buf = malloc(I2C_MAX_OFFSET_LEN + len);
		if (!buf)
			return -ENOMEM;
	}

	i2c1_setup_offset(reg, buf, msg);
	msg->len += len;
	memcpy(buf + 1, buffer, len);

	ret = i2c1_xfer(msg, 1);
	if(ret)
		printf("i2c_write: error sending\n");

	if (buf != _buf)
		free(buf);

	return 0;
}

void rk805_probe(void)
{
	u8 msb, lsb, id_msb, id_lsb;
	u8 on_source = 0, off_source = 0;
	int variant;
	u32 val_on = 0;
	u32 val_off = 0;

	id_msb = 23;
	msb = 0;

	rk805_read(id_msb, &msb, 1); // 23, 0, 1

	id_lsb = 24;
	lsb = 0;

	rk805_read(id_lsb, &lsb, 1); //24, 0, 1

	variant = ((msb << 8) | lsb) & RK8XX_ID_MSK;
	on_source = RK8XX_ON_SOURCE;
	off_source = RK8XX_OFF_SOURCE;

	rk805_read(on_source, (uint8_t *)&val_on, 1);
	rk805_read(off_source, (uint8_t *)&val_off, 1);

	printf("PMIC:  RK%x ", variant);

	if (on_source && off_source)
		printf("(on=0x%02x, off=0x%02x)", val_on, val_off);
	printf("\n");
}

void rockchip_i2c1_init(void)
{
	int divl, divh;
	struct i2c_regs *iregs = (struct i2c_regs *)I2C1_REG_BASE;

	divl = 44;
	divh = 44;
	writel(I2C_CLKDIV_VAL(divl, divh), &iregs->clkdiv);

	rk805_probe();
}

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

void buck1_clrsetbits(uint reg, uint clr, uint set)
{
	u32 val = 0;

	rk805_read(reg, (uint8_t *)&val, 1);

	val = (val & ~clr) | set;
	rk805_write(reg, (uint8_t *)&val, 1);
}

void buck1_set_suspend_enable(int buck, bool enable)
{
	uint mask = 0;

	mask = 1 << buck;

	buck1_clrsetbits(RK816_REG_DCDC_SLP_EN, mask, enable ? mask : 0);
}

void buck1_set_suspend_value(int buck, int uvolt)
{
	int mask = 0x3f;
	int val;

	val = 0x17;

	buck1_clrsetbits(0x30, mask, val); // info->vsel_sleep_reg = 0x30
}

void buck1_set_enable(int buck, bool enable)
{
	uint value, en_reg;

	en_reg = 0x23;
	value = 0x11;

	rk805_write(en_reg, (uint8_t *)&value, 1);
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

	/* init i2c1 */
	rockchip_i2c1_init();

	 /* init regulator - BUCK1 - VDD_LOG */
	buck1_set_suspend_enable(0, 1); /* buck = 0, enable =1 */
	buck1_set_suspend_value(0, 1000000); /* buck = 0, uvolt = 1000000 */
	buck1_set_enable(0, 1); /* buck = 0, enable =1 */
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

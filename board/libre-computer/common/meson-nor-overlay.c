// SPDX-License-Identifier: GPL-2.0+
/*
 * NOR-based DT overlay loader for Meson boards
 *
 * Reads a U-Boot env block from SPI NOR at a fixed offset to find an
 * "overlays=" key listing DTBO names. Loads a FIT image from NOR containing
 * pre-compiled DTBOs, then applies each requested overlay to the control FDT
 * before DM init.
 *
 * Uses bare-metal SPIFC register access since this runs before DM is up.
 * BL2 has already configured SPIFC clocks and pinmux for NOR boot.
 *
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <linux/libfdt.h>
#include <linux/types.h>
#include <asm/io.h>
#include <u-boot/crc.h>
#include <fdt_support.h>
#include <string.h>

/* NOR layout */
#define NOR_ENV_OFFSET		0x1F0000
#define NOR_ENV_SIZE		0x10000		/* 64KB — one erase block */
#define NOR_FIT_OFFSET		0x200000
#define NOR_FIT_MAX_SIZE	0x200000	/* 2MB max */

/* Scratch buffer in high DRAM — safe, nothing uses this pre-DM */
#define SCRATCH_ADDR		0x20000000

/* SPIFC register map (from drivers/spi/meson_spifc.c) */
#define SPIFC_BASE		0xffd14000

#define REG_CMD			0x00
#define REG_CTRL		0x08
#define REG_USER		0x1c
#define REG_USER1		0x20
#define REG_USER4		0x2c
#define REG_SLAVE		0x30
#define REG_C0			0x40

#define CMD_USER		BIT(18)
#define CTRL_ENABLE_AHB		BIT(17)
#define USER_DIN_EN_MS		BIT(0)
#define USER_UC_DOUT_SEL	BIT(27)
#define USER_UC_MASK		((BIT(5) - 1) << 27)
#define USER4_CS_ACT		BIT(30)
#define SLAVE_TRST_DONE		BIT(4)
#define SLAVE_OP_MODE		BIT(30)
#define SLAVE_SW_RST		BIT(31)

#define SPIFC_BUF_SIZE		64
#define SPIFC_TIMEOUT_US	100000	/* 100ms per chunk */

/* SPI NOR READ command */
#define SPI_NOR_CMD_READ	0x03

static inline u32 spifc_read(u32 reg)
{
	return readl(SPIFC_BASE + reg);
}

static inline void spifc_write(u32 reg, u32 val)
{
	writel(val, SPIFC_BASE + reg);
}

static inline void spifc_set(u32 reg, u32 mask)
{
	spifc_write(reg, spifc_read(reg) | mask);
}

static inline void spifc_clr(u32 reg, u32 mask)
{
	spifc_write(reg, spifc_read(reg) & ~mask);
}

static inline void spifc_update(u32 reg, u32 mask, u32 val)
{
	spifc_write(reg, (spifc_read(reg) & ~mask) | (val & mask));
}

/*
 * Transfer one chunk (up to 64 bytes) via SPIFC user mode.
 * dout/din may be NULL independently. keep_cs keeps CS asserted after transfer.
 */
static int spifc_chunk(const u8 *dout, u8 *din, int len, bool keep_cs)
{
	u32 data;
	int i, timeout;

	/* fill TX buffer */
	if (dout) {
		for (i = 0; i < len; i += 4) {
			if (len - i >= 4)
				data = *(const u32 *)(dout + i);
			else {
				data = 0;
				memcpy(&data, dout + i, len - i);
			}
			spifc_write(REG_C0 + i, data);
		}
	}

	/* enable DOUT stage */
	spifc_update(REG_USER, USER_UC_MASK, USER_UC_DOUT_SEL);
	/* set bit count */
	spifc_write(REG_USER1, (8 * len - 1) << 17);
	/* enable simultaneous RX */
	spifc_set(REG_USER, USER_DIN_EN_MS);

	/* CS control */
	spifc_update(REG_USER4, USER4_CS_ACT, keep_cs ? USER4_CS_ACT : 0);

	/* clear done, start */
	spifc_clr(REG_SLAVE, SLAVE_TRST_DONE);
	spifc_set(REG_CMD, CMD_USER);

	/* poll completion */
	timeout = SPIFC_TIMEOUT_US;
	while (!(spifc_read(REG_SLAVE) & SLAVE_TRST_DONE)) {
		if (--timeout <= 0)
			return -1;
		/* ~1us per iteration on typical Cortex-A53 */
	}

	/* drain RX buffer */
	if (din) {
		for (i = 0; i < len; i += 4) {
			data = spifc_read(REG_C0 + i);
			if (len - i >= 4)
				*(u32 *)(din + i) = data;
			else
				memcpy(din + i, &data, len - i);
		}
	}

	return 0;
}

/*
 * Read 'len' bytes from SPI NOR at 'nor_offset' into 'buf'.
 * Issues standard READ (0x03) command with 24-bit address.
 */
static int spifc_raw_read(u32 nor_offset, void *buf, u32 len)
{
	u8 cmd[4];
	u8 *p = buf;
	u32 done = 0;
	int chunk, ret;

	/* disable AHB for user-mode access */
	spifc_clr(REG_CTRL, CTRL_ENABLE_AHB);

	/* send READ command + 3-byte address, keep CS */
	cmd[0] = SPI_NOR_CMD_READ;
	cmd[1] = (nor_offset >> 16) & 0xff;
	cmd[2] = (nor_offset >> 8) & 0xff;
	cmd[3] = nor_offset & 0xff;

	ret = spifc_chunk(cmd, NULL, 4, true);
	if (ret)
		goto out;

	/* read data in 64-byte chunks */
	while (done < len) {
		chunk = len - done;
		if (chunk > SPIFC_BUF_SIZE)
			chunk = SPIFC_BUF_SIZE;

		ret = spifc_chunk(NULL, p + done, chunk,
				  done + chunk < len);
		if (ret)
			goto out;

		done += chunk;
	}

out:
	/* re-enable AHB */
	spifc_set(REG_CTRL, CTRL_ENABLE_AHB);
	return ret;
}

/*
 * Scan env data for "overlays=" and copy the value to out_buf.
 * start is the first byte of env entries (after any header).
 * Entries are separated by \0 (binary/fw_setenv) or \n (text).
 * Returns length of value, or 0 if not found.
 */
static int env_scan_overlays(const char *start, u32 size,
			     char *out_buf, int out_size)
{
	const char *prefix = "overlays=";
	int pfxlen = 9;
	const char *p, *end, *val;
	int i;

	end = start + size;

	for (p = start; p + pfxlen < end; p++) {
		if (p != start && *(p - 1) != '\0' && *(p - 1) != '\n')
			continue;
		if (strncmp(p, prefix, pfxlen) != 0)
			continue;

		val = p + pfxlen;
		for (i = 0; i < out_size - 1 && val + i < end; i++) {
			if (val[i] == '\0' || val[i] == '\n' ||
			    (unsigned char)val[i] == 0xff)
				break;
			out_buf[i] = val[i];
		}
		out_buf[i] = '\0';
		return i;
	}

	return 0;
}

/*
 * Find "overlays=" in NOR env block.
 * Tries binary env format first (4-byte CRC32 + null-separated entries),
 * then falls back to raw text format (no header, newline-separated).
 */
static int nor_env_get_overlays(const char *data, u32 size,
				char *out_buf, int out_size)
{
	u32 env_crc, calc_crc;
	int ret;

	/* Try binary env: first 4 bytes are CRC32 over the rest */
	env_crc = le32_to_cpu(*(const u32 *)data);
	calc_crc = crc32(0, (const unsigned char *)data + 4, size - 4);
	if (env_crc == calc_crc) {
		ret = env_scan_overlays(data + 4, size - 4,
					out_buf, out_size);
		if (ret)
			return ret;
	}

	/* Fall back to raw text format (no CRC header) */
	return env_scan_overlays(data, size, out_buf, out_size);
}

/*
 * Main overlay logic called from fdtdec_board_setup().
 * fdt is the control FDT (gd->fdt_blob) — writable at this point since
 * reserve_fdt() hasn't run yet.
 */
static int nor_apply_overlays(void *fdt)
{
	void *scratch = (void *)(uintptr_t)SCRATCH_ADDR;
	char overlay_buf[256];
	const char *overlays;
	void *fit;
	u32 fit_size;
	const char *name, *next;
	int namelen;
	char path[64];
	int node;
	const void *dtbo_data;
	void *dtbo_copy;
	int dtbo_size;
	int fdt_size;
	int ret;

	/* Step 1: read env block from NOR */
	ret = spifc_raw_read(NOR_ENV_OFFSET, scratch, NOR_ENV_SIZE);
	if (ret) {
		printf("nor-overlay: SPIFC read env failed\n");
		return 0;
	}

	/* Step 2: find overlays= key (handles both binary and text env) */
	if (!nor_env_get_overlays(scratch, NOR_ENV_SIZE,
				  overlay_buf, sizeof(overlay_buf)))
		return 0;

	overlays = overlay_buf;
	printf("nor-overlay: overlays=%s\n", overlays);

	/* Step 4: read FIT header (64 bytes) to get total size */
	fit = scratch;
	ret = spifc_raw_read(NOR_FIT_OFFSET, fit, SPIFC_BUF_SIZE);
	if (ret) {
		printf("nor-overlay: SPIFC read FIT header failed\n");
		return 0;
	}

	if (fdt_magic(fit) != FDT_MAGIC) {
		printf("nor-overlay: invalid FIT magic\n");
		return 0;
	}

	fit_size = fdt_totalsize(fit);
	if (fit_size > NOR_FIT_MAX_SIZE) {
		printf("nor-overlay: FIT too large (%u)\n", fit_size);
		return 0;
	}

	/* Step 5: read full FIT into scratch */
	ret = spifc_raw_read(NOR_FIT_OFFSET, fit, fit_size);
	if (ret) {
		printf("nor-overlay: SPIFC read FIT failed\n");
		return 0;
	}

	/* Step 6: iterate overlay names (space-separated) */
	name = overlays;
	while (*name) {
		/* skip leading spaces */
		while (*name == ' ')
			name++;
		if (*name == '\0')
			break;

		/* find end of this name */
		next = name;
		while (*next && *next != ' ')
			next++;
		namelen = next - name;

		if (namelen == 0)
			break;

		/* build FIT path: /images/<name> */
		if (namelen + 9 > sizeof(path)) {
			printf("nor-overlay: name too long, skipping\n");
			name = next;
			continue;
		}
		memcpy(path, "/images/", 8);
		memcpy(path + 8, name, namelen);
		path[8 + namelen] = '\0';

		/* find node in FIT */
		node = fdt_path_offset(fit, path);
		if (node < 0) {
			printf("nor-overlay: %s not found in FIT\n", path);
			name = next;
			continue;
		}

		/* get DTBO data from FIT node (FIT is just an FDT) */
		dtbo_data = fdt_getprop(fit, node, "data", &dtbo_size);
		if (!dtbo_data || dtbo_size <= 0) {
			printf("nor-overlay: %s has no data\n", path);
			name = next;
			continue;
		}

		/* grow control FDT to accommodate overlay */
		fdt_size = fdt_totalsize(fdt);
		ret = fdt_open_into(fdt, fdt, fdt_size + dtbo_size + 4096);
		if (ret) {
			printf("nor-overlay: fdt_open_into failed: %s\n",
			       fdt_strerror(ret));
			name = next;
			continue;
		}

		/*
		 * fdt_overlay_apply() modifies the overlay in-place (resolves
		 * phandles), so we need a writable copy. Place it after the
		 * FIT in scratch memory.
		 */
		dtbo_copy = (u8 *)fit + ALIGN(fit_size, 64);
		memcpy(dtbo_copy, dtbo_data, dtbo_size);

		ret = fdt_overlay_apply_verbose(fdt, dtbo_copy);
		if (ret)
			printf("nor-overlay: failed to apply %s\n", path + 8);
		else
			printf("nor-overlay: applied %s\n", path + 8);

		name = next;
	}

	return 0;
}

int fdtdec_board_setup(const void *fdt_blob)
{
	return nor_apply_overlays((void *)fdt_blob);
}

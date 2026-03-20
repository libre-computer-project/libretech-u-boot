// SPDX-License-Identifier: GPL-2.0+
/*
 * NOR-based DT overlay and env loader for Meson boards
 *
 * Reads a U-Boot env block from SPI NOR at a fixed offset (NOR_ENV_OFFSET).
 * Two phases:
 *
 * 1. fdtdec_board_setup() (pre-DM): reads "overlays=" key and applies DT
 *    overlays from a FIT image in NOR before DM init.
 *
 * 2. nor_env_import() (post-env, called from misc_init_r): re-reads the
 *    NOR env block and imports ALL key=value pairs into the u-boot env
 *    via env_set(). This allows NOR config to override bootcmd, fdtfile,
 *    or any other env variable.
 *
 * Uses bare-metal SPIFC register access since phase 1 runs before DM is up.
 * Phase 2 reuses the same SPIFC code for consistency.
 * BL2 has already configured SPIFC clocks and pinmux for NOR boot.
 *
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <linux/libfdt.h>
#include <linux/types.h>
#include <asm/io.h>
#include <u-boot/crc.h>
#include <env.h>
#include <fdt_support.h>
#include <malloc.h>
#include <string.h>

/*
 * SoC-specific register bases.
 * GXL: CBUS @ 0xc1100000, HIU @ 0xc883c000
 * G12/SM1: CBUS @ 0xffd00000, HIU @ 0xff63c000
 */
#ifdef CONFIG_MESON_GXL
#define HIU_BASE		0xc883c000
#define SPIFC_BASE		0xc1108c80
#else /* G12A / G12B / SM1 */
#define HIU_BASE		0xff63c000
#define SPIFC_BASE		0xffd14000
#endif

/* HHI GCLK_MPEG0 -- SPIFC clock is bit 30. Offset 0x140 from HIU base. */
#define HHI_GCLK_MPEG0		(HIU_BASE + 0x140)

/* NOR layout */
#define NOR_ENV_OFFSET		0x1F0000
#define NOR_ENV_SIZE		0x10000		/* 64KB -- one erase block */
#define NOR_FIT_OFFSET		0x200000
#define NOR_FIT_MAX_SIZE	0x200000	/* 2MB max */

/* Scratch buffer in high DRAM -- safe, nothing uses this pre-DM */
#define SCRATCH_ADDR		0x0C000000

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
#define USER_CMP_MODE		BIT(2)

/* SPI NOR commands */
#define SPI_NOR_CMD_READ	0x03
#define SPI_NOR_CMD_RDID	0x9f

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
 * Reset and initialize SPIFC for user-mode transfers.
 * Mirrors meson_spifc_hw_init() from drivers/spi/meson_spifc.c.
 */
static bool spifc_init(void)
{
	int timeout = 10000;

	/* software reset */
	spifc_set(REG_SLAVE, SLAVE_SW_RST);

	/* wait for reset to clear -- if SPIFC clock is gated this spins */
	while (spifc_read(REG_SLAVE) & SLAVE_SW_RST) {
		if (--timeout <= 0)
			return false;
	}

	/* disable compatible mode */
	spifc_clr(REG_USER, USER_CMP_MODE);
	/* set master mode */
	spifc_clr(REG_SLAVE, SLAVE_OP_MODE);

	return true;
}

/*
 * Probe SPI NOR by reading JEDEC ID (command 0x9F).
 * Returns true if a valid flash is detected, false otherwise.
 */
static bool spifc_probe_jedec(void)
{
	u8 cmd = SPI_NOR_CMD_RDID;
	u8 id[3] = {0, 0, 0};
	int ret;

	/* disable AHB for user-mode access */
	spifc_clr(REG_CTRL, CTRL_ENABLE_AHB);

	/* send RDID command, keep CS asserted */
	ret = spifc_chunk(&cmd, NULL, 1, true);
	if (ret)
		goto out;

	/* read 3-byte JEDEC ID, release CS */
	ret = spifc_chunk(NULL, id, 3, false);

out:
	/* re-enable AHB */
	spifc_set(REG_CTRL, CTRL_ENABLE_AHB);

	if (ret)
		return false;

	/* 00,00,00 or FF,FF,FF means no flash responding */
	if ((id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00) ||
	    (id[0] == 0xff && id[1] == 0xff && id[2] == 0xff))
		return false;

	return true;
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

	/* Step 0: check SPIFC clock is enabled (BL2 skips this on USB boot) */
	if (!(readl(HHI_GCLK_MPEG0) & BIT(30)))
		return 0;

	/* Step 0a: initialize SPIFC and probe flash */
	if (!spifc_init())
		return 0;

	if (!spifc_probe_jedec())
		return 0;

	/* Step 1: read env block from NOR */
	ret = spifc_raw_read(NOR_ENV_OFFSET, scratch, NOR_ENV_SIZE);
	if (ret) {
		printf("nor-config: SPIFC read env failed\n");
		return 0;
	}

	/* Step 2: find overlays= key (handles both binary and text env) */
	if (!nor_env_get_overlays(scratch, NOR_ENV_SIZE,
				  overlay_buf, sizeof(overlay_buf))) {
		printf("nor-config: no overlays set\n");
		return 0;
	}

	overlays = overlay_buf;
	printf("nor-config: overlays=%s\n", overlays);

	/* Step 3: read FIT header (64 bytes) to get total size */
	fit = scratch;
	ret = spifc_raw_read(NOR_FIT_OFFSET, fit, SPIFC_BUF_SIZE);
	if (ret) {
		printf("nor-config: SPIFC read FIT header failed\n");
		return 0;
	}

	if (fdt_magic(fit) != FDT_MAGIC) {
		printf("nor-config: invalid FIT magic\n");
		return 0;
	}

	fit_size = fdt_totalsize(fit);
	if (fit_size > NOR_FIT_MAX_SIZE) {
		printf("nor-config: FIT too large (%u)\n", fit_size);
		return 0;
	}

	/* Step 4: read full FIT into scratch */
	ret = spifc_raw_read(NOR_FIT_OFFSET, fit, fit_size);
	if (ret) {
		printf("nor-config: SPIFC read FIT failed\n");
		return 0;
	}

	/* Step 5: iterate overlay names (space-separated) */
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
			printf("nor-config: name too long, skipping\n");
			name = next;
			continue;
		}
		memcpy(path, "/images/", 8);
		memcpy(path + 8, name, namelen);
		path[8 + namelen] = '\0';

		/* find node in FIT */
		node = fdt_path_offset(fit, path);
		if (node < 0) {
			printf("nor-config: %s not found in FIT\n", path);
			name = next;
			continue;
		}

		/* get DTBO data from FIT node (FIT is just an FDT) */
		dtbo_data = fdt_getprop(fit, node, "data", &dtbo_size);
		if (!dtbo_data || dtbo_size <= 0) {
			printf("nor-config: %s has no data\n", path);
			name = next;
			continue;
		}

		/* grow control FDT to accommodate overlay */
		fdt_size = fdt_totalsize(fdt);
		ret = fdt_open_into(fdt, fdt, fdt_size + dtbo_size + 4096);
		if (ret) {
			printf("nor-config: fdt_open_into failed: %s\n",
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
			printf("nor-config: failed to apply %s\n", path + 8);
		else
			printf("nor-config: applied %s\n", path + 8);

		name = next;
	}

	return 0;
}

/*
 * Import all key=value pairs from NOR env into u-boot env.
 * Called from misc_init_r (after env is loaded from FAT).
 * NOR env values override FAT env values.
 *
 * Supports both binary env (CRC32 + null-separated) and
 * plain text (newline-separated).
 */
static int nor_env_import_entries(const char *start, u32 size)
{
	const char *p, *end, *eq;
	char key[128], val[512];
	int klen, vlen;
	int count = 0;

	end = start + size;

	for (p = start; p < end; ) {
		/* skip to start of entry */
		if (*p == '\0' || *p == '\n' || (unsigned char)*p == 0xff) {
			/* double-null or 0xff block = end of entries */
			if (*p == '\0' && (p + 1 >= end || *(p + 1) == '\0'))
				break;
			if ((unsigned char)*p == 0xff)
				break;
			p++;
			continue;
		}

		/* find '=' separator */
		eq = NULL;
		for (eq = p; eq < end && *eq != '=' && *eq != '\0' &&
		     *eq != '\n' && (unsigned char)*eq != 0xff; eq++)
			;

		if (eq >= end || *eq != '=') {
			/* no '=' found, skip this entry */
			while (p < end && *p != '\0' && *p != '\n' &&
			       (unsigned char)*p != 0xff)
				p++;
			continue;
		}

		klen = eq - p;
		if (klen == 0 || klen >= sizeof(key)) {
			/* empty or too-long key, skip */
			p = eq + 1;
			while (p < end && *p != '\0' && *p != '\n' &&
			       (unsigned char)*p != 0xff)
				p++;
			continue;
		}

		memcpy(key, p, klen);
		key[klen] = '\0';

		/* extract value */
		p = eq + 1;
		vlen = 0;
		while (p + vlen < end && vlen < (int)sizeof(val) - 1 &&
		       *(p + vlen) != '\0' && *(p + vlen) != '\n' &&
		       (unsigned char)*(p + vlen) != 0xff)
			vlen++;

		memcpy(val, p, vlen);
		val[vlen] = '\0';

		env_set(key, val);
		printf("nor-env: %s=%s\n", key, val);
		count++;

		p += vlen;
	}

	return count;
}

int nor_env_import(void)
{
	char *buf;
	u32 env_crc, calc_crc;
	int count;

	/* check SPIFC clock gate */
	if (!(readl(HHI_GCLK_MPEG0) & BIT(30)))
		return 0;

	if (!spifc_init())
		return 0;

	if (!spifc_probe_jedec())
		return 0;

	buf = malloc(NOR_ENV_SIZE);
	if (!buf)
		return 0;

	if (spifc_raw_read(NOR_ENV_OFFSET, buf, NOR_ENV_SIZE)) {
		printf("nor-env: SPIFC read failed\n");
		free(buf);
		return 0;
	}

	/* try binary env format first */
	env_crc = le32_to_cpu(*(u32 *)buf);
	calc_crc = crc32(0, (unsigned char *)buf + 4, NOR_ENV_SIZE - 4);
	if (env_crc == calc_crc) {
		count = nor_env_import_entries(buf + 4, NOR_ENV_SIZE - 4);
	} else {
		/* fall back to plain text */
		count = nor_env_import_entries(buf, NOR_ENV_SIZE);
	}

	if (count)
		printf("nor-env: imported %d variables\n", count);

	free(buf);
	return count;
}

int fdtdec_board_setup(const void *fdt_blob)
{
	return nor_apply_overlays((void *)fdt_blob);
}

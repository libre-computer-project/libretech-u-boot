// SPDX-License-Identifier: GPL-2.0+
/*
 * NOR-based DT overlay loader -- generic framework
 *
 * Reads a U-Boot env block from SPI NOR at a fixed offset to find an
 * "overlays=" key listing DTBO names. Loads a FIT image from NOR containing
 * pre-compiled DTBOs, then applies each requested overlay to the control FDT
 * before DM init.
 *
 * Vendor-specific NOR read is provided by board_nor_init() and
 * board_nor_read() which each vendor backend implements.
 *
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <linux/libfdt.h>
#include <linux/types.h>
#include <u-boot/crc.h>
#include <env.h>
#include <fdt_support.h>
#include <malloc.h>
#include <string.h>

/* NOR layout */
#define NOR_ENV_OFFSET		0x1F0000
#define NOR_ENV_SIZE		0x10000		/* 64KB -- one erase block */
#define NOR_FIT_OFFSET		0x200000
#define NOR_FIT_MAX_SIZE	0x200000	/* 2MB max */

/* Scratch buffer in high DRAM -- safe, nothing uses this pre-DM */
#define SCRATCH_ADDR		0x20000000

/* FIT header read size */
#define FIT_HDR_SIZE		64

/*
 * Vendor backends must implement these functions:
 *   board_nor_clk_enabled() -- check if NOR controller clock is active
 *   board_nor_init() -- initialize NOR controller for raw reads
 *   board_nor_probe() -- probe for NOR flash presence (JEDEC ID)
 *   board_nor_read(offset, buf, len) -- read len bytes from NOR at offset
 */
extern bool board_nor_clk_enabled(void);
extern bool board_nor_init(void);
extern bool board_nor_probe(void);
extern int board_nor_read(u32 offset, void *buf, u32 len);

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
 * fdt is the control FDT (gd->fdt_blob) -- writable at this point since
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

	/* Step 0: check NOR controller is clocked, init, and probe flash */
	if (!board_nor_clk_enabled())
		return 0;

	if (!board_nor_init())
		return 0;

	if (!board_nor_probe())
		return 0;

	/* Step 1: read env block from NOR */
	ret = board_nor_read(NOR_ENV_OFFSET, scratch, NOR_ENV_SIZE);
	if (ret) {
		printf("nor-overlay: NOR read env failed\n");
		return 0;
	}

	/* Step 2: find overlays= key (handles both binary and text env) */
	if (!nor_env_get_overlays(scratch, NOR_ENV_SIZE,
				  overlay_buf, sizeof(overlay_buf))) {
		printf("nor-overlay: no overlays= found\n");
		return 0;
	}

	overlays = overlay_buf;
	printf("nor-overlay: overlays=%s\n", overlays);

	/* Step 3: read FIT header to get total size */
	fit = scratch;
	ret = board_nor_read(NOR_FIT_OFFSET, fit, FIT_HDR_SIZE);
	if (ret) {
		printf("nor-overlay: NOR read FIT header failed\n");
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

	/* Step 4: read full FIT into scratch */
	ret = board_nor_read(NOR_FIT_OFFSET, fit, fit_size);
	if (ret) {
		printf("nor-overlay: NOR read FIT failed\n");
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

/*
 * Import all key=value pairs from NOR env into u-boot env.
 * Called from misc_init_r (after env is loaded from FAT).
 * NOR env values override FAT env values.
 */
static int nor_env_import_entries(const char *start, u32 size)
{
	const char *p, *end, *eq;
	char key[128], val[512];
	int klen, vlen;
	int count = 0;

	end = start + size;

	for (p = start; p < end; ) {
		if (*p == '\0' || *p == '\n' || (unsigned char)*p == 0xff) {
			if (*p == '\0' && (p + 1 >= end || *(p + 1) == '\0'))
				break;
			if ((unsigned char)*p == 0xff)
				break;
			p++;
			continue;
		}

		eq = NULL;
		for (eq = p; eq < end && *eq != '=' && *eq != '\0' &&
		     *eq != '\n' && (unsigned char)*eq != 0xff; eq++)
			;

		if (eq >= end || *eq != '=') {
			while (p < end && *p != '\0' && *p != '\n' &&
			       (unsigned char)*p != 0xff)
				p++;
			continue;
		}

		klen = eq - p;
		if (klen == 0 || klen >= sizeof(key)) {
			p = eq + 1;
			while (p < end && *p != '\0' && *p != '\n' &&
			       (unsigned char)*p != 0xff)
				p++;
			continue;
		}

		memcpy(key, p, klen);
		key[klen] = '\0';

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

	if (!board_nor_clk_enabled())
		return 0;

	if (!board_nor_init())
		return 0;

	if (!board_nor_probe())
		return 0;

	buf = malloc(NOR_ENV_SIZE);
	if (!buf)
		return 0;

	if (board_nor_read(NOR_ENV_OFFSET, buf, NOR_ENV_SIZE)) {
		printf("nor-env: NOR read failed\n");
		free(buf);
		return 0;
	}

	env_crc = le32_to_cpu(*(u32 *)buf);
	calc_crc = crc32(0, (unsigned char *)buf + 4, NOR_ENV_SIZE - 4);
	if (env_crc == calc_crc)
		count = nor_env_import_entries(buf + 4, NOR_ENV_SIZE - 4);
	else
		count = nor_env_import_entries(buf, NOR_ENV_SIZE);

	if (count)
		printf("nor-env: imported %d variables\n", count);

	free(buf);
	return count;
}

int fdtdec_board_setup(const void *fdt_blob)
{
	return nor_apply_overlays((void *)fdt_blob);
}

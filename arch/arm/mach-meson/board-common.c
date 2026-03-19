// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Beniamino Galvani <b.galvani@gmail.com>
 */

#include <cpu_func.h>
#include <fastboot.h>
#include <init.h>
#include <net.h>
#include <asm/arch/boot.h>
#include <env.h>
#include <asm/cache.h>
#include <asm/global_data.h>
#include <asm/ptrace.h>
#include <linux/libfdt.h>
#include <linux/err.h>
#include <asm/arch/mem.h>
#include <asm/arch/sm.h>
#include <asm/armv8/mmu.h>
#include <asm/unaligned.h>
#include <efi_loader.h>
#include <u-boot/crc.h>

#include <asm/psci.h>

DECLARE_GLOBAL_DATA_PTR;

__weak int board_init(void)
{
	return 0;
}

int dram_init(void)
{
	const fdt64_t *val;
	int offset;
	int len;

	offset = fdt_path_offset(gd->fdt_blob, "/memory");
	if (offset < 0)
		return -EINVAL;

	val = fdt_getprop(gd->fdt_blob, offset, "reg", &len);
	if (len < sizeof(*val) * 2)
		return -EINVAL;

	/* Use unaligned access since cache is still disabled */
	gd->ram_size = get_unaligned_be64(&val[1]);

	return 0;
}

__weak int meson_ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}

static void meson_fixup_cma_size(void *blob)
{
	int rsv_node, node, cma_node;
	u64 cma_size, reserved = 0, available;
	fdt32_t cma_val[2];
	const fdt32_t *sizep;
	int len;

	cma_node = fdt_path_offset(blob, "/reserved-memory/linux,cma");
	if (cma_node < 0)
		return;

	/*
	 * Calculate available memory: total RAM minus all non-CMA
	 * reserved-memory regions. This gives 25% of what the kernel
	 * actually sees, avoiding CMA allocation failures on boards
	 * where secmon/hwrom reservations consume significant RAM.
	 *
	 * On 512MB boards, secmon takes ~53MB leaving ~460MB visible.
	 * 25% of 512MB = 128MB (fails due to fragmentation), but
	 * 25% of 460MB = 115MB (fits in available contiguous blocks).
	 */
	rsv_node = fdt_path_offset(blob, "/reserved-memory");
	if (rsv_node >= 0) {
		fdt_for_each_subnode(node, blob, rsv_node) {
			/* Skip the CMA node itself */
			if (node == cma_node)
				continue;
			sizep = fdt_getprop(blob, node, "size", &len);
			if (sizep && len >= 8)
				reserved += (u64)fdt32_to_cpu(sizep[0]) << 32 |
					    fdt32_to_cpu(sizep[1]);
		}
	}

	available = gd->ram_size > reserved ? gd->ram_size - reserved : 0;
	cma_size = (available / 4) & ~(4ULL * 1024 * 1024 - 1);

	cma_val[0] = cpu_to_fdt32(0);
	cma_val[1] = cpu_to_fdt32((u32)cma_size);

	if (fdt_setprop(blob, cma_node, "size", cma_val, sizeof(cma_val)))
		printf("meson: failed to set CMA size\n");
	else
		printf("meson: CMA %lluMB (avail %lluMB, rsv %lluMB, RAM %lluMB)\n",
		       cma_size >> 20, available >> 20,
		       reserved >> 20, gd->ram_size >> 20);
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	meson_init_reserved_memory(blob);
	meson_fixup_cma_size(blob);

	return meson_ft_board_setup(blob, bd);
}

void meson_board_add_reserved_memory(void *fdt, u64 start, u64 size)
{
	int ret;

	ret = fdt_add_mem_rsv(fdt, start, size);
	if (ret)
		printf("Could not reserve zone @ 0x%llx\n", start);

	if (IS_ENABLED(CONFIG_EFI_LOADER))
		efi_add_memory_map(start, size, EFI_RESERVED_MEMORY_TYPE);
}

int meson_generate_serial_ethaddr(void)
{
	u8 mac_addr[ARP_HLEN];
	char serial[SM_SERIAL_SIZE];
	u32 sid;
	u16 sid16;

	if (!meson_sm_get_serial(serial, SM_SERIAL_SIZE)) {
		sid = crc32(0, (unsigned char *)serial, SM_SERIAL_SIZE);
		sid16 = crc16_ccitt(0, (unsigned char *)serial,	SM_SERIAL_SIZE);

		/* Ensure the NIC specific bytes of the mac are not all 0 */
		if ((sid & 0xffffff) == 0)
			sid |= 0x800000;

		/* Non OUI / registered MAC address */
		mac_addr[0] = ((sid16 >> 8) & 0xfc) | 0x02;
		mac_addr[1] = (sid16 >>  0) & 0xff;
		mac_addr[2] = (sid >> 24) & 0xff;
		mac_addr[3] = (sid >> 16) & 0xff;
		mac_addr[4] = (sid >>  8) & 0xff;
		mac_addr[5] = (sid >>  0) & 0xff;

		eth_env_set_enetaddr("ethaddr", mac_addr);
	} else
		return -EINVAL;

	return 0;
}

static void meson_set_boot_source(void)
{
	const char *source;

	switch (meson_get_boot_device()) {
	case BOOT_DEVICE_EMMC:
		source = "emmc";
		break;

	case BOOT_DEVICE_NAND:
		source = "nand";
		break;

	case BOOT_DEVICE_SPI:
		source = "spi";
		break;

	case BOOT_DEVICE_SD:
		source = "sd";
		break;

	case BOOT_DEVICE_USB:
		source = "usb";
		break;

	default:
		source = "unknown";
	}

	env_set("boot_source", source);
}

__weak int meson_board_late_init(void)
{
	return 0;
}

int board_late_init(void)
{
	meson_set_boot_source();

	return meson_board_late_init();
}

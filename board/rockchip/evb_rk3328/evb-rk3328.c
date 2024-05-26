// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2016 Rockchip Electronics Co., Ltd
 */

#include <asm/io.h>

#define PRIORITY_HIGH_SHIFT 2
#define PRIORITY_LOW_SHIFT 0
#define VIO_VOP_QOS 0xff760080
#define CPU_AXI_QOS_PRIORITY 0x08

void rk_3328_qos_init(void){
	int val = 2 << PRIORITY_HIGH_SHIFT | 2 << PRIORITY_LOW_SHIFT;
	writel(val, CPU_AXI_QOS_PRIORITY + VIO_VOP_QOS);
}

int rk_board_late_init(void){
	rk_3328_qos_init();
	return 0;
}
#if IS_ENABLED(CONFIG_OF_BOARD_FIXUP)
int board_fix_fdt(void *blob)
{
	int node_offset = 0;

	node_offset = fdt_node_offset_by_compatible(blob, 0,
				"jedec,spi-nor");
	fdt_del_node(blob, node_offset);

	node_offset = fdt_node_offset_by_compatible(blob, 0,
				"rockchip,rk3328-spi");
	fdt_setprop(blob, node_offset, "status", "disabled", sizeof("disabled"));

	return 0;
}
#endif

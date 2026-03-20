// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <dm.h>
#include <dm/uclass.h>
#include <env.h>
#include <init.h>
#include <net.h>
#include <efi_loader.h>
#include <video.h>
#include <asm/io.h>
#include <asm/arch/eth.h>
#include "../common/board-meson.h"

struct efi_fw_image fw_images[] = {
	{
		.fw_name = u"AML_S905D3_CC_BOOT",
		.image_index = 1,
	},
	{
		.fw_name = u"AML_S905D3_CC_FIT",
		.image_index = 2,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "sf 0:0=u-boot-bin raw 0 0x1F0000;"
		      "sf 0:0=fit raw 0x200000 0xE00000",
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

#if IS_ENABLED(CONFIG_SET_DFU_ALT_INFO)
void set_dfu_alt_info(char *interface, char *devstr){
	meson_set_dfu_alt_info(interface, devstr);
}
#endif

int misc_init_r(void)
{
	struct udevice *dev;

	meson_generate_serial_ethaddr();
	nor_env_import();

	/* Probe all video devices -- overlay-added displays don't auto-probe */
	uclass_foreach_dev_probe(UCLASS_VIDEO, dev)
		;

	return 0;
}

// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2025 Da Xue <da@libre.computer>
 */

#include <dm.h>
#include <env.h>
#include <efi_loader.h>

struct efi_fw_image fw_images[] = {
	{
		.fw_name = u"ROC_RK3328_CC_BOOT",
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "mmc 1=u-boot-bin raw 0x40 0x2000 mmcpart 0",
	.num_images = ARRAY_SIZE(fw_images),
	.images = fw_images,
};

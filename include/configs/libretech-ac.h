/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for LibreTech AC
 *
 * Copyright (C) 2017 Baylibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#ifndef __CONFIG_H
#define __CONFIG_H

#define BOOT_TARGET_DEVICES(func) \
	func(ROMUSB, romusb, na)  \
	func(MMC, mmc, 0) \
	BOOT_TARGET_DEVICES_USB(func) \
	func(PXE, pxe, na) \
	func(DHCP, dhcp, na)

#define CONFIG_EXTRA_ENV_SETTINGS \
        "stdin=" STDIN_CFG "\0" \
        "stdout=" STDOUT_CFG "\0" \
        "stderr=" STDOUT_CFG "\0" \
        "fdt_addr_r=0x08008000\0" \
        "scriptaddr=0x08000000\0" \
        "kernel_addr_r=0x08080000\0" \
        "pxefile_addr_r=0x01080000\0" \
        "ramdisk_addr_r=0x13000000\0" \
        "lc_fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
        "fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
        BOOTENV

#include <configs/meson64.h>

#endif /* __CONFIG_H */

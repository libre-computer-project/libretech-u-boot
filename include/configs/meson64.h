/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Configuration for Amlogic Meson 64bits SoCs
 * (C) Copyright 2016 Beniamino Galvani <b.galvani@gmail.com>
 */

#ifndef __MESON64_CONFIG_H
#define __MESON64_CONFIG_H

/* Generic Interrupt Controller Definitions */
#if (defined(CONFIG_MESON_AXG) || defined(CONFIG_MESON_G12A))
#define GICD_BASE			0xffc01000
#define GICC_BASE			0xffc02000
#elif defined(CONFIG_MESON_A1)
#define GICD_BASE			0xff901000
#define GICC_BASE			0xff902000
#else /* MESON GXL and GXBB */
#define GICD_BASE			0xc4301000
#define GICC_BASE			0xc4302000
#endif

/* Serial drivers */
/* The following table includes the supported baudrates */
#define CFG_SYS_BAUDRATE_TABLE  \
	{300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, \
		230400, 250000, 460800, 500000, 1000000, 2000000, 4000000, \
		8000000 }

/* For splashscreen */
#ifdef CONFIG_VIDEO
#define STDOUT_CFG "vidconsole,serial"
#else
#define STDOUT_CFG "serial"
#endif

#ifdef CONFIG_USB_KEYBOARD
#define STDIN_CFG "usbkbd,serial"
#else
#define STDIN_CFG "serial"
#endif

#define CFG_SYS_SDRAM_BASE		0

#define BOOTM_SIZE		__stringify(0x1700000)
#define KERNEL_ADDR_R		__stringify(0x08080000)
#define KERNEL_COMP_ADDR_R	__stringify(0x0d080000)
#define FDT_ADDR_R		__stringify(0x08008000)
#define SCRIPT_ADDR_R		__stringify(0x08000000)
#define PXEFILE_ADDR_R		__stringify(0x01080000)
#define FDTOVERLAY_ADDR_R	__stringify(0x01000000)
#define RAMDISK_ADDR_R		__stringify(0x13000000)

#ifndef MESON_DEVICE_SETTINGS
#define MESON_DEVICE_SETTINGS
#endif

#ifndef BOOT_TARGETS
#define BOOT_TARGETS "mmc0 mmc1 nvme scsi usb pxe dhcp spi"
#endif

#ifndef CFG_EXTRA_ENV_SETTINGS
#define CFG_EXTRA_ENV_SETTINGS \
	"stdin=" STDIN_CFG "\0" \
	"stdout=" STDOUT_CFG "\0" \
	"stderr=" STDOUT_CFG "\0" \
	"kernel_comp_addr_r=" KERNEL_COMP_ADDR_R "\0" \
	"kernel_comp_size=0x2000000\0" \
	"fdt_addr_r=" FDT_ADDR_R "\0" \
	"scriptaddr=" SCRIPT_ADDR_R "\0" \
	"kernel_addr_r=" KERNEL_ADDR_R "\0" \
	"pxefile_addr_r=" PXEFILE_ADDR_R "\0" \
	"fdtoverlay_addr_r=" FDTOVERLAY_ADDR_R "\0" \
	"ramdisk_addr_r=" RAMDISK_ADDR_R "\0" \
	"fdtfile=amlogic/" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	"dfu_alt_info=fitimage ram " KERNEL_ADDR_R " 0x4000000 \0" \
	"boot_targets=" BOOT_TARGETS "\0" \
	MESON_DEVICE_SETTINGS
#endif


#endif /* __MESON64_CONFIG_H */

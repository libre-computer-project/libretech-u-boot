# SPDX-License-Identifier: GPL-2.0+
#
# (C) Copyright 2004-2006
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.

obj-$(CONFIG_$(PHASE_)ENV_SUPPORT) += common.o
obj-$(CONFIG_$(PHASE_)ENV_SUPPORT) += env.o
obj-$(CONFIG_$(PHASE_)ENV_SUPPORT) += attr.o
obj-$(CONFIG_$(PHASE_)ENV_SUPPORT) += flags.o

ifndef CONFIG_XPL_BUILD
obj-y += callback.o
obj-$(CONFIG_ENV_IS_IN_EEPROM) += eeprom.o
obj-$(CONFIG_ENV_IS_IN_EEPROM) += embedded.o
extra-$(CONFIG_ENV_IS_IN_FLASH) += embedded.o
obj-$(CONFIG_ENV_IS_IN_NVRAM) += embedded.o
obj-$(CONFIG_ENV_IS_IN_NVRAM) += nvram.o
obj-$(CONFIG_ENV_IS_IN_ONENAND) += onenand.o
obj-$(CONFIG_ENV_IS_IN_REMOTE) += remote.o
obj-$(CONFIG_ENV_IS_IN_UBI) += ubi.o
endif

obj-$(CONFIG_$(PHASE_)ENV_IS_NOWHERE) += nowhere.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_MMC) += mmc.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_FAT) += fat.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_EXT4) += ext4.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_NAND) += nand.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_SPI_FLASH) += sf.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_MTD) += mtd.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_FLASH) += flash.o
obj-$(CONFIG_$(PHASE_)ENV_IS_IN_SCSI) += scsi.o

CFLAGS_embedded.o := -Wa,--no-warn -DENV_CRC=$(shell tools/envcrc 2>/dev/null)

# SPDX-License-Identifier: GPL-2.0+
ifeq ($(CONFIG_XPL_BUILD),y)
# SPL: create empty built-in.a, do not compile board code
obj- := __dummy__.o
else
obj-y	:= board.o
obj-$(CONFIG_ARCH_MESON)	+= board-meson.o
obj-$(CONFIG_ARCH_MESON)	+= nor-config.o meson-spifc-config.o
obj-$(CONFIG_ARCH_SUNXI)	+= board-sunxi.o
obj-$(CONFIG_ARCH_ROCKCHIP)	+= board-rockchip.o
endif

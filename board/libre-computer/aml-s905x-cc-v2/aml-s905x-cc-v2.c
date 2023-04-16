// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <init.h>
#include <net.h>
#include <asm/io.h>
#include <asm/arch/gx.h>
#include <asm/arch/sm.h>
#include <asm/arch/eth.h>
#include <asm/arch/mem.h>
#include <splash.h>

#define EFUSE_SN_OFFSET		20
#define EFUSE_SN_SIZE		16
#define EFUSE_MAC_OFFSET	52
#define EFUSE_MAC_SIZE		6

#define ETH_REG_BASE	0xc8834540
#define ETH_REG_0		0x4
#define ETH_REG_2		0x18
#define GX_ETH_REG_0_INVERT_RMII_CLK    BIT(11)
#define GX_ETH_REG_0_CLK_EN             BIT(12)

int misc_init_r(void)
{
	u8 mac_addr[EFUSE_MAC_SIZE];
	char serial[EFUSE_SN_SIZE];
	ssize_t len;

	if (!eth_env_get_enetaddr("ethaddr", mac_addr)) {
		len = meson_sm_read_efuse(EFUSE_MAC_OFFSET,
					  mac_addr, EFUSE_MAC_SIZE);
		if (len == EFUSE_MAC_SIZE && is_valid_ethaddr(mac_addr))
			eth_env_set_enetaddr("ethaddr", mac_addr);
		else
			meson_generate_serial_ethaddr();

		if (!IS_ENABLED(CONFIG_ETH_DESIGNWARE_MESON8B)){
			printf("Net:   Fast Ethernet\n");
			out_le32(ETH_REG_BASE + ETH_REG_0, GX_ETH_REG_0_INVERT_RMII_CLK | GX_ETH_REG_0_CLK_EN);
			writel(0x10110181, ETH_REG_BASE + ETH_REG_2);
		}

	}

	if (!env_get("serial#")) {
		len = meson_sm_read_efuse(EFUSE_SN_OFFSET, serial,
			EFUSE_SN_SIZE);
		if (len == EFUSE_SN_SIZE)
			env_set("serial#", serial);
	}

	return 0;
}

#ifdef CONFIG_SPLASH_SCREEN
static struct splash_location splash_locations[] = {
        {
                .name = "mmc_fs",
                .storage = SPLASH_STORAGE_MMC,
                .flags = SPLASH_STORAGE_FS,
                .devpart = "0:auto",
        }
};

int splash_screen_prepare(void)
{
        if (CONFIG_IS_ENABLED(SPLASH_SOURCE))
                return splash_source_load(splash_locations,
                        ARRAY_SIZE(splash_locations)) && splash_video_logo_load();
        return splash_video_logo_load();
}
#endif

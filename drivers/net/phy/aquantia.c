/* aquantia.c: Aquantia phy driver.
 *
 * Copyright (c) 2012 AnnapurnaLabs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/phy.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/of.h>
#include <linux/mii.h>
#include <linux/mdio.h>

#if defined(CONFIG_MACH_QNAPTS)
#define PHY_ID_AQR107   0x03a1b4e1
#define AQUANTIA_AQR107_ID   PHY_ID_AQR107
#define PHY_AQUANTIA_FEATURES   (SUPPORTED_10000baseT_Full | \
                                 SUPPORTED_1000baseT_Full | \
                                 SUPPORTED_100baseT_Full | \
                                 PHY_DEFAULT_FEATURES)
#else
#define AQUANTIA_AQR105_ID			0x3a1b4a2
#endif



#define AQ_PHY_ADDR(device, reg) (MII_ADDR_C45 | (device * 0x10000) | reg)

#define AQ_AN_VENDOR_STATUS			AQ_PHY_ADDR(0x7, 0xc800)
#define AQ_AN_VENDOR_STATUS_SPEED_MASK		0xe
#define AQ_AN_VENDOR_STATUS_DUPLEX_MASK		0x1

#define AQ_STATUS_SPEED_10000			0x6
#define AQ_STATUS_SPEED_5000			0xa
#define AQ_STATUS_SPEED_2500			0x8
#define AQ_STATUS_SPEED_1000			0x4
#define AQ_STATUS_SPEED_100			0x3

#define AQ_CONNECTION_STATUS			AQ_PHY_ADDR(0x7, 0xc810)
#define AQ_CONNECTION_STATUS_LINK_MASK		0x3e00
#define AQ_CONNECTION_STATUS_LINK_UP		0x800


static int aquantia_update_link_led(struct phy_device *phydev)
{
    u32 led0_val = 0;
    u32 led1_val = 0;
    u16 reg = 0;
    led0_val = phy_read(phydev, AQ_PHY_ADDR(0x1e, 0xc430));
    led1_val = phy_read(phydev, AQ_PHY_ADDR(0x1e, 0xc431));
//    printk("=== QNAP Check: %s(%d): BF update link led: led0 0x%x, led1 0x%x\n", __func__, __LINE__, led0_val, led1_val);
    led0_val &= ~(0x7f<<0x2);
    led1_val &= ~(0x7f<<0x2);
    switch (phydev->speed) {
    case SPEED_1000:
        led0_val |= (0x1<<0x6);
        break;
    case SPEED_10000:
        led1_val |= (0x1<<0x6);
        break;
    default:
        return -EINVAL;
    }
//    printk("=== QNAP Check: %s(%d): AF update link led: led0 0x%x, led1 0x%x\n", __func__, __LINE__, led0_val, led1_val);
    reg = led0_val &0xffff;
    if(phy_write(phydev, AQ_PHY_ADDR(0x1e, 0xc430), reg))
        return -1;
    reg = led1_val &0xffff;
    if(phy_write(phydev, AQ_PHY_ADDR(0x1e, 0xc431), reg))
        return -1;

//    printk("=== QNAP Check: %s(%d): update link led & set speed  %s\n", __func__, __LINE__, 
//        phydev->speed==SPEED_1000?"SPEED_1000":phydev->speed==SPEED_100?"SPEED_100":"SPEED_UNKNOW");
}


static int aquantia_config_aneg(struct phy_device *phydev)
{
    int retval = -1;
    u16 reg;
    u16 speed;
    phydev->supported = PHY_AQUANTIA_FEATURES;
    phydev->advertising = phydev->supported;

    return 0;
}




#if defined(CONFIG_MACH_QNAPTS)
static int aquantia_aneg_done(struct phy_device *phydev)
{
    int reg;

    reg = phy_read(phydev, AQ_PHY_ADDR(MDIO_MMD_AN, MDIO_STAT1));
    return (reg < 0) ? reg : (reg & BMSR_ANEGCOMPLETE);
}

#endif

static int aq_config_init(struct phy_device *phydev)
{
#if defined(CONFIG_MACH_QNAPTS)
    phydev->supported = PHY_AQUANTIA_FEATURES;
    phydev->advertising = phydev->supported;
#else
	phydev->supported = ADVERTISED_1000baseT_Full;
	phydev->advertising = ADVERTISED_1000baseT_Full;
#endif
	phydev->state = PHY_NOLINK;
	phydev->autoneg = AUTONEG_ENABLE;

	return 0;
}

static int aq_read_status(struct phy_device *phydev)
{
	int connection_status;
	int an_vendor_status;

	connection_status = phy_read(phydev, AQ_CONNECTION_STATUS);
	if ((connection_status & AQ_CONNECTION_STATUS_LINK_MASK) != AQ_CONNECTION_STATUS_LINK_UP)
		goto no_link;

	an_vendor_status = phy_read(phydev, AQ_AN_VENDOR_STATUS);

	switch (an_vendor_status & AQ_AN_VENDOR_STATUS_SPEED_MASK) {
	case AQ_STATUS_SPEED_10000:
		phydev->speed = SPEED_10000;
		break;
	case AQ_STATUS_SPEED_5000:
		phydev->speed = SPEED_5000;
		break;
	case AQ_STATUS_SPEED_2500:
		phydev->speed = SPEED_2500;
		break;
	case AQ_STATUS_SPEED_1000:
		phydev->speed = SPEED_1000;
		break;
	case AQ_STATUS_SPEED_100:
		phydev->speed = SPEED_100;
		break;
	default:
		phydev->speed = SPEED_10;
	}
//printk("=== QNAP check: %s(%d): phydev->speed = 0x%x\n", __func__, __LINE__, phydev->speed);
	if ((an_vendor_status & AQ_AN_VENDOR_STATUS_DUPLEX_MASK) != 0) {
		phydev->duplex = DUPLEX_FULL;
	} else {
		phydev->duplex = DUPLEX_HALF;
	}

    aquantia_update_link_led(phydev);
	phydev->link = 1;
	return 0;

no_link:
	phydev->link = 0;
	return 0;
}

static int aq_match_phy_device(struct phy_device *phydev)
{
	if (phydev->c45_ids.device_ids[4] == AQUANTIA_AQR107_ID)
		return 1;

	return 0;
}

static struct phy_driver aq_driver[] = {
{
	.phy_id		= AQUANTIA_AQR107_ID,
	.phy_id_mask	= 0xffffffff,
	.name		= "Aquantia AQR107 phy driver",
	.flags		= PHY_HAS_INTERRUPT,
	.config_init	= aq_config_init,
	.read_status	= aq_read_status,
	.match_phy_device = aq_match_phy_device,
#if defined(CONFIG_MACH_QNAPTS)
    .config_aneg    = aquantia_config_aneg,
    .aneg_done  = aquantia_aneg_done,
#endif
	.driver		= { .owner = THIS_MODULE },
}};

static int __init aq_init(void)
{
	return phy_drivers_register(aq_driver,
		ARRAY_SIZE(aq_driver));
}
module_init(aq_init);

static void __exit aq_exit(void)
{
	phy_drivers_unregister(aq_driver,
		ARRAY_SIZE(aq_driver));
}
module_exit(aq_exit);

MODULE_LICENSE("GPL");

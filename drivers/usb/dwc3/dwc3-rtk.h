/**
 * dwc3-rtk.h - Realtek DWC3 Specific Glue layer
 *
 * Copyright (C) 2020 Realtek Semiconductor Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __DRIVERS_USB_DWC3_RTK_H
#define __DRIVERS_USB_DWC3_RTK_H

#define WRAP_CTR_reg  0x0
#define DISABLE_MULTI_REQ BIT(1)

#define WRAP_USB_HMAC_CTR0_reg 0x60
#define U3PORT_DIS BIT(8)

#define WRAP_USB2_PHY_reg  0x70
#define USB2_PHY_EN_PHY_PLL_PORT0 BIT(12)
#define USB2_PHY_EN_PHY_PLL_PORT1 BIT(13)
#define USB2_PHY_SWITCH_MASK 0x707
#define USB2_PHY_SWITCH_DEVICE 0x0
#define USB2_PHY_SWITCH_HOST 0x606

struct dwc3_rtk {
	struct platform_device	*usb2_phy;
	struct platform_device	*usb3_phy;
	struct device		*dev;

	void __iomem		*regs;
	size_t		regs_size;

	struct clk		*clk;
	struct dwc3 *dwc;

	struct work_struct work;

	int default_dwc3_dr_mode; /* define by dwc3 driver, and it is fixed */
	int cur_dr_mode; /* current dr mode */
	bool support_drd_mode; /* if support Host/device switch */
	bool support_type_c;

	/* For debugfs */
	struct dentry		*debug_dir;
};

void dwc3_rtk_debugfs_init(struct dwc3_rtk *dwc3_rtk);
void dwc3_rtk_debugfs_exit(struct dwc3_rtk *dwc3_rtk);

#endif /* __DRIVERS_USB_DWC3_RTK_H */

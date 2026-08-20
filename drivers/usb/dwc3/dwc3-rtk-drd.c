/**
 * dwc3-rtk-drd.c - Realtek DWC3 Specific Glue layer
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/composite.h>

#include "core.h"
#include "gadget.h"
#include "io.h"
#include "dwc3-rtk-drd.h"

static int dwc3_check_drd_mode(struct dwc3 *dwc)
{
	int mode = USB_DR_MODE_UNKNOWN;

	if (dwc->xhci) {
		mode = USB_DR_MODE_HOST;
		dev_dbg(dwc->dev, "%s Now is host\n", __func__);
	} else if (dwc->gadget.udc) {
		mode = USB_DR_MODE_PERIPHERAL;
		dev_dbg(dwc->dev, "%s Now is gadget\n", __func__);
	}

	return mode;
}

static int rtk_dwc3_drd_core_soft_reset(struct dwc3 *dwc)
{
	int ret;
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg | DWC3_GCTL_DSBLCLKGTNG);

	ret = dwc3_core_soft_reset(dwc);

	dwc3_writel(dwc->regs, DWC3_GCTL, reg);

	return ret;
}

static int rtk_dwc3_drd_event_buffers_setup(struct dwc3 *dwc)
{
	struct dwc3_event_buffer	*evt;

	evt = dwc->ev_buf;
	evt->lpos = 0;
	dwc3_writel(dwc->regs, DWC3_GEVNTADRLO(0),
			lower_32_bits(evt->dma));
	dwc3_writel(dwc->regs, DWC3_GEVNTADRHI(0),
			upper_32_bits(evt->dma));
	dwc3_writel(dwc->regs, DWC3_GEVNTSIZ(0),
			DWC3_GEVNTSIZ_SIZE(evt->length));
	dwc3_writel(dwc->regs, DWC3_GEVNTCOUNT(0), 0);

	return 0;
}

__maybe_unused static void rtk_dwc3_set_mode(struct dwc3 *dwc, u32 mode)
{
	u32 reg;

	reg = dwc3_readl(dwc->regs, DWC3_GCTL);
	reg &= ~(DWC3_GCTL_PRTCAPDIR(DWC3_GCTL_PRTCAP_OTG));
	reg |= DWC3_GCTL_PRTCAPDIR(mode);
	dwc3_writel(dwc->regs, DWC3_GCTL, reg);
}

struct usb_gadget_driver *rtk_dwc3_set_and_get_usb_gadget_driver(
	    struct usb_gadget_driver *driver)
{
	static struct usb_gadget_driver *gadget_driver;
	struct usb_gadget_driver *local_driver = gadget_driver;

	gadget_driver = driver;

	return local_driver;
}
EXPORT_SYMBOL_GPL(rtk_dwc3_set_and_get_usb_gadget_driver);

int dwc3_drd_to_host(struct dwc3 *dwc)
{
	int ret;
	unsigned long timeout;
	//unsigned long flags = 0;
	u32 reg;

	dev_info(dwc->dev, "%s START....", __func__);
	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_PERIPHERAL) {
		if (dwc->gadget_driver != NULL) {
			rtk_dwc3_set_and_get_usb_gadget_driver(
				    dwc->gadget_driver);
			usb_gadget_unregister_driver(dwc->gadget_driver);
		}
		dwc3_gadget_exit(dwc);
	}
	/* Do wmb */
	wmb();

	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_HOST) {
		dev_info(dwc->dev, "%s Now is host", __func__);
		return 0;
	}

	/* issue device SoftReset too */
	timeout = jiffies + msecs_to_jiffies(500);
	dwc3_writel(dwc->regs, DWC3_DCTL, DWC3_DCTL_CSFTRST);
	do {
		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		if (!(reg & DWC3_DCTL_CSFTRST))
			break;

		if (time_after(jiffies, timeout)) {
			dev_err(dwc->dev, "Reset Timed Out\n");
			ret = -ETIMEDOUT;
			goto err0;
		}

		cpu_relax();
	} while (true);

	dev_info(dwc->dev, "%s: call dwc3_core_soft_reset\n", __func__);
	ret = rtk_dwc3_drd_core_soft_reset(dwc);
	if (ret) {
		dev_err(dwc->dev, "soft reset failed\n");
		goto err0;
	}

	ret = rtk_dwc3_drd_event_buffers_setup(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to setup event buffers\n");
		goto err0;
	}

	dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_HOST);

	ret = dwc3_host_init(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to init host\n");
		goto err0;
	}
err0:
	dev_info(dwc->dev, "%s END....", __func__);
	return ret;
}

int dwc3_drd_to_device(struct dwc3 *dwc)
{
	int ret;
	unsigned long timeout, flags = 0;
	u32 reg;

	dev_info(dwc->dev, "%s START....", __func__);

	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_HOST) {
		dev_info(dwc->dev, "%s dwc3_host_exit", __func__);
		dwc3_host_exit(dwc);
	}
	/* Do wmb */
	wmb();

	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_PERIPHERAL) {
		dev_info(dwc->dev, "%s Now is gadget", __func__);
		return 0;
	}

	/* issue device SoftReset too */
	timeout = jiffies + msecs_to_jiffies(500);
	dwc3_writel(dwc->regs, DWC3_DCTL, DWC3_DCTL_CSFTRST);
	do {
		reg = dwc3_readl(dwc->regs, DWC3_DCTL);
		if (!(reg & DWC3_DCTL_CSFTRST))
			break;

		if (time_after(jiffies, timeout)) {
			dev_err(dwc->dev, "Reset Timed Out\n");
			ret = -ETIMEDOUT;
			goto err0;
		}

		cpu_relax();
	} while (true);

	ret = rtk_dwc3_drd_core_soft_reset(dwc);
	if (ret) {
		dev_err(dwc->dev, "soft reset failed\n");
		goto err0;
	}

	spin_lock_irqsave(&dwc->lock, flags);

	ret = rtk_dwc3_drd_event_buffers_setup(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to setup event buffers\n");
		spin_unlock_irqrestore(&dwc->lock, flags);
		goto err0;
	}

	dwc3_set_mode(dwc, DWC3_GCTL_PRTCAP_DEVICE);

	spin_unlock_irqrestore(&dwc->lock, flags);

	ret = dwc3_gadget_init(dwc);
	if (ret) {
		dev_err(dwc->dev, "failed to init gadget\n");
		goto err0;
	} else {
		struct usb_gadget_driver *driver =
			    rtk_dwc3_set_and_get_usb_gadget_driver(NULL);
		if (driver)
			ret = usb_gadget_probe_driver(driver);
	}
	if (ret) {
		dev_err(dwc->dev, "failed to usb_gadget probe gadget_driver\n");
		goto err0;
	}
err0:
	dev_info(dwc->dev, "%s END....", __func__);
	return ret;
}

int dwc3_drd_to_stop_all(struct dwc3 *dwc)
{
	int ret = 0;

	dev_info(dwc->dev, "%s START....", __func__);
	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_HOST)
		dwc3_host_exit(dwc);
	if (dwc3_check_drd_mode(dwc) == USB_DR_MODE_PERIPHERAL) {
		if (dwc->gadget_driver != NULL) {
			rtk_dwc3_set_and_get_usb_gadget_driver(
				    dwc->gadget_driver);
			usb_gadget_unregister_driver(dwc->gadget_driver);
		}
		dwc3_gadget_exit(dwc);
	}
	/* Do wmb */
	wmb();
	dev_info(dwc->dev, "%s END....", __func__);
	return ret;
}

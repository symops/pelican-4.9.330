/**
 * dwc3-rtk.c - Realtek DWC3 Specific Glue layer
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/usb/otg.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/usb/of.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/suspend.h>
#include <soc/realtek/rtk_chip.h>
#include <soc/realtek/rtk-usb-manager.h>

#include "dwc3-rtk.h"
#include "dwc3-rtk-drd.h"
#include "core.h"
#include "io.h"

#ifdef CONFIG_USB_PATCH_ON_RTK
#ifdef CONFIG_RTK_USB3PHY
extern void rtk_usb3_phy_toggle(struct usb_phy *usb3_phy, bool isConnect,
	    int port);
#endif

void RTK_dwc3_usb3_phy_toggle(struct device *hcd_dev, bool isConnect, int port)
{
	struct device *dwc3_dev = NULL;
	struct dwc3 *dwc = NULL;

	if (hcd_dev == NULL)
		return;

	dwc3_dev = hcd_dev->parent;
	if (dwc3_dev == NULL)
		return;

	dwc = dev_get_drvdata(dwc3_dev);
#ifdef CONFIG_RTK_USB3PHY
	dev_dbg(dwc3_dev, "%s port=%d\n", __func__, port);
	if (dwc != NULL)
		rtk_usb3_phy_toggle(dwc->usb3_phy, isConnect, port);
#endif
}

#ifdef CONFIG_RTK_USB2PHY
extern void rtk_usb2_phy_toggle(struct usb_phy *usb3_phy, bool isConnect,
	    int port);
#endif

int RTK_dwc3_usb2_phy_toggle(struct device *hcd_dev, bool isConnect, int port)
{
	struct device *dwc3_dev = NULL;
	struct dwc3 *dwc = NULL;

	if (hcd_dev == NULL)
		return -1;

	dwc3_dev = hcd_dev->parent;
	if (dwc3_dev == NULL)
		return -1;

	dwc = dev_get_drvdata(dwc3_dev);
	if (dwc == NULL)
		return -1;

#ifdef CONFIG_RTK_USB2PHY
	dev_dbg(dwc3_dev, "%s port=%d\n", __func__, port);
	rtk_usb2_phy_toggle(dwc->usb2_phy, isConnect, port);
#endif
	return 0;
}
#endif // CONFIG_USB_PATCH_ON_RTK

static int dwc3_rtk_register_phys(struct dwc3_rtk *rtk)
{
	struct usb_phy_generic_platform_data pdata;
	struct platform_device	*pdev;
	int			ret;

	memset(&pdata, 0x00, sizeof(pdata));

	pdev = platform_device_alloc("usb_phy_generic", PLATFORM_DEVID_AUTO);
	if (!pdev)
		return -ENOMEM;

	rtk->usb2_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB2;

	ret = platform_device_add_data(rtk->usb2_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err1;

	pdev = platform_device_alloc("usb_phy_generic", PLATFORM_DEVID_AUTO);
	if (!pdev) {
		ret = -ENOMEM;
		goto err1;
	}

	rtk->usb3_phy = pdev;
	pdata.type = USB_PHY_TYPE_USB3;

	ret = platform_device_add_data(rtk->usb3_phy, &pdata, sizeof(pdata));
	if (ret)
		goto err2;

	ret = platform_device_add(rtk->usb2_phy);
	if (ret)
		goto err2;

	ret = platform_device_add(rtk->usb3_phy);
	if (ret)
		goto err3;

	return 0;

err3:
	platform_device_del(rtk->usb2_phy);

err2:
	platform_device_put(rtk->usb3_phy);

err1:
	platform_device_put(rtk->usb2_phy);

	return ret;
}

/*
static int dwc3_rtk_remove_child(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}
*/

bool dwc3_rtk_is_support_drd_mode(struct dwc3_rtk *dwc3_rtk)
{
	if (!dwc3_rtk) {
		pr_err("%s: ERROR: dwc3_rtk is NULL!", __func__);
		return false;
	}

	return dwc3_rtk->support_drd_mode;
}

bool dwc3_rtk_is_connected_on_device_mode(struct dwc3_rtk *dwc3_rtk)
{
	bool connected = true;
	int no_host_connect = 0;
	int no_run_gadget = 0;
	u32 dsts, dctl;

	if (!dwc3_rtk) {
		pr_err("%s: ERROR: dwc3_rtk is NULL!", __func__);
		return connected;
	}
	if (dwc3_rtk->cur_dr_mode != USB_DR_MODE_PERIPHERAL) {
		dev_info(dwc3_rtk->dev,
			    "%s: Error: not in device mode (cur_dr_mode=%x)\n",
			    __func__, dwc3_rtk->cur_dr_mode);
		return connected;
	}

	dsts = dwc3_readl(dwc3_rtk->dwc->regs, DWC3_DSTS);
	dctl = dwc3_readl(dwc3_rtk->dwc->regs, DWC3_DCTL);

	dev_info(dwc3_rtk->dev, "%s: Device mode check DSTS=%x DCTL=%x\n",
		    __func__,
		    dsts, dctl);
	no_host_connect = DWC3_DSTS_USBLNKST(dsts) >= DWC3_LINK_STATE_SS_DIS;
	no_host_connect = no_host_connect | ((dsts & 0x0003FFF8) == BIT(17));
	no_run_gadget = (dctl & BIT(31)) == 0x0;
	if (no_host_connect || no_run_gadget)
		connected = false;

	return connected;
}

static void switch_u2_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
	switch (dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		writel(USB2_PHY_SWITCH_DEVICE |
			    (~USB2_PHY_SWITCH_MASK &
			      readl(rtk->regs + WRAP_USB2_PHY_reg)),
			    rtk->regs + WRAP_USB2_PHY_reg);
		break;
	case USB_DR_MODE_HOST:
		writel(USB2_PHY_SWITCH_HOST |
			    (~USB2_PHY_SWITCH_MASK &
			      readl(rtk->regs + WRAP_USB2_PHY_reg)),
			    rtk->regs + WRAP_USB2_PHY_reg);
		break;
	case USB_DR_MODE_OTG:
		//writel(BIT(11) , rtk->regs + WRAP_USB2_PHY_reg);
		dev_info(rtk->dev, "%s: USB_DR_MODE_OTG\n", __func__);
		break;
	}
}

static void switch_dwc3_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
#ifdef CONFIG_USB_DWC3_RTK_DUAL_ROLE
	switch (dr_mode) {
	case USB_DR_MODE_PERIPHERAL:
		dev_info(rtk->dev, "%s dr_mode=USB_DR_MODE_PERIPHERAL\n",
			    __func__);
		dwc3_drd_to_device(rtk->dwc);
		break;
	case USB_DR_MODE_HOST:
		dev_info(rtk->dev, "%s dr_mode=USB_DR_MODE_HOST\n",
			    __func__);
		dwc3_drd_to_host(rtk->dwc);
		break;
	default:
		dev_info(rtk->dev, "%s dr_mode=%d\n", __func__, dr_mode);
		dwc3_drd_to_stop_all(rtk->dwc);
	}
#else
	dev_info(rtk->dev, "Not support CONFIG_USB_DWC3_RTK_DUAL_ROLE\n");
#endif /* CONFIG_USB_DWC3_RTK_DUAL_ROLE */
}

int dwc3_rtk_get_dr_mode(struct dwc3_rtk *rtk)
{
	return rtk->cur_dr_mode;
}

int dwc3_rtk_set_dr_mode(struct dwc3_rtk *rtk, int dr_mode)
{
	if (!rtk->support_drd_mode)
		return rtk->cur_dr_mode;

	dev_dbg(rtk->dev, "%s START....", __func__);

	rtk->cur_dr_mode = dr_mode;
	rtk->dwc->dr_mode = dr_mode;

	if (dr_mode != USB_DR_MODE_HOST) {
		dev_info(rtk->dev, "%s: To disable power\n", __func__);
		rtk_usb_port_power_on_off(rtk->dev, false);
	}

	switch_dwc3_dr_mode(rtk, dr_mode);
	mdelay(10);
	switch_u2_dr_mode(rtk, dr_mode);

	if (dr_mode == USB_DR_MODE_HOST) {
		dev_info(rtk->dev, "%s: To enable power\n", __func__);
		rtk_usb_port_power_on_off(rtk->dev, true);
	}

	dev_dbg(rtk->dev, "%s END....", __func__);

	return rtk->cur_dr_mode;
}

static ssize_t set_dr_mode_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);
	char *ptr = buf;
	int count = PAGE_SIZE;
	int n;

	n = scnprintf(ptr, count,
		     "Now cur_dr_mode is %s (default dwc3 dr_mode is %s)\n",
		    ({ char *tmp;
		switch (rtk->cur_dr_mode) {
		case USB_DR_MODE_PERIPHERAL:
		    tmp = "USB_DR_MODE_PERIPHERAL"; break;
		case USB_DR_MODE_HOST:
		    tmp = "USB_DR_MODE_HOST"; break;
		default:
		    tmp = "USB_DR_MODE_UNKNOWN"; break;
		    } tmp; }),
		    ({ char *tmp;
		switch (rtk->default_dwc3_dr_mode) {
		case USB_DR_MODE_PERIPHERAL:
		    tmp = "USB_DR_MODE_PERIPHERAL"; break;
		case USB_DR_MODE_HOST:
		    tmp = "USB_DR_MODE_HOST"; break;
		default:
		    tmp = "USB_DR_MODE_UNKNOWN"; break;
		    } tmp; }));

	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "write host -> switch to Host mode\n");
	ptr += n;
	count -= n;

	n = scnprintf(ptr, count,
		     "write device -> switch to Device mode\n");
	ptr += n;
	count -= n;

	return ptr - buf;
}

static ssize_t set_dr_mode_write(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);

	if (!strncmp(buf, "host", 4)) {
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_HOST);
	} else if (!strncmp(buf, "device", 6)) {
		dwc3_rtk_set_dr_mode(rtk, USB_DR_MODE_PERIPHERAL);
	} else {
		dwc3_rtk_set_dr_mode(rtk, 0);
	}
	return count;
}
static DEVICE_ATTR(set_dr_mode, 0644, set_dr_mode_show,
	    set_dr_mode_write);

static int dwc3_rtk_init(struct dwc3_rtk *rtk)
{
	struct device		*dev = rtk->dev;
	//struct device_node	*node = dev->of_node;
	void __iomem		*regs = rtk->regs;

	if ((get_rtd_chip_id() == CHIP_ID_RTD1295 ||
		    get_rtd_chip_id() == CHIP_ID_RTD1296) &&
		    get_rtd_chip_revision() == RTD_CHIP_A00) {
		writel(DISABLE_MULTI_REQ | readl(regs + WRAP_CTR_reg),
				regs + WRAP_CTR_reg);
		dev_info(dev, "[bug fixed] 1295/1296 A00: add workaround to disable multiple request for D-Bus");
	}

	if (get_rtd_chip_id() == CHIP_ID_RTD1395 ||
		    get_rtd_chip_id() == CHIP_ID_RTD1392) {
		writel(USB2_PHY_EN_PHY_PLL_PORT1 | readl(regs + WRAP_USB2_PHY_reg),
			    regs + WRAP_USB2_PHY_reg);
		dev_info(dev, "[bug fixed] 1395 add workaround to disable usb2 port 2 suspend!");

	}
	return 0;
}

static int dwc3_rtk_initiated(struct dwc3_rtk *rtk)
{
	struct device		*dev;
	struct dwc3		*dwc;

	if (!rtk) {
		pr_err("ERROR! rtk is NULL\n");
		return -1;
	}

	dev = rtk->dev;
	dwc = rtk->dwc;
	if (!dwc) {
		dev_err(dev, "ERROR! dwc3 is NULL\n");
		return -1;
	}

	dev_info(dev, "%s\n", __func__);

	/* workaround: to avoid transaction error and cause port reset
	 * we enable threshold control for TX/RX
	 * [Dev_Fix] Enable DWC3 threshold control for USB compatibility issue
	 * commit 77f116ba77cc089ee2a6ceca1d2aa496b39c98ba
	 * [Dev_Fix] change RX threshold packet count from 1 to 3,
	 * it will get better performance
	 * commit fe8905c2112f899f9ec3ddbfd83e0f183d3fbf7d
	 * [DEV_FIX] In case there may have transaction error once
	 * system bus busy
	 * commit b36294740c5cf66932c0fec429f4c5399e26f591
	 */
	if ((get_rtd_chip_id() == CHIP_ID_RTD1195) ||
		    ((get_rtd_chip_id() & CHIP_ID_RTD129X) ==
		        CHIP_ID_RTD129X) ||
		    ((get_rtd_chip_id() & CHIP_ID_RTD139X) ==
		        CHIP_ID_RTD139X)) {
		dwc3_writel(dwc->regs, DWC3_GTXTHRCFG,
			    DWC3_GTXTHRCFG_TXPKTCNT(1) |
			    DWC3_GTXTHRCFG_MAXTXBURSTSIZE(1));
		dwc3_writel(dwc->regs, DWC3_GRXTHRCFG,
			    DWC3_GRXTHRCFG_PKTCNTSEL |
			    DWC3_GRXTHRCFG_RXPKTCNT(3) |
			    DWC3_GRXTHRCFG_MAXRXBURSTSIZE(3));

		/* enable auto retry */
		dwc3_writel(dwc->regs, DWC3_GUCTL,
			    dwc3_readl(dwc->regs, DWC3_GUCTL) |
			    DWC3_GUCTL_HSTINAUTORETRY);
	}

	return 0;
}

static struct device_node *__get_device_node(struct device_node *node,
	 char *compatible)
{
	struct device_node	*child_node = NULL;

	do {
		child_node = of_get_next_child(node, child_node);
		if (child_node && of_device_is_compatible(child_node,
				    compatible)) {
			pr_debug("%s: Get %s node at child node\n",
			    __func__, compatible);
			return child_node;
		}
	} while (child_node);
	return child_node;
}

static int dwc3_rtk_probe_dwc3core(struct dwc3_rtk *rtk)
{
	struct device		*dev = rtk->dev;
	struct device_node	*node = dev->of_node;
	struct device_node	*dwc3_node;
	int    ret = 0;

	dwc3_rtk_init(rtk);

	if (node) {
		ret = of_platform_populate(node, NULL, NULL, dev);
		if (ret) {
			dev_err(dev, "failed to add dwc3 core\n");
			return ret;
		}

		dwc3_node = __get_device_node(node, "synopsys,dwc3");
		if (dwc3_node != NULL) {
			struct device *dwc3_dev;
			struct platform_device *dwc3_pdev;
			int dr_mode;
			struct device_node *type_c_node;

			dwc3_pdev = of_find_device_by_node(dwc3_node);
			dwc3_dev = &dwc3_pdev->dev;
			rtk->dwc = platform_get_drvdata(dwc3_pdev);

			dr_mode = usb_get_dr_mode(dwc3_dev);
			rtk->default_dwc3_dr_mode = dr_mode;
			rtk->cur_dr_mode = dr_mode;

			switch_u2_dr_mode(rtk, dr_mode);

			type_c_node = of_get_next_child(node, dwc3_node);
			if (!type_c_node ||
				     !of_device_is_available(type_c_node))
				rtk->support_type_c = false;
			else
				rtk->support_type_c = true;

			if (!rtk->support_type_c &&
				    (dr_mode == USB_DR_MODE_HOST))
				rtk_usb_init_port_power_on(rtk->dev);
		}
	}

	dwc3_rtk_initiated(rtk);

	if (rtk->support_drd_mode)
		device_create_file(dev, &dev_attr_set_dr_mode);

	return ret;
}

static void dwc3_rtk_probe_work(struct work_struct *work)
{
	struct dwc3_rtk *rtk = container_of(work, struct dwc3_rtk, work);
	struct device		*dev = rtk->dev;
	int    ret = 0;

	unsigned long probe_time = jiffies;

	dev_info(dev, "%s Start ...\n", __func__);

	ret = dwc3_rtk_probe_dwc3core(rtk);

	if (ret)
		dev_err(dev, "%s failed to add dwc3 core\n", __func__);

	dev_info(dev, "%s End ... ok! (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));
}

static int dwc3_rtk_probe(struct platform_device *pdev)
{
	struct dwc3_rtk	*rtk;
	struct device		*dev = &pdev->dev;
	struct device_node	*node = dev->of_node;

	struct resource         *res;
	void __iomem            *regs;

	int			ret = -ENOMEM;
	unsigned long probe_time = jiffies;

	dev_info(&pdev->dev, "Probe Realtek-SoC USB DWC3 Host Controller\n");

	rtk = devm_kzalloc(dev, sizeof(*rtk), GFP_KERNEL);
	if (!rtk) {
		dev_err(dev, "not enough memory\n");
		goto err1;
	}

	/*
	 * Right now device-tree probed devices don't get dma_mask set.
	 * Since shared usb code relies on it, set it here for now.
	 * Once we move to full device tree support this will vanish off.
	 */
	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	platform_set_drvdata(pdev, rtk);

	ret = dwc3_rtk_register_phys(rtk);
	if (ret) {
		dev_err(dev, "couldn't register PHYs\n");
		goto err1;
	}

	rtk->dev	= dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "missing memory resource\n");
		return -ENODEV;
	}

	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err1;
	}

	rtk->regs = regs;
	rtk->regs_size = resource_size(res);

	dwc3_rtk_debugfs_init(rtk);

	if (node) {
		if (of_property_read_bool(node, "dis_u3_port")) {
			void __iomem *usb_hmac_ctr0 = rtk->regs +
				    WRAP_USB_HMAC_CTR0_reg;
			int val_u3port_dis = U3PORT_DIS | readl(usb_hmac_ctr0);

			writel(val_u3port_dis, usb_hmac_ctr0);

			dev_info(rtk->dev, "%s: disable usb 3.0 port (usb_hmac_ctr0=0x%x)\n",
				    __func__, readl(usb_hmac_ctr0));
		}

		rtk->support_drd_mode = false;
		if (of_property_read_bool(node, "drd_mode")) {
			dev_info(rtk->dev, "%s: support drd_mode\n", __func__);
			rtk->support_drd_mode = true;
		}
	}

	if (node) {
		if (of_property_read_bool(node, "delay_probe_work")) {
			INIT_WORK(&rtk->work, dwc3_rtk_probe_work);
			if (of_property_read_bool(node, "ordered_probe"))
				rtk_usb_manager_schedule_work(dev, &rtk->work);
			else
				schedule_work(&rtk->work);
		} else {
			ret = dwc3_rtk_probe_dwc3core(rtk);
			if (ret) {
				dev_err(dev, "%s failed to add dwc3 core\n",
					    __func__);
				goto err2;
			}
		}
	} else {
		dev_err(dev, "no device node, failed to add dwc3 core\n");
		ret = -ENODEV;
		goto err2;
	}

	dev_info(dev, "%s ok! (take %d ms)\n", __func__,
		    jiffies_to_msecs(jiffies - probe_time));

	return 0;

err2:
err1:
	return ret;
}

static int dwc3_rtk_remove(struct platform_device *pdev)
{
	struct dwc3_rtk	*rtk = platform_get_drvdata(pdev);

	if (rtk->support_drd_mode)
		device_remove_file(rtk->dev, &dev_attr_set_dr_mode);

	dwc3_rtk_debugfs_exit(rtk);

	of_platform_depopulate(rtk->dev);
	platform_device_unregister(rtk->usb2_phy);
	platform_device_unregister(rtk->usb3_phy);

	clk_disable_unprepare(rtk->clk);

	return 0;
}

static void dwc3_rtk_shutdown(struct platform_device *pdev)
{
	struct dwc3_rtk	*rtk = platform_get_drvdata(pdev);
	struct device		*dev = &pdev->dev;

	dev_info(dev, "%s start ...\n", __func__);

	platform_device_unregister(rtk->usb2_phy);
	platform_device_unregister(rtk->usb3_phy);

	clk_disable_unprepare(rtk->clk);

	dev_info(dev, "%s ok!\n", __func__);
}

#ifdef CONFIG_OF
static const struct of_device_id rtk_dwc3_match[] = {
	{ .compatible = "Realtek,dwc3" },
	{ .compatible = "Realtek,rtk119x-dwc3" },
	{ .compatible = "Realtek,rtd129x-dwc3-drd" },
	{ .compatible = "Realtek,rtd129x-dwc3-u2h" },
	{ .compatible = "Realtek,rtd129x-dwc3-u3h" },
	{},
};
MODULE_DEVICE_TABLE(of, rtk_dwc3_match);
#endif

#ifdef CONFIG_PM_SLEEP
static int dwc3_rtk_suspend(struct device *dev)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);

	dev_info(dev, "[USB] Enter %s\n", __func__);

	if (!rtk) {
		dev_err(dev, "[USB] %s dwc3_rtk is NULL\n", __func__);
		goto out;
	}

#ifdef CONFIG_USB_PATCH_ON_RTK
	if (RTK_PM_STATE == PM_SUSPEND_STANDBY) {
		//For idle mode
		dev_info(dev, "[USB] %s Idle mode\n", __func__);
		goto out;
	}
#endif // CONFIG_USB_PATCH_ON_RTK

	//For suspend mode
	dev_info(dev,  "[USB] %s Suspend mode\n", __func__);

out:
	dev_info(dev, "[USB] Exit %s", __func__);
	return 0;
}

static int dwc3_rtk_resume(struct device *dev)
{
	struct dwc3_rtk *rtk = dev_get_drvdata(dev);
	struct dwc3 *dwc = rtk->dwc;

	dev_info(dev, "[USB] Enter %s", __func__);

	if (!rtk) {
		dev_err(dev, "[USB] %s dwc3_rtk is NULL\n", __func__);
		goto out;
	}

#ifdef CONFIG_USB_PATCH_ON_RTK
	if (RTK_PM_STATE == PM_SUSPEND_STANDBY) {
		//For idle mode
		dev_info(dev, "[USB] %s Idle mode\n", __func__);
		goto out;
	}
#endif // CONFIG_USB_PATCH_ON_RTK

	//For suspend mode
	dev_info(dev,  "[USB] %s Suspend mode\n", __func__);

	dwc3_rtk_init(rtk);

	switch_u2_dr_mode(rtk, dwc->dr_mode);

	dwc3_rtk_initiated(rtk);

	/* runtime set active to reflect active state. */
	pm_runtime_disable(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

out:
	dev_info(dev, "[USB] Exit %s\n", __func__);
	return 0;
}

static const struct dev_pm_ops dwc3_rtk_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dwc3_rtk_suspend, dwc3_rtk_resume)
};

#define DEV_PM_OPS	(&dwc3_rtk_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_rtk_driver = {
	.probe		= dwc3_rtk_probe,
	.remove		= dwc3_rtk_remove,
	.driver		= {
		.name	= "rtk-dwc3",
		.of_match_table = of_match_ptr(rtk_dwc3_match),
		.pm	= DEV_PM_OPS,
	},
	.shutdown	= dwc3_rtk_shutdown,
};

module_platform_driver(dwc3_rtk_driver);

MODULE_ALIAS("platform:rtk-dwc3");
MODULE_LICENSE("GPL");

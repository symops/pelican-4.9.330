/*
 *  phy-rtk-usb2.c RTK usb2.0 PHY driver
 *
 * Copyright (C) 2017 Realtek Semiconductor Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/usb/otg.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/nvmem-consumer.h>
#include <linux/slab.h>
#include <soc/realtek/rtk_chip.h>

#include "phy-rtk-usb.h"

#define RTK_USB2PHY_NAME "rtk-usb2phy"

#define WAIT_VBUSY_RETRY	3

#define OFFEST_PHY_READ	0x20

#define USB_ST_BUSY		BIT(23)


#define PAGE_START 0xE0
#define PAGE0_0xE4 0xE4
#define PAGE0_0xE7 0xE7
#define PAGE1_0xE0 0xE0
/* mapping 0xE0 to 0 ... 0xE7 to 7, 0xF0 to 8 ,,, 0xF7 to 15 */
#define PAGE_ADDR_MAP_ARRAY_INDEX(addr) \
	(((addr - PAGE_START)&0x7) + \
	(((addr - PAGE_START)&0x10)>>1))
#define ARRAY_INDEX_MAP_PAGE_ADDR(index) \
	(((index + PAGE_START)&0x7) + \
	(((index&0x8)<<1) + PAGE_START))

struct reg_addr {
	void __iomem *REG_WRAP_VStatusOut2;
	void __iomem *REG_GUSB2PHYACC0;
	int vstatus_index;
};

struct phy_parameter {
	u8 addr;
	u8 data;
};

struct phy_data {
	int page0_size;
	struct phy_parameter *page0;
	int page1_size;
	struct phy_parameter *page1;
	int page2_size;
	struct phy_parameter *page2;

	bool check_efuse;
	int8_t efuse_usb_dc_cal;
	bool do_toggle;
	bool use_default_parameter;
	bool is_double_sensitivity_mode;
	s32 ldo_page0_e4_compensate;
};

#define MAX_PHY_DATA_SIZE 20

static char rtk_usb_phy_read(struct reg_addr *regAddr, char addr)
{
	volatile unsigned int regVal;
	void __iomem *REG_GUSB2PHYACC0 = regAddr->REG_GUSB2PHYACC0;
	int ret = 0;

	addr -= OFFEST_PHY_READ;

	// polling until VBusy == 0
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return (char)ret;

	// VCtrl = low nibble of addr, VLoadM = 1
	regVal = BIT(25) | // vload
			 ((addr & 0x0f) << 8);	// vcontrol
	phy_write(REG_GUSB2PHYACC0, regVal);
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return (char)ret;

	// VCtrl = high nibble of addr, VLoadM = 1
	regVal = BIT(25) | // vload
			 ((addr & 0xf0) << 4);	// vcontrol
	phy_write(REG_GUSB2PHYACC0, regVal);
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return (char)ret;

	/* rmb  for reg read */
	smp_rmb();
	regVal = phy_read(REG_GUSB2PHYACC0);

	return (char) (regVal & 0xff);
}

static int rtk_usb_phy_write(struct reg_addr *regAddr, char addr, char data)
{
	volatile unsigned int regVal;
	void __iomem *REG_WRAP_VStatusOut2 = regAddr->REG_WRAP_VStatusOut2;
	void __iomem *REG_GUSB2PHYACC0 = regAddr->REG_GUSB2PHYACC0;
	int shift_bits = regAddr->vstatus_index * 8;
	int ret = 0;

	//write data to VStatusOut2 (data output to phy)
	phy_write(REG_WRAP_VStatusOut2, (u32)data<<shift_bits);
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return ret;

	// VCtrl = low nibble of addr, VLoadM = 1
	regVal = BIT(25) |
			 ((addr & 0x0f) << 8);

	phy_write(REG_GUSB2PHYACC0, regVal);
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return ret;

	// VCtrl = high nibble of addr, VLoadM = 1
	regVal = BIT(25) |
			 ((addr & 0xf0) << 4);

	phy_write(REG_GUSB2PHYACC0, regVal);
	ret = utmi_wait_register(REG_GUSB2PHYACC0, USB_ST_BUSY, 0);
	if (ret)
		return ret;

	return 0;
}

static int rtk_usb_phy_set_page(struct reg_addr *regAddr, int page)
{
	switch (page) {
	case 0:
		return rtk_usb_phy_write(regAddr, 0xf4, 0x9b);
	case 1:
		return rtk_usb_phy_write(regAddr, 0xf4, 0xbb);
	case 2:
		return rtk_usb_phy_write(regAddr, 0xf4, 0xdb);
	default:
		pr_err("%s error page=%d\n", __func__, page);
	}

	return -1;
}

#ifdef CONFIG_USB_PATCH_ON_RTK

#define USB_CTRL 0x98007FB0
#define ISO_USB_U2PHY_REG_LDO_PW (BIT(20) | BIT(21) | BIT(22) | BIT(23))

static int check_phy_power(struct reg_addr *regAddr)
{
	volatile unsigned int regVal;
	void __iomem *REG_GUSB2PHYACC0 = regAddr->REG_GUSB2PHYACC0;
	u8 addr = 0xF4;
	u8 read_addr = addr - OFFEST_PHY_READ;
	int use_ldo = 0;

	pr_info("%s\n", __func__);
	// VCtrl = low nibble of addr, VLoadM = 1
	regVal = BIT(25) | // vload
			 ((read_addr & 0x0f) << 8);	// vcontrol
	phy_write(REG_GUSB2PHYACC0, regVal);
	mdelay(1);

	// VCtrl = high nibble of addr, VLoadM = 1
	regVal = BIT(25) | // vload
			 ((read_addr & 0xf0) << 4);	// vcontrol
	phy_write(REG_GUSB2PHYACC0, regVal);
	mdelay(1);

	/* rmb  for reg read */
	smp_rmb();
	regVal = phy_read(REG_GUSB2PHYACC0);

	if ((regVal & 0xf) == 0x0) {
		void __iomem *reg = ioremap(USB_CTRL, 0x4);
		volatile unsigned int val;

		val = readl(reg);
		val |= ISO_USB_U2PHY_REG_LDO_PW;
		writel(val, reg);
		mdelay(1);

		phy_write(REG_GUSB2PHYACC0, BIT(25));
		iounmap(reg);

		use_ldo = 1;
		pr_notice("%s phy no power turn on it! ", __func__);
		pr_notice("USB_CTRL val=0x%x (regVal=0x%x ==> regVal=0x%x)\n",
			    val, regVal, rtk_usb_phy_read(regAddr, addr));
	} else {
		void __iomem *reg = ioremap(USB_CTRL, 0x4);
		volatile unsigned int val;

		val = readl(reg);
		if (val & ISO_USB_U2PHY_REG_LDO_PW) {
			pr_notice("%s phy use ldo power! ", __func__);
			pr_notice("USB_CTRL val=0x%x\n", val);
			use_ldo = 1;
		}
		iounmap(reg);
	}

	return use_ldo;
}
#endif // CONFIG_USB_PATCH_ON_RTK

static void rtk_usb2_phy_shutdown(struct usb_phy *phy)
{
	/* Todo */
}

static int updated_phy_parameter_by_efuse(struct rtk_usb_phy_s *rtk_phy,
	    struct phy_data *phy_data, int index)
{
	u8 value = 0;
	struct reg_addr *regAddr;
	struct phy_parameter *page0 = phy_data->page0;
	struct nvmem_cell *cell;
	int port_index = rtk_phy->port_index + index;
	int i;

	cell = nvmem_cell_get(rtk_phy->dev, "usb-dc-cal");
	if (IS_ERR(cell)) {
		dev_warn(rtk_phy->dev, "failed to get usb-dc-cal: %ld\n",
			    PTR_ERR(cell));
	} else {
		unsigned char *buf;
		size_t buf_size;
		int buf_index = port_index / 2;
		int buf_shift = port_index % 2;
		int value_size = 4;
		int value_mask = (BIT(value_size) - 1);

		buf = nvmem_cell_read(cell, &buf_size);

		value = (buf[buf_index] >> (buf_shift * value_size)) & value_mask;

		dev_dbg(rtk_phy->dev,
			    "port_index=%d (buf_index=%d buf_shift=%d, buf_size=%d) value=%x mask=%x\n",
			    port_index, buf_index, buf_shift, (int)buf_size,
			    value, value_mask);
		kfree(buf);
		nvmem_cell_put(cell);
	}

	dev_dbg(rtk_phy->dev, "Get Efuse usb_dc_cal for port_index=%d value=%x\n",
		    port_index, value);

	if (value < 0x8)
		phy_data->efuse_usb_dc_cal = (int8_t)value;
	else
		phy_data->efuse_usb_dc_cal = -(int8_t)(0x7 & value);

	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[index];

	i = PAGE_ADDR_MAP_ARRAY_INDEX((u8)PAGE0_0xE4);

	if (i < phy_data->page0_size) {
		if (!(page0 + i)->addr) {
			u8 addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			u8 data = rtk_usb_phy_read(regAddr, addr);

			(page0 + i)->addr = addr;
			(page0 + i)->data = data;
			dev_dbg(rtk_phy->dev,
				    "Get default addr %x value %x\n",
				    (page0 + i)->addr, (page0 + i)->data);
		}
		(page0 + i)->data +=
			    phy_data->efuse_usb_dc_cal;

		dev_info(rtk_phy->dev,
			    "Set addr %x value %x by efuse_usb_dc_cal=%d\n",
			    (page0 + i)->addr, (page0 + i)->data,
			    phy_data->efuse_usb_dc_cal);
	} else {
		dev_err(rtk_phy->dev,
			    "ERROR: %s %d index=%d addr Not PAGE0_0xE4\n",
			    __func__, __LINE__, i);
	}

	return 0;
}

static void do_rtk_usb2_phy_toggle(struct rtk_usb_phy_s *rtk_phy,
	    int index, bool isConnect);

static int do_rtk_usb2_phy_init(struct usb_phy *phy, int index)
{
	int i;
	struct rtk_usb_phy_s *rtk_phy = (struct rtk_usb_phy_s *) phy;
	struct reg_addr *regAddr;
	struct phy_data *phy_data;
	struct phy_parameter *phy_page_setting;
	s32 ldo_page0_e4_compensate = 0;

	if (!rtk_phy) {
		pr_err("%s, rtk_phy is NULL\n", __func__);
		return -EINVAL;
	}

	dev_info(phy->dev, "%s Init RTK USB 2.0 PHY phy#%d\n",
		    __func__, index);

	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[index];
	phy_data = &((struct phy_data *)rtk_phy->phy_data)[index];

	if (!phy_data) {
		pr_err("%s, phy_data is NULL\n", __func__);
		return -EINVAL;
	}

#ifdef CONFIG_USB_PATCH_ON_RTK
	if (check_phy_power(regAddr)) {
		dev_info(phy->dev, "%s USB phy use ldo power compensate phy parameter (%d)\n",
		    __func__, phy_data->ldo_page0_e4_compensate);
		ldo_page0_e4_compensate = phy_data->ldo_page0_e4_compensate;
	}
#endif // CONFIG_USB_PATCH_ON_RTK

	if (phy_data->use_default_parameter) {
		dev_info(phy->dev, "%s phy#%d use default parameter\n",
			    __func__, index);
		goto do_toggle;
	}

	if (phy_data->check_efuse)
		updated_phy_parameter_by_efuse(rtk_phy, phy_data, index);

	/* Set page 0 */
	phy_page_setting = phy_data->page0;
	rtk_usb_phy_set_page(regAddr, 0);

	for (i = 0; i < phy_data->page0_size; i++) {
		u8 addr = (phy_page_setting + i)->addr;
		u8 data = (phy_page_setting + i)->data;

		if (!addr)
			continue;

		if (addr == PAGE0_0xE4)
			data = (u8)((s32)data + ldo_page0_e4_compensate);

		if (rtk_usb_phy_write(regAddr, addr, data)) {
			dev_err(phy->dev,
				    "[%s:%d] Error page0 addr=0x%x value=0x%x\n",
				    __func__, __LINE__, addr, data);
			return -1;
		}
		dev_dbg(phy->dev, "[%s:%d] Good page0 addr=0x%x value=0x%x\n",
			    __func__, __LINE__, addr,
			    rtk_usb_phy_read(regAddr, addr));
	}

	/* Set page 1 */
	phy_page_setting = phy_data->page1;
	rtk_usb_phy_set_page(regAddr, 1);

	for (i = 0; i < phy_data->page1_size; i++) {
		u8 addr = (phy_page_setting + i)->addr;
		u8 data = (phy_page_setting + i)->data;

		if (!addr)
			continue;

		if (rtk_usb_phy_write(regAddr, addr, data)) {
			dev_err(phy->dev,
				    "[%s:%d] Error page1 addr=0x%x value=0x%x\n",
				    __func__, __LINE__,
				    addr, data);
			return -1;
		}
		dev_dbg(phy->dev, "[%s:%d] Good page1 addr=0x%x value=0x%x\n",
			    __func__, __LINE__, addr,
			    rtk_usb_phy_read(regAddr, addr));
	}

	if (phy_data->page2_size == 0)
		goto do_toggle;

	/* Set page 2 */
	phy_page_setting = phy_data->page2;
	rtk_usb_phy_set_page(regAddr, 2);

	for (i = 0; i < phy_data->page2_size; i++) {
		u8 addr = (phy_page_setting + i)->addr;
		u8 data = (phy_page_setting + i)->data;

		if (!addr)
			continue;

		if (rtk_usb_phy_write(regAddr, addr, data)) {
			dev_err(phy->dev,
				    "[%s:%d] Error page2 addr=0x%x value=0x%x\n",
				    __func__, __LINE__, addr, data);
			return -1;
		}
		dev_dbg(phy->dev, "[%s:%d] Good page2 addr=0x%x value=0x%x\n",
			    __func__, __LINE__,
			    (phy_page_setting + i)->addr,
			    rtk_usb_phy_read(regAddr,
			      (phy_page_setting + i)->addr));
	}

do_toggle:
	do_rtk_usb2_phy_toggle(rtk_phy, index, false);

	return 0;
}

static int rtk_usb2_phy_init(struct usb_phy *phy)
{
	struct rtk_usb_phy_s *rtk_phy = (struct rtk_usb_phy_s *) phy;
	int i, ret = 0;
	unsigned long phy_init_time = jiffies;

	dev_info(phy->dev, "%s Init RTK USB 2.0 PHY\n", __func__);
	for (i = 0; i < rtk_phy->phyN; i++)
		ret = do_rtk_usb2_phy_init(phy, i);

	dev_info(phy->dev, "%s Initialized RTK USB 2.0 PHY (take %dms)\n",
		    __func__,
		    jiffies_to_msecs(jiffies - phy_init_time));
	return ret;
}

static void do_rtk_usb2_phy_toggle(struct rtk_usb_phy_s *rtk_phy,
	    int index, bool isConnect)
{
	struct reg_addr *regAddr;
	struct phy_data *phy_data;
	struct phy_parameter *phy_page_setting;
	int i;

	if (!rtk_phy) {
		pr_err("%s phy_data is NULL\n", __func__);
		return;
	}

	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[index];
	phy_data = &((struct phy_data *)rtk_phy->phy_data)[index];

	if (!phy_data) {
		dev_err(rtk_phy->dev, "%s phy_data is NULL\n", __func__);
		return;
	}

	if (!phy_data->do_toggle)
		return;

	/* Set page 1 */
	phy_page_setting = phy_data->page1;
	rtk_usb_phy_set_page(regAddr, 1);

	i = PAGE_ADDR_MAP_ARRAY_INDEX(PAGE1_0xE0);

	if (i < phy_data->page1_size) {
		u8 addr = (phy_page_setting + i)->addr;
		u8 data = (phy_page_setting + i)->data;

		if (!addr) {
			addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			data = rtk_usb_phy_read(regAddr, addr);

			(phy_page_setting + i)->addr = addr;
			(phy_page_setting + i)->data = data;
			dev_dbg(rtk_phy->dev,
				    "Get default addr %x value %x\n",
				    (phy_page_setting + i)->addr,
				    (phy_page_setting + i)->data);
		}

		dev_info(rtk_phy->dev,
			    "%s ########## to toggle PAGE1_0xE0 BIT(2)\n",
			    __func__);
		rtk_usb_phy_write(regAddr, addr, data & (~BIT(2)));
		mdelay(1);
		rtk_usb_phy_write(regAddr, addr, data | (BIT(2)));
	} else {
		dev_err(rtk_phy->dev,
			    "ERROR: %s %d index=%d addr Not PAGE1_0xE0\n",
			    __func__, __LINE__, i);
	}

	if (phy_data->is_double_sensitivity_mode)
		return;

	/* Set page 0 */
	phy_page_setting = phy_data->page0;
	rtk_usb_phy_set_page(regAddr, 0);

	i = PAGE_ADDR_MAP_ARRAY_INDEX(PAGE0_0xE7);

	if (i < phy_data->page0_size) {
		u8 addr = (phy_page_setting + i)->addr;
		u8 data = (phy_page_setting + i)->data;

		if (!addr) {
			addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			data = rtk_usb_phy_read(regAddr, addr);

			(phy_page_setting + i)->addr = addr;
			(phy_page_setting + i)->data = data;
			dev_dbg(rtk_phy->dev,
				    "Get default addr %x value %x\n",
				    (phy_page_setting + i)->addr,
				    (phy_page_setting + i)->data);
		}

		if (isConnect) {
			rtk_usb_phy_write(regAddr, addr, data &
				    (~(BIT(4) | BIT(5) | BIT(6))));
		} else {
			rtk_usb_phy_write(regAddr, addr, data |
				    (BIT(4) | BIT(5) | BIT(6)));
		}
		dev_info(rtk_phy->dev,
			    "%s %sconnect to set Page0 0xE7=%x\n",
			    __func__,
			    isConnect?"":"dis",
			    rtk_usb_phy_read(regAddr, addr));
	} else {
		dev_err(rtk_phy->dev,
			    "ERROR: %s %d index=%d addr Not PAGE0_0xE7\n",
			    __func__, __LINE__, i);
	}
}

void rtk_usb2_phy_toggle(struct usb_phy *usb2_phy, bool isConnect, int port)
{
	int index = port;
	struct rtk_usb_phy_s *rtk_phy = NULL;

	if (usb2_phy != NULL && usb2_phy->dev != NULL)
		rtk_phy = dev_get_drvdata(usb2_phy->dev);

	if (rtk_phy == NULL) {
		pr_err("%s %d ERROR! NO this device\n", __func__, __LINE__);
		return;
	}
	if (index > rtk_phy->phyN) {
		pr_err("%s %d ERROR! port=%d > phyN=%d\n",
			    __func__, __LINE__, index, rtk_phy->phyN);
		return;
	}

	do_rtk_usb2_phy_toggle(rtk_phy, index, isConnect);

}
EXPORT_SYMBOL(rtk_usb2_phy_toggle);

#ifdef CONFIG_DYNAMIC_DEBUG
static int rtk_usb2_parameter_show(struct seq_file *s, void *unused)
{
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	int i, index;

	for (index = 0; index < rtk_phy->phyN; index++) {
		struct reg_addr *regAddr =
			    &((struct reg_addr *)rtk_phy->reg_addr)[index];
		struct phy_data *phy_data =
			    &((struct phy_data *)rtk_phy->phy_data)[index];
		struct phy_parameter *phy_page_setting;

		seq_printf(s, "PHY %d:\n", index);

		seq_puts(s, "Page 0:\n");
		/* Set page 0 */
		phy_page_setting = phy_data->page0;
		rtk_usb_phy_set_page(regAddr, 0);

		for (i = 0; i < phy_data->page0_size; i++) {
			u8 addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			u8 data = (phy_page_setting + i)->data;
			u8 value = rtk_usb_phy_read(regAddr, addr);

			if ((phy_page_setting + i)->addr)
				seq_printf(s, "Page 0: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					    addr, data, value);
			else
				seq_printf(s, "Page 0: addr=0x%x data=none ==> read value=0x%02x\n",
					    addr, value);
		}

		seq_puts(s, "Page 1:\n");
		/* Set page 1 */
		phy_page_setting = phy_data->page1;
		rtk_usb_phy_set_page(regAddr, 1);

		for (i = 0; i < phy_data->page1_size; i++) {
			u8 addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			u8 data = (phy_page_setting + i)->data;
			u8 value = rtk_usb_phy_read(regAddr, addr);

			if ((phy_page_setting + i)->addr)
				seq_printf(s, "Page 1: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					    addr, data, value);
			else
				seq_printf(s, "Page 1: addr=0x%x data=none ==> read value=0x%02x\n",
					    addr, value);
		}

		if (phy_data->page2_size == 0)
			goto out;

		seq_puts(s, "Page 2:\n");
		/* Set page 2 */
		phy_page_setting = phy_data->page2;
		rtk_usb_phy_set_page(regAddr, 2);

		for (i = 0; i < phy_data->page2_size; i++) {
			u8 addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);
			u8 data = (phy_page_setting + i)->data;
			u8 value = rtk_usb_phy_read(regAddr, addr);

			if ((phy_page_setting + i)->addr)
				seq_printf(s, "Page 2: addr=0x%x data=0x%02x ==> read value=0x%02x\n",
					    addr, data, value);
			else
				seq_printf(s, "Page 2: addr=0x%x data=none ==> read value=0x%02x\n",
					    addr, value);
		}
	}

out:
	return 0;
}

static int rtk_usb2_parameter_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb2_parameter_show, inode->i_private);
}

static const struct file_operations rtk_usb2_parameter_fops = {
	.open			= rtk_usb2_parameter_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int rtk_usb2_set_parameter_show(struct seq_file *s, void *unused)
{
	//struct rtk_usb_phy_s	*rtk_phy = s->private;

	seq_puts(s, "Set Phy parameter by following command\n");
	seq_puts(s, "echo \"phy_num page addr value\" > set_parameter\n");
	seq_puts(s, "echo \"0 page0 0xE1 0x30\" > set_parameter\n");
	seq_puts(s, "echo \"0 page1 0xE1 0xEF\" > set_parameter\n");

	return 0;
}

static int rtk_usb2_set_parameter_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb2_set_parameter_show, inode->i_private);
}

static ssize_t rtk_usb2_set_parameter_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	struct reg_addr *regAddr;
	struct phy_data *phy_data;
	struct phy_parameter *phy_page_setting;
	int page_size = 0;
	int ret = 0;
	char			buffer[40];
	char *buf = buffer;
	int i, index;
	u32 addr;
	u32 value;

	if (copy_from_user(&buffer, ubuf,
		    min_t(size_t, sizeof(buffer) - 1, count)))
		return -EFAULT;

	ret = sscanf(buf, "%d", &index);
	if (ret < 0)
		return -EFAULT;

	buf = buf + 2;
	regAddr = &((struct reg_addr *)rtk_phy->reg_addr)[index];
	phy_data = &((struct phy_data *)rtk_phy->phy_data)[index];

	if (!strncmp(buf, "page0", 5)) {
		buf = buf + 5;
		buf = skip_spaces(buf);
		ret = sscanf(buf, "%x %x", &addr, &value);
		if (ret < 0)
			return -EFAULT;

		phy_page_setting = phy_data->page0;
		page_size = phy_data->page0_size;
		rtk_usb_phy_set_page(regAddr, 0);
		dev_dbg(rtk_phy->dev, "%s page0 addr = 0x%x, value = 0x%x\n",
			    __func__, addr, value);

	} else if (!strncmp(buf, "page1", 5)) {
		buf = buf + 5;
		buf = skip_spaces(buf);
		ret = sscanf(buf, "%x %x", &addr, &value);
		if (ret < 0)
			return -EFAULT;

		phy_page_setting = phy_data->page1;
		page_size = phy_data->page1_size;
		rtk_usb_phy_set_page(regAddr, 1);
		dev_dbg(rtk_phy->dev, "%s page1 addr = 0x%x, value = 0x%x\n",
			    __func__, addr, value);
	} else if (!strncmp(buf, "page2", 5)) {
		buf = buf + 5;
		buf = skip_spaces(buf);
		ret = sscanf(buf, "%x %x", &addr, &value);
		if (ret < 0)
			return -EFAULT;

		phy_page_setting = phy_data->page2;
		page_size = phy_data->page2_size;
		rtk_usb_phy_set_page(regAddr, 2);
		dev_dbg(rtk_phy->dev, "%s page2 addr = 0x%x, value = 0x%x\n",
			    __func__, addr, value);
	} else {
		dev_err(rtk_phy->dev, "UNKNOWN input (%s)", buf);
	}

	for (i = 0; i < page_size; i++) {
		u8 i2addr = ARRAY_INDEX_MAP_PAGE_ADDR(i);

		if (i2addr == addr) {
			(phy_page_setting + i)->addr = addr;
			(phy_page_setting + i)->data = value;
			if (rtk_usb_phy_write(regAddr, addr, value))
				dev_err(rtk_phy->dev,
					    "[%s:%d] Error: addr=0x%x value=0x%x\n",
					    __func__, __LINE__, addr, value);
			else
				dev_dbg(rtk_phy->dev,
					    "[%s:%d] Good: addr=0x%x value=0x%x\n",
					    __func__, __LINE__, addr,
					    rtk_usb_phy_read(regAddr, addr));
		}
	}

	return count;
}

static const struct file_operations rtk_usb2_set_parameter_fops = {
	.open			= rtk_usb2_set_parameter_open,
	.write			= rtk_usb2_set_parameter_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int rtk_usb2_toggle_show(struct seq_file *s, void *unused)
{
	struct rtk_usb_phy_s *rtk_phy = s->private;
	struct phy_data *phy_data;
	int i;

	for (i = 0; i < rtk_phy->phyN; i++) {
		phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
		seq_printf(s, "Now phy#%d do_toggle is %s.\n",
			    i, phy_data->do_toggle?"Enable":"Disable");
	}
	seq_puts(s, "ehco 1 to enable toggle phy parameter.\n");

	return 0;
}

static int rtk_usb2_toggle_open(struct inode *inode, struct file *file)
{
	return single_open(file, rtk_usb2_toggle_show, inode->i_private);
}

static ssize_t rtk_usb2_toggle_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct rtk_usb_phy_s		*rtk_phy = s->private;
	char			buf[32];
	struct phy_data *phy_data;
	bool enable = false;
	int i;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "1", 1))
		enable = true;

	for (i = 0; i < rtk_phy->phyN; i++) {
		phy_data = &((struct phy_data *)rtk_phy->phy_data)[i];
		phy_data->do_toggle = enable;
		dev_info(rtk_phy->dev, "Set phy#%d do_toggle is %s.\n",
			    i, phy_data->do_toggle?"Enable":"Disable");
	}

	return count;
}

static const struct file_operations rtk_usb2_toggle_fops = {
	.open			= rtk_usb2_toggle_open,
	.write			= rtk_usb2_toggle_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static inline void create_debug_files(struct rtk_usb_phy_s *rtk_phy)
{
	struct dentry *phy_debug_root = NULL;

	dev_dbg(rtk_phy->dev, "%s", __func__);

	phy_debug_root = create_phy_debug_root();

	if (!phy_debug_root) {
		dev_err(rtk_phy->dev, "%s Error phy_debug_root is NULL",
			    __func__);
		return;
	}

	rtk_phy->debug_dir = debugfs_create_dir(dev_name(rtk_phy->dev),
		    phy_debug_root);
	if (!rtk_phy->debug_dir) {
		dev_err(rtk_phy->dev, "%s Error debug_dir is NULL", __func__);
		return;
	}

	if (!debugfs_create_file("parameter", 0444, rtk_phy->debug_dir, rtk_phy,
		    &rtk_usb2_parameter_fops))
		goto file_error;

	if (!debugfs_create_file("set_parameter", 0644,
		    rtk_phy->debug_dir, rtk_phy, &rtk_usb2_set_parameter_fops))
		goto file_error;

	if (!debugfs_create_file("toggle", 0644,
		    rtk_phy->debug_dir, rtk_phy, &rtk_usb2_toggle_fops))
		goto file_error;

	return;

file_error:
	debugfs_remove_recursive(rtk_phy->debug_dir);
}
#endif //CONFIG_DYNAMIC_DEBUG

#define DEFAULT_CHIP_REVISION 0xA00
static int __get_phy_parameter_v1(struct device *dev, struct phy_data *phy_data,
	    struct device_node *sub_node)
{
	int phy_data_page0_size, phy_data_page1_size;
	int phy_data_page2_size;
	char tmp_addr[MAX_PHY_DATA_SIZE];
	char tmp_data[MAX_PHY_DATA_SIZE];
	int i, chip_revision, revision, ret = 0;

	chip_revision = get_rtd_chip_revision();

	dev_dbg(dev, "%s: Chip revision is %x\n", __func__, chip_revision);

	ret = of_property_read_u32_index(sub_node,
		    "phy_data_page0_size", 0, &phy_data_page0_size);
	if (ret)
		goto err;

	ret = of_property_read_u32_index(sub_node,
		    "phy_data_page1_size", 0, &phy_data_page1_size);
	if (ret)
		goto err;

	dev_dbg(dev, "%s %d phy_data_page0_size=%d, phy_data_page1_size=%d\n",
		    __func__, __LINE__,
		    phy_data_page0_size, phy_data_page1_size);

	if (phy_data_page0_size > MAX_PHY_DATA_SIZE ||
		    phy_data_page1_size > MAX_PHY_DATA_SIZE) {
		dev_err(dev, "%s phy_data size > MAX_PHY_DATA_SIZE\n",
			    __func__);
		goto err;
	}

	ret = of_property_read_u32_index(sub_node,
		    "phy_data_page2_size", 0, &phy_data_page2_size);
	if (ret)
		phy_data_page2_size = 0;
	dev_dbg(dev, "%s %d phy_data_page2_size=%d\n",
		    __func__, __LINE__,
		    phy_data_page2_size);

	if (phy_data_page2_size > MAX_PHY_DATA_SIZE) {
		dev_err(dev, "%s page2 phy_data size=%d > MAX_PHY_DATA_SIZE\n",
			    __func__, phy_data_page2_size);
		goto err;
	}

	phy_data->page0_size = phy_data_page0_size;
	phy_data->page0 = devm_kzalloc(dev,
		    sizeof(struct phy_parameter) *
			phy_data_page0_size,
		    GFP_KERNEL);
	if (!phy_data->page0) {
		ret = -ENOMEM;
		goto err;
	}

	phy_data->page1_size = phy_data_page1_size;
	phy_data->page1 = devm_kzalloc(dev,
		    sizeof(struct phy_parameter) *
			phy_data_page1_size,
		    GFP_KERNEL);
	if (!phy_data->page1) {
		ret = -ENOMEM;
		goto err;
	}

	phy_data->page2_size = phy_data_page2_size;
	if (phy_data->page2_size > 0) {
		phy_data->page2 = devm_kzalloc(dev,
			    sizeof(struct phy_parameter) *
				phy_data->page2_size,
			    GFP_KERNEL);
		if (!phy_data->page2) {
			ret = -ENOMEM;
			goto err;
		}
	}

	ret = of_property_read_u8_array(sub_node, "phy_data_page0_addr",
		    tmp_addr, phy_data_page0_size);
	if (ret)
		goto err;

	revision = chip_revision;
	while (revision >= DEFAULT_CHIP_REVISION) {
		char phy_data_revision[20] = {0};

		snprintf(phy_data_revision, 19, "phy_data_page0_%X", revision);

		ret = of_property_read_u8_array(sub_node, phy_data_revision,
			    tmp_data, phy_data_page0_size);
		if (!ret) {
			dev_dbg(dev, "%s load %s parameter\n",
				    __func__, phy_data_revision);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;
	}

	/* For old device tree */
	if (ret) {
		ret = of_property_read_u8_array(sub_node, "phy_data_page0_data",
			    tmp_data, phy_data_page0_size);
		if (ret)
			goto err;
		else
			dev_info(dev, "%s load page0 parameter\n",
				    __func__);
	}

	for (i = 0; i < phy_data_page0_size; i++) {
		struct phy_parameter *phy_data_page0 =
			    (phy_data->page0 + i);

		phy_data_page0->addr = tmp_addr[i];
		phy_data_page0->data = tmp_data[i];
	}

	ret = of_property_read_u8_array(sub_node, "phy_data_page1_addr",
		    tmp_addr, phy_data_page1_size);
	if (ret)
		goto err;

	revision = chip_revision;
	while (revision >= DEFAULT_CHIP_REVISION) {
		char phy_data_revision[20] = {0};

		snprintf(phy_data_revision, 19, "phy_data_page1_%X", revision);

		ret = of_property_read_u8_array(sub_node, phy_data_revision,
			    tmp_data, phy_data_page1_size);
		if (!ret) {
			dev_dbg(dev, "%s load %s parameter\n",
				    __func__, phy_data_revision);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;
	}

	/* For old device tree */
	if (ret) {
		ret = of_property_read_u8_array(sub_node, "phy_data_page1_data",
			    tmp_data, phy_data_page1_size);
		if (ret)
			goto err;
		else
			dev_info(dev, "%s load page1 parameter\n",
				    __func__);
	}

	for (i = 0; i < phy_data_page1_size; i++) {
		struct phy_parameter *phy_data_page1 =
			    (phy_data->page1 + i);

		phy_data_page1->addr = tmp_addr[i];
		phy_data_page1->data = tmp_data[i];
	}

	if (phy_data->page2_size > 0) {
		ret = of_property_read_u8_array(sub_node,
			    "phy_data_page2_addr",
			    tmp_addr, phy_data->page2_size);
		if (ret)
			goto err;

		revision = chip_revision;
		while (revision >= DEFAULT_CHIP_REVISION) {
			char phy_data_revision[20] = {0};

			snprintf(phy_data_revision, 19, "phy_data_page2_%X",
				    revision);

			ret = of_property_read_u8_array(sub_node,
				    phy_data_revision,
				    tmp_data, phy_data_page2_size);
			if (!ret) {
				dev_dbg(dev, "%s load %s parameter\n",
					    __func__, phy_data_revision);
				break;
			}
			revision--;
			if ((revision & 0xFF) > 0xF)
				revision = (revision & 0xF00) + 0xF;
		}

		/* For old device tree */
		if (ret) {
			ret = of_property_read_u8_array(sub_node,
				    "phy_data_page2_data",
				    tmp_data, phy_data->page2_size);
			if (ret)
				goto err;
			else
				dev_info(dev, "%s load page2 parameter\n",
					    __func__);
		}
		for (i = 0; i < phy_data->page2_size; i++) {
			struct phy_parameter *phy_data_page2 =
				    (phy_data->page2 + i);
			phy_data_page2->addr = tmp_addr[i];
			phy_data_page2->data = tmp_data[i];
		}
	}

err:
	return ret;
}

static int __get_phy_parameter_v2(struct device *dev, struct phy_data *phy_data,
	    struct device_node *sub_node)
{
	u32 page_size = 0;
	u32 num_cells = 2; /*< addr value > */
	u32 data_size;
	int i, offset, chip_revision, revision, ret = 0;
	char phy_data_revision[15] = {0};

	chip_revision = get_rtd_chip_revision();

	/* Page 0 */
	ret = of_property_read_u32_index(sub_node, "page0_size", 0, &page_size);
	if (ret) {
		dev_err(dev, "%s No page0_size\n", __func__);
		goto parse_page1;
	}

	phy_data->page0_size = page_size;
	phy_data->page0 = devm_kzalloc(dev,
		    sizeof(struct phy_parameter) * page_size, GFP_KERNEL);
	if (!phy_data->page0) {
		ret = -ENOMEM;
		goto out;
	}

	revision = chip_revision;
	while (revision >= DEFAULT_CHIP_REVISION) {
		snprintf(phy_data_revision, 15, "page0_data_%X", revision);

		if (of_get_property(sub_node, phy_data_revision, &data_size)) {
			dev_dbg(dev, "%s load %s parameter (data_size=%d)\n",
				    __func__, phy_data_revision, data_size);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;

		data_size = 0;
		ret = 0;
	}
	data_size = data_size / (sizeof(u32) * num_cells);

	for (i = 0; i < data_size; i++) {
		struct phy_parameter *phy_data_page;
		u32 addr, data;
		int index;

		offset = i * num_cells;

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset, &addr);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, addr);
			break;
		}

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset + 1, &data);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, data);
			break;
		}

		index = PAGE_ADDR_MAP_ARRAY_INDEX(addr);
		phy_data_page = (phy_data->page0 + index);
		phy_data_page->addr = (char)addr;
		phy_data_page->data = (char)data;

		dev_dbg(dev, "%s index=%d addr=0x%x data=0x%x\n",
			    phy_data_revision, index,
			    phy_data_page->addr, phy_data_page->data);
	}

parse_page1:
	/* Page 1 */
	ret = of_property_read_u32_index(sub_node, "page1_size", 0, &page_size);
	if (ret) {
		dev_err(dev, "%s No page0_size\n", __func__);
		goto parse_page2;
	}

	phy_data->page1_size = page_size;
	phy_data->page1 = devm_kzalloc(dev,
		    sizeof(struct rtk_usb_phy_data_s) * page_size, GFP_KERNEL);
	if (!phy_data->page1) {
		ret = -ENOMEM;
		goto out;
	}

	revision = chip_revision;
	while (revision >= DEFAULT_CHIP_REVISION) {
		snprintf(phy_data_revision, 15, "page1_data_%X", revision);

		if (of_get_property(sub_node, phy_data_revision, &data_size)) {
			dev_dbg(dev, "%s load %s parameter (data_size=%d)\n",
				    __func__, phy_data_revision, data_size);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;

		data_size = 0;
		ret = 0;
	}
	data_size = data_size / (sizeof(u32) * num_cells);

	for (i = 0; i < data_size; i++) {
		struct phy_parameter *phy_data_page;
		u32 addr, data;
		int index;

		offset = i * num_cells;

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset, &addr);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, addr);
			break;
		}

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset + 1, &data);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, data);
			break;
		}

		index = PAGE_ADDR_MAP_ARRAY_INDEX(addr);
		phy_data_page = phy_data->page1 + index;
		phy_data_page->addr = (char)addr;
		phy_data_page->data = (char)data;

		dev_dbg(dev, "%s index=%d addr=0x%x data=0x%x\n",
			    phy_data_revision, index,
			    phy_data_page->addr, phy_data_page->data);
	}

parse_page2:
	/* Page 2 */
	ret = of_property_read_u32_index(sub_node, "page2_size", 0, &page_size);
	if (ret) {
		dev_dbg(dev, "%s No page2_size\n", __func__);
		goto out;
	}

	phy_data->page2_size = page_size;
	phy_data->page2 = devm_kzalloc(dev,
		    sizeof(struct rtk_usb_phy_data_s) * page_size, GFP_KERNEL);
	if (!phy_data->page2) {
		ret = -ENOMEM;
		goto out;
	}

	revision = chip_revision;
	while (revision >= DEFAULT_CHIP_REVISION) {
		snprintf(phy_data_revision, 15, "page2_data_%X", revision);

		if (of_get_property(sub_node, phy_data_revision, &data_size)) {
			dev_dbg(dev, "%s load %s parameter (data_size=%d)\n",
				    __func__, phy_data_revision, data_size);
			break;
		}
		revision--;
		if ((revision & 0xFF) > 0xF)
			revision = (revision & 0xF00) + 0xF;

		data_size = 0;
		ret = 0;
	}
	data_size = data_size / (sizeof(u32) * num_cells);

	for (i = 0; i < data_size; i++) {
		struct phy_parameter *phy_data_page;
		u32 addr, data;
		int index;

		offset = i * num_cells;

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset, &addr);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, addr);
			break;
		}

		ret = of_property_read_u32_index(sub_node, phy_data_revision,
			    offset + 1, &data);
		if (ret) {
			dev_err(dev, "ERROR: To get %s i=%d addr=0x%x\n",
				    phy_data_revision, i, data);
			break;
		}

		index = PAGE_ADDR_MAP_ARRAY_INDEX(addr);
		phy_data_page = phy_data->page2 + index;
		phy_data_page->addr = (char)addr;
		phy_data_page->data = (char)data;

		dev_dbg(dev, "%s index=%d addr=0x%x data=0x%x\n",
			    phy_data_revision, index,
			    phy_data_page->addr, phy_data_page->data);
	}

out:
	return ret;
}

static int __get_phy_parameter(struct rtk_usb_phy_s *rtk_usb_phy, int index)
{
	struct device *dev = rtk_usb_phy->dev;
	struct reg_addr *addr =
		    &((struct reg_addr *)rtk_usb_phy->reg_addr)[index];
	struct phy_data *phy_data =
		    &((struct phy_data *)rtk_usb_phy->phy_data)[index];
	char phy_name[5], phy_name_v2[10];
	struct device_node *sub_node;
	int ret = 0;

	addr->REG_GUSB2PHYACC0     = of_iomap(dev->of_node, index);
	addr->REG_WRAP_VStatusOut2 = of_iomap(dev->of_node, rtk_usb_phy->phyN);
	addr->vstatus_index = index;
	dev_dbg(dev, "%s %d #%d REG_WRAP_VStatusOut2=%p\n",
		    __func__, __LINE__,
		    index, addr->REG_WRAP_VStatusOut2);
	dev_dbg(dev, "%s %d #%d REG_GUSB2PHYACC0=%p\n",
		    __func__, __LINE__,
		    index, addr->REG_GUSB2PHYACC0);

	snprintf(phy_name, 5, "phy%d", index);

	sub_node = of_get_child_by_name(dev->of_node, phy_name);
	if (sub_node) {
		dev_info(dev, "%s %d: #%d Get phy data v1 sub_node for %s\n",
			    __func__, __LINE__, index, phy_name);
		ret = __get_phy_parameter_v1(dev, phy_data, sub_node);
		if (ret)
			goto err;
	} else {
		snprintf(phy_name_v2, 10, "phy%d_data", index);
		sub_node = of_get_child_by_name(dev->of_node, phy_name_v2);
		if (sub_node) {
			dev_info(dev, "%s %d: #%d Get phy data v2 sub_node for %s\n",
			    __func__, __LINE__, index, phy_name_v2);
			ret = __get_phy_parameter_v2(dev, phy_data, sub_node);
			if (ret)
				goto err;
		}
	}

	if (!sub_node)
		goto err;

	if (of_property_read_bool(sub_node, "do_toggle"))
		phy_data->do_toggle = true;
	else
		phy_data->do_toggle = false;

	if (of_property_read_bool(sub_node, "check_efuse"))
		phy_data->check_efuse = true;
	else
		phy_data->check_efuse = false;

	if (of_property_read_bool(sub_node, "use_default_parameter"))
		phy_data->use_default_parameter = true;
	else
		phy_data->use_default_parameter = false;

	if (of_property_read_bool(sub_node,
		    "is_double_sensitivity_mode"))
		phy_data->is_double_sensitivity_mode = true;
	else
		phy_data->is_double_sensitivity_mode = false;

	if (of_property_read_s32(sub_node,
		 "ldo_page0_e4_compensate", &phy_data->ldo_page0_e4_compensate))
		phy_data->ldo_page0_e4_compensate = 0;

err:
	return ret;
}

static int rtk_usb2phy_probe(struct platform_device *pdev)
{
	struct rtk_usb_phy_s *rtk_usb_phy;
	struct device *dev = &pdev->dev;
	int index, ret = 0;
	int port_index, phyN;

	rtk_usb_phy = devm_kzalloc(dev, sizeof(*rtk_usb_phy), GFP_KERNEL);
	if (!rtk_usb_phy)
		return -ENOMEM;

	rtk_usb_phy->dev			= &pdev->dev;
	rtk_usb_phy->phy.dev		= rtk_usb_phy->dev;
	rtk_usb_phy->phy.label		= RTK_USB2PHY_NAME;
	rtk_usb_phy->phy.init		= rtk_usb2_phy_init;
	rtk_usb_phy->phy.shutdown	= rtk_usb2_phy_shutdown;

	if (!dev->of_node) {
		dev_err(dev, "%s %d No device node\n", __func__, __LINE__);
		goto err;
	}

	ret = of_property_read_u32_index(dev->of_node, "port_index", 0,
		    &port_index);
	if (ret)
		port_index = -1;

	ret = of_property_read_u32_index(dev->of_node, "phyN", 0,
		    &phyN);
	if (ret)
		goto err;

	dev_dbg(dev, "%s %d port_index=%d phyN=%d\n",
		    __func__, __LINE__, port_index, phyN);

	rtk_usb_phy->port_index = port_index;
	rtk_usb_phy->phyN = phyN;
	rtk_usb_phy->reg_addr = devm_kzalloc(dev,
		    sizeof(struct reg_addr) * phyN, GFP_KERNEL);
	if (!rtk_usb_phy->reg_addr)
		return -ENOMEM;

	rtk_usb_phy->phy_data = devm_kzalloc(dev,
		    sizeof(struct phy_data) * phyN,
		    GFP_KERNEL);

	if (!rtk_usb_phy->phy_data)
		return -ENOMEM;

	rtk_usb_phy->chip_id = get_rtd_chip_id();
	rtk_usb_phy->chip_revision = get_rtd_chip_revision();

	dev_info(dev, "%s: Chip %x revision is %x\n", __func__,
		    rtk_usb_phy->chip_id, rtk_usb_phy->chip_revision);

	for (index = 0; index < phyN; index++) {
		ret = __get_phy_parameter(rtk_usb_phy, index);
		if (ret) {
			dev_err(dev, "%s %d: __get_phy_parameter fail ret=%d\n",
				    __func__, __LINE__, ret);
			goto err;
		}
	}

	platform_set_drvdata(pdev, rtk_usb_phy);

	ret = usb_add_phy_dev(&rtk_usb_phy->phy);
	if (ret)
		goto err;

#ifdef CONFIG_DYNAMIC_DEBUG
	create_debug_files(rtk_usb_phy);
#endif

	dev_info(&pdev->dev, "%s Probe RTK USB 2.0 PHY\n", __FILE__);
err:
	return ret;
}

static int rtk_usb2phy_remove(struct platform_device *pdev)
{
	struct rtk_usb_phy_s *rtk_usb_phy = platform_get_drvdata(pdev);

#ifdef CONFIG_DYNAMIC_DEBUG
	debugfs_remove_recursive(rtk_usb_phy->debug_dir);
#endif

	usb_remove_phy(&rtk_usb_phy->phy);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id usbphy_rtk_dt_match[] = {
	{ .compatible = "Realtek,usb2phy", },
	{ .compatible = "Realtek,rtd119x-usb2phy", },
	{ .compatible = "Realtek,rtd129x-usb2phy", },
	{},
};
MODULE_DEVICE_TABLE(of, usbphy_rtk_dt_match);
#endif

static struct platform_driver rtk_usb2phy_driver = {
	.probe		= rtk_usb2phy_probe,
	.remove		= rtk_usb2phy_remove,
	.driver		= {
		.name	= RTK_USB2PHY_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(usbphy_rtk_dt_match),
	},
};

module_platform_driver(rtk_usb2phy_driver);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" RTK_USB2PHY_NAME);

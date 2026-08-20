// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Realtek System-on-Chip info
 *
 * Copyright (c) 2017-2019 Andreas Färber
 * Copyright (c) 2019 Realtek Semiconductor Corp.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <soc/realtek/rtk_chip.h>

#define REG_CHIP_ID	0x0
#define REG_CHIP_REV	0x4

#define REALTEK_SOC_CHIP_STR "Realtek,soc-chip"

struct rtd_soc_device {
	struct soc_device *soc_dev;

	const struct rtd_soc *rtd_soc;
	enum rtd_chip_id chip_id;
	enum rtd_chip_revision chip_rev;
	u32 raw_chip_id;
	u32 raw_chip_rev;
};

int get_rtd_chip_id(void)
{
	struct device_node *node;
	struct platform_device *pdev = NULL;
	static struct rtd_soc_device *rtd_soc_dev;
	static enum rtd_chip_id chip_id = CHIP_ID_UNKNOWN;

	if (chip_id != CHIP_ID_UNKNOWN)
		return chip_id;

	if (rtd_soc_dev == NULL) {
		node = of_find_compatible_node(NULL, NULL,
		    REALTEK_SOC_CHIP_STR);
		if (node != NULL)
			pdev = of_find_device_by_node(node);
		if (pdev != NULL)
			rtd_soc_dev = platform_get_drvdata(pdev);
	}
	if (rtd_soc_dev == NULL) {
		pr_err("%s ERROR no rtd_soc_device", __func__);
		return -ENODEV;
	}

	chip_id = rtd_soc_dev->chip_id;
	return chip_id;
}
EXPORT_SYMBOL(get_rtd_chip_id);

int get_rtd_chip_revision(void)
{
	struct device_node *node;
	struct platform_device *pdev = NULL;
	static struct rtd_soc_device *rtd_soc_dev;
	static enum rtd_chip_revision chip_rev = RTD_CHIP_UNKNOWN_REV;

	if (chip_rev != RTD_CHIP_UNKNOWN_REV)
		return chip_rev;

	if (rtd_soc_dev == NULL) {
		node = of_find_compatible_node(NULL, NULL,
		    REALTEK_SOC_CHIP_STR);
		if (node != NULL)
			pdev = of_find_device_by_node(node);
		if (pdev != NULL)
			rtd_soc_dev = platform_get_drvdata(pdev);
	}
	if (rtd_soc_dev == NULL) {
		pr_err("%s ERROR no rtd_soc_device", __func__);
		return -ENODEV;
	}

	chip_rev = rtd_soc_dev->chip_rev;
	return chip_rev;
}
EXPORT_SYMBOL(get_rtd_chip_revision);

struct rtd_soc_revision {
	const char *name;
	u32 raw_chip_rev;
	enum rtd_chip_revision chip_rev;
};

static const struct rtd_soc_revision rtd1195_revisions[] = {
	{ "A", 0x00000000, RTD_CHIP_A00 },
	{ "B", 0x00010000, RTD_CHIP_A01 },
	{ "C", 0x00020000, RTD_CHIP_B00 },
	{ "D", 0x00030000, RTD_CHIP_B01 },
	{ }
};

static const struct rtd_soc_revision rtd1295_revisions[] = {
	{ "A00", 0x00000000 , RTD_CHIP_A00 },
	{ "A01", 0x00010000 , RTD_CHIP_A01 },
	{ "B00", 0x00020000 , RTD_CHIP_B00 },
	{ "B01", 0x00030000 , RTD_CHIP_B01},
	{ }
};

static const struct rtd_soc_revision rtd1395_revisions[] = {
	{ "A00", 0x00000000 , RTD_CHIP_A00 },
	{ "A01", 0x00010000 , RTD_CHIP_A01 },
	{ "A02", 0x00020000 , RTD_CHIP_A02 },
	{ }
};

static const struct rtd_soc_revision rtd1619_revisions[] = {
	{ "A00", 0x00000000 , RTD_CHIP_A00 },
	{ "A01", 0x00010000 , RTD_CHIP_A01 },
	{ }
};

static const struct rtd_soc_revision rtd1319_revisions[] = {
	{ "A00", 0x00000000 , RTD_CHIP_A00 },
	{ "B00", 0x00010000 , RTD_CHIP_B00 },
	{ "B01", 0x00020000 , RTD_CHIP_B01 },
	{ }
};

struct rtd_soc {
	u32 raw_chip_id;
	const char *family;
	const char *(*get_name)(struct device *dev, const struct rtd_soc *s);
	const struct rtd_soc_revision *revisions;
	const char *codename;
};

static const char *rtd119x_name(struct device *dev, const struct rtd_soc *s)
{
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

	rtd_soc_dev->chip_id = CHIP_ID_RTD1195;

	return s->family;
}

static const char *rtd129x_name(struct device *dev, const struct rtd_soc *s)
{
	void __iomem *base;
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

	base = of_iomap(dev->of_node, 2);
	if (base) {
		u32 efuse = readl_relaxed(base);
		iounmap(base);
		dev_info(dev, "efuse: 0x%08x\n", efuse);
		if ((efuse & 0x3) == 0x1) {
			rtd_soc_dev->chip_id = CHIP_ID_RTD1294;
			return "RTD1294";
		}
	}

	base = of_iomap(dev->of_node, 1);
	if (base) {
		u32 chipinfo1 = readl_relaxed(base);
		iounmap(base);
		dev_info(dev, "chipinfo1: 0x%08x\n", chipinfo1);
		if (chipinfo1 & BIT(11)) {
			if (chipinfo1 & BIT(4)) {
				rtd_soc_dev->chip_id = CHIP_ID_RTD1293;
				return "RTD1293";
			}
			rtd_soc_dev->chip_id = CHIP_ID_RTD1296;
			return "RTD1296";
		}
	}

	rtd_soc_dev->chip_id = CHIP_ID_RTD1295;
	return "RTD1295";
}

static const char *rtd139x_name(struct device *dev, const struct rtd_soc *s)
{
	void __iomem *base;
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

	base = of_iomap(dev->of_node, 1);
	if (base) {
		u32 chipinfo1 = readl_relaxed(base);
		iounmap(base);
		dev_info(dev, "chipinfo1: 0x%08x\n", chipinfo1);
		if (chipinfo1 & BIT(12)) {
			rtd_soc_dev->chip_id = CHIP_ID_RTD1392;
			return "RTD1392";
		}
	}

	rtd_soc_dev->chip_id = CHIP_ID_RTD1395;

	return s->family;
}

static const char *rtd161x_name(struct device *dev, const struct rtd_soc *s)
{
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

	rtd_soc_dev->chip_id = CHIP_ID_RTD1619;

	return s->family;
}

static const char *rtd131x_name(struct device *dev, const struct rtd_soc *s)
{
	void __iomem *base;
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

#define OTP_CHIP_MASK 0x000F0000
#define RTD1319 0x00000000
#define RTD1317 0x00010000
#define RTD1315 0x00020000

	base = of_iomap(dev->of_node, 1);
	if (base) {
		u32 efuse = readl_relaxed(base);
		iounmap(base);
		dev_info(dev, "efuse: 0x%08x\n", efuse);
		switch (efuse & OTP_CHIP_MASK) {
		case (RTD1319):
			break;
		case (RTD1317):
			rtd_soc_dev->chip_id = CHIP_ID_RTD1317;
			return "RTD1317";
		case (RTD1315):
			rtd_soc_dev->chip_id = CHIP_ID_RTD1315;
			return "RTD1315";
		default:
			pr_err("%s: Not define chip id 0x%x\n",
				    __func__, efuse);
		}
	}

	rtd_soc_dev->chip_id = CHIP_ID_RTD1319;

	return s->family;
}

static const struct rtd_soc rtd_soc_families[] = {
	{ 0x00006329, "RTD1195", rtd119x_name, rtd1195_revisions, "Phoenix" },
	{ 0x00006421, "RTD1295", rtd129x_name, rtd1295_revisions, "Kylin" },
	{ 0x00006481, "RTD1395", rtd139x_name, rtd1395_revisions, "Hercules" },
	{ 0x00006525, "RTD1619", rtd161x_name, rtd1619_revisions, "Thor" },
	{ 0x00006570, "RTD1319", rtd131x_name, rtd1319_revisions, "Hank" },
};

static const struct rtd_soc *rtd_soc_by_chip_id(u32 raw_chip_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rtd_soc_families); i++) {
		const struct rtd_soc *family = &rtd_soc_families[i];

		if (family->raw_chip_id == raw_chip_id)
			return family;
	}
	return NULL;
}

static const char *rtd_soc_rev(const struct device *dev,
	    const struct rtd_soc *family, u32 raw_chip_rev)
{
	struct rtd_soc_device *rtd_soc_dev = dev_get_drvdata(dev);

	if (family) {
		const struct rtd_soc_revision *rev = family->revisions;

		while (rev && rev->name) {
			if (rev->raw_chip_rev == raw_chip_rev) {
				rtd_soc_dev->chip_rev = rev->chip_rev;
				return rev->name;
			}
			rev++;
		}
	}
	return "unknown";
}

static int rtd_soc_probe(struct platform_device *pdev)
{
	const struct rtd_soc *s;
	struct soc_device_attribute *soc_dev_attr;
	struct soc_device *soc_dev;
	struct rtd_soc_device *rtd_soc_dev;
	struct device_node *node;
	void __iomem *base;
	u32 raw_chip_id, raw_chip_rev;

	base = of_iomap(pdev->dev.of_node, 0);
	if (!base)
		return -ENODEV;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	rtd_soc_dev = kzalloc(sizeof(*rtd_soc_dev), GFP_KERNEL);
	if (!rtd_soc_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, rtd_soc_dev);

	raw_chip_id  = readl_relaxed(base + REG_CHIP_ID);
	raw_chip_rev = readl_relaxed(base + REG_CHIP_REV);

	rtd_soc_dev->raw_chip_id = raw_chip_id;
	rtd_soc_dev->raw_chip_rev = raw_chip_rev;

	node = of_find_node_by_path("/");
	of_property_read_string(node, "model", &soc_dev_attr->machine);
	of_node_put(node);

	s = rtd_soc_by_chip_id(raw_chip_id);
	rtd_soc_dev->rtd_soc = s;

	soc_dev_attr->family = kasprintf(GFP_KERNEL, "Realtek %s",
		(s && s->codename) ? s->codename :
		((s && s->family) ? s->family : "Digital Home Center"));

	if (likely(s && s->get_name))
		soc_dev_attr->soc_id = s->get_name(&pdev->dev, s);
	else
		soc_dev_attr->soc_id = "unknown";

	soc_dev_attr->revision = rtd_soc_rev(&pdev->dev, s, raw_chip_rev);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->family);
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	rtd_soc_dev->soc_dev = soc_dev;

	pr_info("%s %s (0x%08x) rev %s (0x%08x) detected\n",
		soc_dev_attr->family, soc_dev_attr->soc_id, raw_chip_id,
		soc_dev_attr->revision, raw_chip_rev);

	return 0;
}

static int rtd_soc_remove(struct platform_device *pdev)
{
	struct rtd_soc_device *rtd_soc_dev = platform_get_drvdata(pdev);
	struct soc_device *soc_dev = rtd_soc_dev->soc_dev;

	soc_device_unregister(soc_dev);

	return 0;
}

static const struct of_device_id rtd_soc_dt_ids[] = {
	 { .compatible = REALTEK_SOC_CHIP_STR },
	 { }
};

static struct platform_driver rtd_soc_driver = {
	.probe = rtd_soc_probe,
	.remove = rtd_soc_remove,
	.driver = {
		.name = "rtk-soc",
		.of_match_table	= rtd_soc_dt_ids,
	},
};

static int __init rtd_soc_driver_init(void)
{
	return platform_driver_register(&(rtd_soc_driver));
}
subsys_initcall(rtd_soc_driver_init);

static void __exit rtd_soc_driver_exit(void)
{
	platform_driver_unregister(&(rtd_soc_driver));
}

MODULE_DESCRIPTION("Realtek SoC identification");
MODULE_LICENSE("GPL");

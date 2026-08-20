// SPDX-License-Identifier: GPL-2.0-only
/*
 * Power Controller of RTD-1295 SoC
 *
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <dt-bindings/power/rtd1295-power.h>
#include "rtk_pd.h"

static DEFINE_SPINLOCK(crt_power_lock);
static struct rtk_pd_sram sram_ve1 = INIT_RTK_PD_SRAM_NCONT("sram_ve1", 0x380, 0x3a8, 0xf, &crt_power_lock);
static struct rtk_pd_sram sram_ve2 = INIT_RTK_PD_SRAM("sram_ve2", 0x3c0, 0xf, &crt_power_lock);
static struct rtk_pd_sram sram_ve3 = INIT_RTK_PD_SRAM("sram_ve3", 0x3e0, 0xf, &crt_power_lock);
static struct rtk_pd_sram sram_gpu = INIT_RTK_PD_SRAM_NCONT("sram_gpu", 0x394, 0x3ac, 0xf, &crt_power_lock);
static struct rtk_pd_sram sram_nat   = INIT_RTK_PD_SRAM("sram_nat", 0x420,  0xf, &crt_power_lock);
static struct rtk_pd_simple iso_ve1 = INIT_RTK_PD_ISO("iso_ve1", 0x400,  0, &crt_power_lock);
static struct rtk_pd_simple iso_ve2 = INIT_RTK_PD_ISO("iso_ve2", 0x400,  4, &crt_power_lock);
static struct rtk_pd_simple iso_ve3 = INIT_RTK_PD_ISO("iso_ve3", 0x400,  6, &crt_power_lock);
static struct rtk_pd_simple iso_gpu = INIT_RTK_PD_ISO("iso_gpu", 0x400,  1, &crt_power_lock);
static struct rtk_pd_simple iso_nat = INIT_RTK_PD_ISO("iso_nat", 0x400, 18, &crt_power_lock);
static struct rtk_pd pd_ve1 = INIT_RTK_PD("pd_ve1", 0, NULL);
static struct rtk_pd pd_ve2 = INIT_RTK_PD("pd_ve2", 0, NULL);
static struct rtk_pd pd_ve3 = INIT_RTK_PD("pd_ve3", 0, NULL);
static struct rtk_pd pd_gpu = INIT_RTK_PD("pd_gpu", 0, NULL);
static struct rtk_pd pd_nat = INIT_RTK_PD("pd_nat", 0, NULL);

static struct generic_pm_domain *rtd1295_domains[RTD1295_PD_MAX] = {
	[RTD1295_PD_VE1]      = &pd_ve1.pd,
	[RTD1295_PD_VE2]      = &pd_ve2.pd,
	[RTD1295_PD_VE3]      = &pd_ve3.pd,
	[RTD1295_PD_GPU]      = &pd_gpu.pd,
	[RTD1295_PD_NAT]      = &pd_nat.pd,
	[RTD1295_PD_SRAM_VE1] = &sram_ve1.core.pd,
	[RTD1295_PD_SRAM_VE2] = &sram_ve2.core.pd,
	[RTD1295_PD_SRAM_VE3] = &sram_ve3.core.pd,
	[RTD1295_PD_SRAM_GPU] = &sram_gpu.core.pd,
	[RTD1295_PD_SRAM_NAT] = &sram_nat.core.pd,
	[RTD1295_PD_ISO_VE1]  = &iso_ve1.core.pd,
	[RTD1295_PD_ISO_VE2]  = &iso_ve2.core.pd,
	[RTD1295_PD_ISO_VE3]  = &iso_ve3.core.pd,
	[RTD1295_PD_ISO_GPU]  = &iso_gpu.core.pd,
	[RTD1295_PD_ISO_NAT]  = &iso_nat.core.pd,
};

static int rtd1295_domain_map[][2] = {
	{ RTD1295_PD_SRAM_VE1, RTD1295_PD_ISO_VE1, },
	{ RTD1295_PD_SRAM_VE2, RTD1295_PD_ISO_VE2, },
	{ RTD1295_PD_SRAM_VE3, RTD1295_PD_ISO_VE3, },
	{ RTD1295_PD_SRAM_GPU, RTD1295_PD_ISO_GPU, },
	{ RTD1295_PD_SRAM_NAT, RTD1295_PD_ISO_NAT, },
	{ RTD1295_PD_ISO_VE1,  RTD1295_PD_VE1,     },
	{ RTD1295_PD_ISO_VE2,  RTD1295_PD_VE2,     },
	{ RTD1295_PD_ISO_VE3,  RTD1295_PD_VE3,     },
	{ RTD1295_PD_ISO_GPU,  RTD1295_PD_GPU,     },
	{ RTD1295_PD_ISO_NAT,  RTD1295_PD_NAT,     },
	{ RTD1295_PD_VE2,      RTD1295_PD_VE1,     },
};

static void rtd1295_setup_sysconfig(void)
{
}

static int rtd1295_power_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rtk_pd_device *pd_dev;
	int ret;

	rtd1295_setup_sysconfig();

	pd_dev = devm_kzalloc(dev, sizeof(*pd_dev), GFP_KERNEL);
	if (!pd_dev)
		return -ENOMEM;

	pd_dev->dev = dev;
	pd_dev->regmap = syscon_node_to_regmap(np->parent);
	if (IS_ERR(pd_dev->regmap)) {
		ret = PTR_ERR(pd_dev->regmap);
		dev_err(dev, "failed to get syscon: %d\n", ret);
		return ret;
	}
	INIT_LIST_HEAD(&pd_dev->list);

	rtk_pd_device_add_domains(pd_dev, rtd1295_domains, ARRAY_SIZE(rtd1295_domains));
	rtk_pd_setup_power_tree(pd_dev, rtd1295_domain_map, ARRAY_SIZE(rtd1295_domain_map));

	pd_dev->of_provider_data.domains = rtd1295_domains;
	pd_dev->of_provider_data.num_domains = ARRAY_SIZE(rtd1295_domains);
	ret = of_genpd_add_provider_onecell(np, &pd_dev->of_provider_data);
	WARN(ret, "of_genpd_add_provider_onecell() returns %d\n", ret);

	dev_set_drvdata(dev, pd_dev);
	return 0;
}

static const struct of_device_id rtd1295_power_match[] = {
	{ .compatible = "realtek,rtd1295-power" },
	{}
};

static struct platform_driver rtd1295_power_driver = {
	.probe = rtd1295_power_probe,
	.driver = {
		.name = "rtk-rtd1295-power",
		.of_match_table = of_match_ptr(rtd1295_power_match),
		.pm = &rtk_pd_generic_pm_ops,
	},
};

static int __init rtd1295_power_init(void)
{
	return platform_driver_register(&rtd1295_power_driver);
}
arch_initcall(rtd1295_power_init);

MODULE_DESCRIPTION("Realtek RTD1295 Power Controller");
MODULE_AUTHOR("Cheng-Yu Lee <cylee12@realtek.com>");
MODULE_LICENSE("GPL v2");


// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2020 Realtek Semiconductor Corporation
 * Author: Cheng-Yu Lee <cylee12@realtek.com>
 */

#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include "rtk_pd.h"

static int rtk_genpd_power_on(struct generic_pm_domain *genpd)
{
	struct rtk_pd *pd = gen_pd_to_rtk_pd(genpd);

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	rtk_pd_power_on(pd);

	return 0;
}

static int rtk_genpd_power_off(struct generic_pm_domain *genpd)
{
	struct rtk_pd *pd = gen_pd_to_rtk_pd(genpd);

	pr_debug("%s: %s\n", rtk_pd_name(pd), __func__);
	rtk_pd_power_off(pd);

	return 0;
}

static int rtk_genpd_attach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	pr_debug("%s: attach %s %s\n", genpd->name, dev_driver_string(dev), dev_name(dev));
	return 0;
}

static void rtk_genpd_detach_dev(struct generic_pm_domain *genpd, struct device *dev)
{
	pr_debug("%s: detach %s %s\n", genpd->name, dev_driver_string(dev), dev_name(dev));
}

int rtk_pd_init(struct rtk_pd_device *pd_dev, struct rtk_pd *pd)
{
	int is_off = 1;
	struct dev_power_governor *gov = rtk_pd_get_gov(pd);

	pd->pd_dev = pd_dev;
	is_off = !rtk_pd_power_state(pd);

	pr_debug("%s: %s: default power state: %s\n", rtk_pd_name(pd), __func__,
		is_off ? "off" : "on");

	pd->pd.attach_dev = rtk_genpd_attach_dev;
	pd->pd.detach_dev = rtk_genpd_detach_dev;
	pd->pd.power_on   = rtk_genpd_power_on;
	pd->pd.power_off  = rtk_genpd_power_off;

	pm_genpd_init(&pd->pd, gov, is_off);
	list_add(&pd->list, &pd_dev->list);
	return 0;
}

int rtk_pd_device_add_domains(struct rtk_pd_device *pd_dev, struct generic_pm_domain **domains, int num_domains)
{
	struct generic_pm_domain *domain;
	int i;
	int ret;

	pd_dev->domains = domains;
	pd_dev->num_domains = num_domains;

	for (i = 0; i < num_domains; i++) {
		domain = domains[i];
		if (!domain)
			continue;

		ret = rtk_pd_init(pd_dev, gen_pd_to_rtk_pd(domain));
		WARN(ret, "rtk_pd_init() returns %d\n", ret);
	}
	return 0;
}

int rtk_pd_setup_power_tree(struct rtk_pd_device *pd_dev, int map[][2], int num_maps)
{
	struct generic_pm_domain **domains = pd_dev->domains;
	int i;
	int id, sub_id;
	int ret;

	for (i = 0; i < num_maps; i++) {
		id = map[i][0];
		sub_id = map[i][1];

		ret = pm_genpd_add_subdomain(domains[id], domains[sub_id]);
		WARN(ret, "pm_genpd_add_subdomain() returns %d\n", ret);
	}
	return 0;
}

void rtk_pd_device_show_power_state(struct rtk_pd_device *pd_dev)
{
	struct rtk_pd *p;

	dev_info(pd_dev->dev, "list power state:\n");
	list_for_each_entry(p, &pd_dev->list, list) {
		dev_info(pd_dev->dev, "  %s: state=%d\n", rtk_pd_name(p), rtk_pd_power_state(p));
	}
}

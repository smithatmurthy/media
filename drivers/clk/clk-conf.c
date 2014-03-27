/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/printk.h>

/**
 * of_clk_device_init() - parse and set clk configuration assigned to a device
 * @node: device node to apply the configuration for
 *
 * This function parses 'clock-parents' and 'clock-rates' properties and sets
 * any specified clock parents and rates.
 */
int of_clk_device_init(struct device_node *node)
{
	struct property	*prop;
	const __be32 *cur;
	int rc, index, num_parents;
	struct clk *clk, *pclk;
	u32 rate;

	num_parents = of_count_phandle_with_args(node, "clock-parents",
						 "#clock-cells");
	if (num_parents == -EINVAL)
		pr_err("clk: Invalid value of clock-parents property at %s\n",
		       node->full_name);

	for (index = 0; index < num_parents; index++) {
		pclk = of_clk_get_by_property(node, "clock-parents", index);
		if (IS_ERR(pclk)) {
			/* skip empty (null) phandles */
			if (PTR_ERR(pclk) == -ENOENT)
				continue;

			pr_warn("clk: couldn't get parent clock %d for %s\n",
				index, node->full_name);
			return PTR_ERR(pclk);
		}

		clk = of_clk_get(node, index);
		if (IS_ERR(clk)) {
			pr_warn("clk: couldn't get clock %d for %s\n",
				index, node->full_name);
			return PTR_ERR(clk);
		}

		rc = clk_set_parent(clk, pclk);
		if (rc < 0)
			pr_err("clk: failed to reparent %s to %s: %d\n",
			       __clk_get_name(clk), __clk_get_name(pclk), rc);
		else
			pr_debug("clk: set %s as parent of %s\n",
				 __clk_get_name(pclk), __clk_get_name(clk));
	}

	index = 0;
	of_property_for_each_u32(node, "clock-rates", prop, cur, rate) {
		if (rate) {
			clk = of_clk_get(node, index);
			if (IS_ERR(clk)) {
				pr_warn("clk: couldn't get clock %d for %s\n",
					index, node->full_name);
				return PTR_ERR(clk);
			}

			rc = clk_set_rate(clk, rate);
			if (rc < 0)
				pr_err("clk: couldn't set %s clock rate: %d\n",
				       __clk_get_name(clk), rc);
			else
				pr_debug("clk: set rate of clock %s to %lu\n",
					 __clk_get_name(clk), clk_get_rate(clk));
		}
		index++;
	}

	return 0;
}

/*
 * LED flash manager of helpers.
 *
 *	Copyright (C) 2014 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#include <linux/device.h>
#include <linux/led-class-flash.h>
#include <linux/of.h>
#include <linux/of_led_flash_manager.h>
#include <linux/slab.h>

static int __parse_strobe_gate_node(struct led_classdev_flash *flash,
				    struct device_node *node,
				    struct list_head *gates)
{
	struct device_node *mux_node, *subnode;
	struct led_flash_strobe_gate *gate;
	struct led_classdev *led_cdev = &flash->led_cdev;
	u32 line_id;
	int ret, num_gates = 0;

	/* Get node mux node */
	mux_node = of_parse_phandle(node, "mux", 0);
	if (!mux_node)
		return -EINVAL;

	/* Get the value the mux has to be written to open the gate */
	ret = of_property_read_u32(node, "mux-line-id", &line_id);
	if (ret < 0)
		return ret;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	gate->mux_node = mux_node;
	gate->line_id = line_id;
	list_add_tail(&gate->list, gates);

	/* Parse nested gate nodes */
	for_each_available_child_of_node(node, subnode) {
		if (!of_node_ncmp(subnode->name, "gate", 4)) {
			ret = __parse_strobe_gate_node(flash, subnode, gates);
			if (ret < 0) {
				dev_dbg(led_cdev->dev->parent,
				"Failed to parse gate node (%d)\n",
				ret);
				goto err_parse_gate;
			}

			if (++num_gates > 1) {
				dev_dbg(led_cdev->dev->parent,
				"Only one available child gate is allowed.\n");
				ret = -EINVAL;
				goto err_parse_gate;
			}
		}
	}

	return 0;

err_parse_gate:
	kfree(gate);

	return ret;
}

static int __parse_strobe_provider_node(struct led_classdev_flash *flash,
			       struct device_node *node)
{
	struct device_node *provider_node;
	struct led_flash_strobe_provider *provider;
	int ret;

	/* Create strobe provider representation */
	provider = kzalloc(sizeof(*provider), GFP_KERNEL);
	if (!provider)
		return -ENOMEM;

	/* Get phandle of the device generating strobe source signal */
	provider_node = of_parse_phandle(node, "strobe-provider", 0);

	/* provider property may be absent */
	if (provider_node) {
		/* Use compatible property as a strobe provider name */
		ret = of_property_read_string(provider_node, "compatible",
					(const char **) &provider->name);
		if (ret < 0)
			goto err_read_name;
	}

	INIT_LIST_HEAD(&provider->strobe_gates);

	list_add_tail(&provider->list, &flash->strobe_providers);

	ret = __parse_strobe_gate_node(flash, node, &provider->strobe_gates);
	if (ret < 0)
		goto err_read_name;

	return 0;

err_read_name:
	kfree(provider);

	return ret;
}

int of_led_flash_manager_parse_dt(struct led_classdev_flash *flash,
				  struct device_node *node)
{
	struct device_node *subnode;
	int ret;

	for_each_available_child_of_node(node, subnode) {
		if (!of_node_cmp(subnode->name, "gate-software-strobe")) {
			ret = __parse_strobe_gate_node(flash, subnode,
						&flash->software_strobe_gates);
			if (ret < 0)
				return ret;
		}
		if (!of_node_ncmp(subnode->name, "gate-external-strobe", 20)) {
			ret = __parse_strobe_provider_node(flash, subnode);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_led_flash_manager_parse_dt);

void of_led_flash_manager_release_dt_data(struct led_classdev_flash *flash)
{
	struct list_head *gate_list;
	struct led_flash_strobe_gate *gate, *gn;
	struct led_flash_strobe_provider *provider, *sn;

	gate_list = &flash->software_strobe_gates;
	list_for_each_entry_safe(gate, gn, gate_list, list)
		kfree(gate);

	list_for_each_entry_safe(provider, sn, &flash->strobe_providers, list) {
		list_for_each_entry_safe(gate, gn, &provider->strobe_gates,
								list)
			kfree(gate);
		kfree(provider);
	}
}
EXPORT_SYMBOL_GPL(of_led_flash_manager_release_dt_data);

/*
 * Exemplary driver showing usage of the Flash Manager API
 * for registering/unregistering asynchronous multiplexers.
 *
 *	Copyright (C) 2014, Samsung Electronics Co., Ltd.
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/led-class-flash.h>
#include <linux/led-flash-manager.h>
#include <linux/leds.h>
#include <linux/of.h>

static int led_async_mux_select_line(u32 line_id, void *mux)
{
	pr_info("led_async_mux_select_line line_id: %d\n", line_id);
	return 0;
}

struct led_flash_mux_ops mux_ops = {
	.select_line = led_async_mux_select_line,
};

static int led_async_mux_probe(struct platform_device *pdev)
{
	struct led_flash_mux mux;

	mux.ops = &mux_ops;
	mux.owner = THIS_MODULE;
	mux.node = pdev->dev->of_node;

	return led_flash_manager_bind_async_mux(&mux);
}

static int led_async_mux_remove(struct platform_device *pdev)
{
	return led_flash_manager_unbind_async_mux(pdev->dev->of_node);
}

static struct of_device_id led_async_mux_dt_match[] = {
	{.compatible = "led-async-mux"},
	{},
};

static struct platform_driver led_async_mux_driver = {
	.probe		= led_async_mux_probe,
	.remove		= led_async_mux_remove,
	.driver		= {
		.name		= "led-async-mux",
		.owner		= THIS_MODULE,
		.of_match_table = led_async_mux_dt_match,
	},
};

module_platform_driver(led_async_mux_driver);

MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_DESCRIPTION("LED async mux");
MODULE_LICENSE("GPL");

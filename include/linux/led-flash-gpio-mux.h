/*
 * LED Flash Class gpio mux
 *
 *	Copyright (C) 2014 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#ifndef _LED_FLASH_GPIO_MUX_H
#define _LED_FLASH_GPIO_MUX_H

struct device_node;

struct list_head;

/**
 * struct led_flash_gpio_mux_selector - element of gpio mux selectors list
 * @list: links mux selectors
 * @gpio: mux selector gpio
 */
struct led_flash_gpio_mux_selector {
	struct list_head list;
	int gpio;
};

/**
 * struct led_flash_gpio_mux - gpio mux
 * @selectors:	mux selectors
 */
struct led_flash_gpio_mux {
	struct list_head selectors;
};

/**
 * led_flash_gpio_mux_create - create gpio mux
 * @new_mux: created mux
 * @mux_node: device tree node with gpio definitions
 *
 * Create V4L2 subdev wrapping given LED subsystem device.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_flash_gpio_mux_create(struct led_flash_gpio_mux **new_mux,
			struct device_node *mux_node);

/**
 * led_flash_gpio_mux_release - release gpio mux
 * @gpio_mux: mux to be released
 *
 * Create V4L2 subdev wrapping given LED subsystem device.
 */
void led_flash_gpio_mux_release(void *gpio_mux);

/**
 * led_flash_gpio_mux_select_line - select mux line
 * @line_id: id of the line to be selected
 * @mux: mux to be set
 *
 * Create V4L2 subdev wrapping given LED subsystem device.
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_flash_gpio_mux_select_line(u32 line_id, void *mux);

#endif /* _LED_FLASH_GPIO_MUX_H */

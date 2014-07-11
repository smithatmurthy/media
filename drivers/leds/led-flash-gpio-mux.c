/*
 * LED Flash Class gpio mux
 *
 *      Copyright (C) 2014 Samsung Electronics Co., Ltd
 *      Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#include <linux/gpio.h>
#include <linux/led-flash-gpio-mux.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>

int led_flash_gpio_mux_select_line(u32 line_id, void *mux)
{
	struct led_flash_gpio_mux *gpio_mux = (struct led_flash_gpio_mux *) mux;
	struct led_flash_gpio_mux_selector *sel;
	u32 mask = 1;

	/* Setup selectors */
	list_for_each_entry(sel, &gpio_mux->selectors, list) {
		gpio_set_value(sel->gpio, !!(line_id & mask));
		mask <<= 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(led_flash_gpio_mux_select_line);

/* Create standard gpio mux */
int led_flash_gpio_mux_create(struct led_flash_gpio_mux **new_mux,
			      struct device_node *mux_node)
{
	struct led_flash_gpio_mux *mux;
	struct led_flash_gpio_mux_selector *sel;
	int gpio_num, gpio, ret, i;
	char gpio_name[20];
	static int cnt_gpio;

	/* Get the number of mux selectors */
	gpio_num = of_gpio_count(mux_node);
	if (gpio_num == 0)
		return -EINVAL;

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	INIT_LIST_HEAD(&mux->selectors);

	/* Request gpios for all selectors */
	for (i = 0; i < gpio_num; ++i) {
		gpio = of_get_gpio(mux_node, i);
		if (gpio_is_valid(gpio)) {
			sprintf(gpio_name, "v4l2_mux selector %d", cnt_gpio++);
			ret = gpio_request_one(gpio, GPIOF_DIR_OUT, gpio_name);
			if (ret < 0)
				goto err_gpio_request;

			/* Add new entry to the gpio selectors list */
			sel = kzalloc(sizeof(*sel), GFP_KERNEL);
			if (!sel) {
				ret = -ENOMEM;
				goto err_gpio_request;
			}
			sel->gpio = gpio;

			list_add_tail(&sel->list, &mux->selectors);
		} else {
			ret = -EINVAL;
			goto err_gpio_request;
		}

	}

	*new_mux = mux;

	return 0;

err_gpio_request:
	led_flash_gpio_mux_release(mux);

	return ret;
}
EXPORT_SYMBOL_GPL(led_flash_gpio_mux_create);

void led_flash_gpio_mux_release(void *mux)
{
	struct led_flash_gpio_mux *gpio_mux = (struct led_flash_gpio_mux *) mux;
	struct led_flash_gpio_mux_selector *sel, *n;

	list_for_each_entry_safe(sel, n, &gpio_mux->selectors, list) {
		if (gpio_is_valid(sel->gpio))
			gpio_free(sel->gpio);
		kfree(sel);
	}
	kfree(gpio_mux);
}
EXPORT_SYMBOL_GPL(led_flash_gpio_mux_release);

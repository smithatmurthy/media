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

#ifndef _OF_LED_FLASH_MANAGER_H
#define _OF_LED_FLASH_MANAGER_H

#include <linux/sysfs.h>
#include <linux/device.h>

struct led_classdev_flash;
struct device_node;
struct list_head;

#define MAX_ATTR_NAME 18 /* allows for strobe providers ids up to 999 */

/**
 * struct led_flash_strobe_gate - strobe signal gate
 * @list:	links gates
 * @mux_node:	gate's parent mux
 * @line_id:	id of a mux line that the gate represents
 */
struct led_flash_strobe_gate {
	struct list_head list;
	struct device_node *mux_node;
	u32 line_id;
};

/**
 * struct led_flash_strobe_provider - external strobe signal provider that
				      may be associated with the flash device
 * @list:		links strobe providers
 * @strobe_gates:	list of gates that route strobe signal
			from the strobe provider to the flash device
 * @node:		device node of the strobe provider device
 */
struct led_flash_strobe_provider {
	struct list_head list;
	struct list_head strobe_gates;
	const char *name;
	struct device_attribute attr;
	char attr_name[MAX_ATTR_NAME];
	bool attr_registered;
};

/**
 * of_led_flash_manager_parse_dt - parse flash manager data of
 *				   a LED Flash Class device DT node
 *
 * @flash: the LED Flash Class device to store the parsed data in
 * @node: device node to parse
 *
 * Parse the multiplexers' settings that need to be applied for
 * opening a route to all the flash strobe signals available for
 * the device.
 *
 * Returns: 0 on success or negative error value on failure
 */
int of_led_flash_manager_parse_dt(struct led_classdev_flash *flash,
					 struct device_node *node);


/**
 * of_led_flash_manager_release_dt_data - release parsed DT data
 *
 * @flash: the LED Flash Class device to release data from
 *
 * Release data structures containing information about strobe
 * source routes available for the device.
 */
void of_led_flash_manager_release_dt_data(struct led_classdev_flash *flash);

#endif /* _OF_LED_FLASH_MANAGER_H */

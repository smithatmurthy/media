/*
 *  LED Flash Manager for maintaining LED Flash Class devices
 *  along with their corresponding muxes that faciliate dynamic
 *  reconfiguration of the strobe signal source.
 *
 *	Copyright (C) 2014 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#ifndef _LED_FLASH_MANAGER_H
#define _LED_FLASH_MANAGER_H

#include <linux/types.h>

struct list_head;

struct led_flash_mux_ops {
	/* select mux line */
	int (*select_line)(u32 line_id, void *mux);
	/*
	 * release private mux data - it is intended for gpio muxes
	 * only and mustn't be initialized by asynchronous muxes.
	 */
	void (*release_private_data)(void *mux);
};

/**
 * struct led_flash_mux_ref - flash mux reference
 * @list:	links flashes pointing to a mux
 * @flash:	flash holding a mux reference
 */
struct led_flash_mux_ref {
	struct list_head list;
	struct led_classdev_flash *flash;
};

/**
 * struct led_flash_mux - flash mux used for routing strobe signal
 * @list:		list of muxes declared by registered flashes
 * @node:		mux Device Tree node
 * @private_data:	mux internal data
 * @ops:		ops facilitating mux state manipulation
 * @refs:		list of flashes using the mux
 * @num_refs:		number of flashes using the mux
 * @owner:		module owning an async mux driver
 */
struct led_flash_mux {
	struct list_head list;
	struct device_node *node;
	void *private_data;
	struct led_flash_mux_ops *ops;
	struct list_head refs;
	int num_refs;
	struct module *owner;
};

/**
 * led_flash_manager_setup_strobe - setup flash strobe
 * @flash: the LED Flash Class device to strobe
 * @external: true - setup external strobe,
 *	      false - setup software strobe
 *
 * Configure muxes to route relevant strobe signals to the
 * flash led device and strobe the flash if software strobe
 * is to be activated. If the flash device depends on shared
 * muxes the caller is blocked for the flash_timeout period.
 *
 * Returns: 0 on success or negative error value on failure.
 */
int led_flash_manager_setup_strobe(struct led_classdev_flash *flash,
				   bool external);

/**
 * led_flash_manager_bind_async_mux - bind asynchronous mulitplexer
 * @async_mux: mux registration data
 *
 * Notify the flash manager that an asychronous mux has been probed.
 *
 * Returns: 0 on success or negative error value on failure.
 */
int led_flash_manager_bind_async_mux(struct led_flash_mux *async_mux);

/**
 * led_flash_manager_unbind_async_mux - unbind asynchronous mulitplexer
 * @mux_node: device node of the mux to be unbound
 *
 * Notify the flash manager that an asychronous mux has been removed.
 *
 * Returns: 0 on success or negative error value on failure.
 */
int led_flash_manager_unbind_async_mux(struct device_node *mux_node);

/**
 * led_flash_manager_register_flash - register LED Flash Class device
				      in the flash manager
 * @flash: the LED Flash Class device to be registered
 * @node: Device Tree node - it is expected to contain information
 *	  about strobe signal topology
 *
 * Register LED Flash Class device and retrieve information
 * about related strobe signals topology.
 *
 * Returns: 0 on success or negative error value on failure.
 */
int led_flash_manager_register_flash(struct led_classdev_flash *flash,
				     struct device_node *node);

/**
 * led_flash_manager_unregister_flash - unregister LED Flash Class device
 *					from the flash manager
 * @flash: the LED Flash Class device to be unregistered
 *
 * Unregister LED Flash Class device from the flash manager.
 */
void led_flash_manager_unregister_flash(struct led_classdev_flash *flash);

#endif /* _LED_FLASH_MANAGER_H */

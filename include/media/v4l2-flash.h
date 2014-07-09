/*
 * V4L2 Flash LED sub-device registration helpers.
 *
 *	Copyright (C) 2014 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#ifndef _V4L2_FLASH_H
#define _V4L2_FLASH_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>

struct led_classdev_flash;
struct led_classdev;
enum led_brightness;

struct v4l2_flash_ops {
	int (*torch_brightness_set)(struct led_classdev *led_cdev,
					enum led_brightness brightness);
	int (*torch_brightness_update)(struct led_classdev *led_cdev);
	int (*flash_brightness_set)(struct led_classdev_flash *flash,
					u32 brightness);
	int (*flash_brightness_update)(struct led_classdev_flash *flash);
	int (*strobe_set)(struct led_classdev_flash *flash, bool state);
	int (*strobe_get)(struct led_classdev_flash *flash, bool *state);
	int (*timeout_set)(struct led_classdev_flash *flash, u32 timeout);
	int (*indicator_brightness_set)(struct led_classdev_flash *flash,
					u32 brightness);
	int (*indicator_brightness_update)(struct led_classdev_flash *flash);
	int (*external_strobe_set)(struct led_classdev_flash *flash,
					bool enable);
	int (*fault_get)(struct led_classdev_flash *flash, u32 *fault);
	void (*sysfs_lock)(struct led_classdev *led_cdev);
	void (*sysfs_unlock)(struct led_classdev *led_cdev);
};

/**
 * struct v4l2_flash_ctrl - controls that define the sub-dev's state
 * @source:		V4L2_CID_FLASH_STROBE_SOURCE control
 * @led_mode:		V4L2_CID_FLASH_LED_MODE control
 * @torch_intensity:	V4L2_CID_FLASH_TORCH_INTENSITY control
 */
struct v4l2_flash_ctrl {
	struct v4l2_ctrl *source;
	struct v4l2_ctrl *led_mode;
	struct v4l2_ctrl *torch_intensity;
};

/**
 * struct v4l2_flash_ctrl_config - V4L2 Flash controls initialization data
 * @torch_intensity:		V4L2_CID_FLASH_TORCH_INTENSITY constraints
 * @flash_intensity:		V4L2_CID_FLASH_INTENSITY constraints
 * @indicator_intensity:	V4L2_CID_FLASH_INDICATOR_INTENSITY constraints
 * @flash_timeout:		V4L2_CID_FLASH_TIMEOUT constraints
 * @flash_fault:		possible flash faults
 */
struct v4l2_flash_ctrl_config {
	struct v4l2_ctrl_config torch_intensity;
	struct v4l2_ctrl_config flash_intensity;
	struct v4l2_ctrl_config indicator_intensity;
	struct v4l2_ctrl_config flash_timeout;
	u32 flash_faults;
};

/**
 * struct v4l2_flash - Flash sub-device context
 * @flash:		LED Flash Class device controlled by this sub-device
 * @ops:		LED Flash Class device ops
 * @sd:			V4L2 sub-device
 * @hdl:		flash controls handler
 * @ctrl:		state defining controls
 * @config:		V4L2 Flash controlsrconfiguration data
 * @software_strobe_gates: route to the software strobe signal
 * @external_strobe_gates: route to the external strobe signal
 * @sensors:		available external strobe sources
 */
struct v4l2_flash {
	struct led_classdev_flash *flash;
	const struct v4l2_flash_ops *ops;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_flash_ctrl ctrl;
	struct v4l2_flash_ctrl_config config;
	char **strobe_providers_menu;
};

static inline struct v4l2_flash *v4l2_subdev_to_v4l2_flash(
							struct v4l2_subdev *sd)
{
	return container_of(sd, struct v4l2_flash, sd);
}

static inline struct v4l2_flash *v4l2_ctrl_to_v4l2_flash(struct v4l2_ctrl *c)
{
	return container_of(c->handler, struct v4l2_flash, hdl);
}

#ifdef CONFIG_V4L2_FLASH_LED_CLASS
/**
 * v4l2_flash_init - initialize V4L2 flash led sub-device
 * @led_fdev:	the LED Flash Class device to wrap
 * @config:	initialization data for V4L2 Flash controls
 * @flash_ops:	V4L2 Flash device ops
 * @out_flash:	handler to the new V4L2 Flash device
 *
 * Create V4L2 subdev wrapping given LED subsystem device.

 * Returns: 0 on success or negative error value on failure
 */
int v4l2_flash_init(struct led_classdev_flash *led_fdev,
		    struct v4l2_flash_ctrl_config *config,
		    const struct v4l2_flash_ops *flash_ops,
		    struct v4l2_flash **out_flash);

/**
 * v4l2_flash_release - release V4L2 Flash sub-device
 * @flash: the V4L2 Flash device to release
 *
 * Release V4L2 flash led subdev.
 */
void v4l2_flash_release(struct v4l2_flash *v4l2_flash);

#else
#define v4l2_flash_init(led_cdev, config, flash_ops, out_flash) (0)
#define v4l2_flash_release(v4l2_flash)
#endif /* CONFIG_V4L2_FLASH_LED_CLASS */

#endif /* _V4L2_FLASH_H */

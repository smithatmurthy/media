/*
 * LED Flash Class interface
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_FLASH_LEDS_H_INCLUDED
#define __LINUX_FLASH_LEDS_H_INCLUDED

#include <linux/leds.h>

struct v4l2_flash_ops;
struct led_classdev_flash;
struct device_node;

/*
 * Supported led fault bits - must be kept in synch
 * with V4L2_FLASH_FAULT bits.
 */
#define LED_FAULT_OVER_VOLTAGE		 V4L2_FLASH_FAULT_OVER_VOLTAGE
#define LED_FAULT_TIMEOUT		 V4L2_FLASH_FAULT_TIMEOUT
#define LED_FAULT_OVER_TEMPERATURE	 V4L2_FLASH_FAULT_OVER_TEMPERATURE
#define LED_FAULT_SHORT_CIRCUIT		 V4L2_FLASH_FAULT_SHORT_CIRCUIT
#define LED_FAULT_OVER_CURRENT		 V4L2_FLASH_FAULT_OVER_CURRENT
#define LED_FAULT_INDICATOR		 V4L2_FLASH_FAULT_INDICATOR
#define LED_FAULT_UNDER_VOLTAGE		 V4L2_FLASH_FAULT_UNDER_VOLTAGE
#define LED_FAULT_INPUT_VOLTAGE		 V4L2_FLASH_FAULT_INPUT_VOLTAGE
#define LED_FAULT_LED_OVER_TEMPERATURE	 V4L2_FLASH_OVER_TEMPERATURE

#define LED_FLASH_MAX_SYSFS_GROUPS 6

struct led_flash_ops {
	/* set flash brightness */
	int (*flash_brightness_set)(struct led_classdev_flash *flash,
					u32 brightness);
	/* get flash brightness */
	int (*flash_brightness_get)(struct led_classdev_flash *flash,
					u32 *brightness);
	/* set flash indicator brightness */
	int (*indicator_brightness_set)(struct led_classdev_flash *flash,
					u32 brightness);
	/* get flash indicator brightness */
	int (*indicator_brightness_get)(struct led_classdev_flash *flash,
					u32 *brightness);
	/* set flash strobe state */
	int (*strobe_set)(struct led_classdev_flash *flash, bool state);
	/* get flash strobe state */
	int (*strobe_get)(struct led_classdev_flash *flash, bool *state);
	/* set flash timeout */
	int (*timeout_set)(struct led_classdev_flash *flash, u32 timeout);
	/* setup the device to strobe the flash upon a pin state assertion */
	int (*external_strobe_set)(struct led_classdev_flash *flash,
					bool enable);
	/* get the flash LED fault */
	int (*fault_get)(struct led_classdev_flash *flash, u32 *fault);
};

/*
 * Current value of a flash setting along
 * with its constraints.
 */
struct led_flash_setting {
	/* maximum allowed value */
	u32 min;
	/* maximum allowed value */
	u32 max;
	/* step value */
	u32 step;
	/* current value */
	u32 val;
};

/*
 * Aggregated flash settings - designed for ease
 * of passing initialization data to the clients
 * wrapping a LED Flash class device.
 */
struct led_flash_config {
	struct led_flash_setting torch_brightness;
	struct led_flash_setting flash_brightness;
	struct led_flash_setting indicator_brightness;
	struct led_flash_setting flash_timeout;
	u32 flash_faults;
};

struct led_classdev_flash {
	/* led-flash-manager uses it to link flashes */
	struct list_head list;
	/* led class device */
	struct led_classdev led_cdev;
	/* flash led specific ops */
	const struct led_flash_ops *ops;

	/* flash sysfs groups */
	struct attribute_group *sysfs_groups[LED_FLASH_MAX_SYSFS_GROUPS];

	/* flash brightness value in microamperes along with its constraints */
	struct led_flash_setting brightness;

	/* timeout value in microseconds along with its constraints */
	struct led_flash_setting timeout;

	/*
	 * Indicator brightness value in microamperes along with
	 * its constraints - this is an optional setting and must
	 * be allocated by the driver if the device supports privacy
	 * indicator led.
	 */
	struct led_flash_setting *indicator_brightness;

	/*
	 * determines if a device supports external
	 * flash strobe sources
	 */
	bool has_external_strobe;

	/* If true the flash led is strobed from external source */
	bool external_strobe;

	/* Flash manager data */
	/* Strobe signals topology data */
	struct list_head software_strobe_gates;
	struct list_head strobe_providers;

	/* identifier of the selected strobe signal provider */
	int strobe_provider_id;

	/* number of defined strobe providers */
	int num_strobe_providers;

	/*
	 * number of muxes that this device shares
	 * with other LED Flash Class devices.
	 */
	int num_shared_muxes;
};

static inline struct led_classdev_flash *lcdev_to_flash(
						struct led_classdev *lcdev)
{
	return container_of(lcdev, struct led_classdev_flash, led_cdev);
}

/**
 * led_classdev_flash_register - register a new object of led_classdev_flash class
				 with support for flash LEDs
 * @parent: the device to register
 * @flash: the led_classdev_flash structure for this device
 * @node: device tree node of the LED Flash Class device - it must be
	  initialized if the device is to be registered in the flash manager
 *
 * Returns: 0 on success or negative error value on failure
 */
int led_classdev_flash_register(struct device *parent,
				struct led_classdev_flash *flash,
				struct device_node *node);

/**
 * led_classdev_flash_unregister - unregisters an object of led_classdev_flash class
				   with support for flash LEDs
 * @flash: the flash led device to unregister
 *
 * Unregisters a previously registered via led_classdev_flash_register object
 */
void led_classdev_flash_unregister(struct led_classdev_flash *flash);

/**
 * led_set_flash_strobe - setup flash strobe
 * @flash: the flash LED to set strobe on
 * @state: 1 - strobe flash, 0 - stop flash strobe
 *
 * Setup flash strobe - trigger flash strobe
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_set_flash_strobe(struct led_classdev_flash *flash,
				bool state);

/**
 * led_get_flash_strobe - get flash strobe status
 * @flash: the LED to query
 * @state: 1 - flash is strobing, 0 - flash is off
 *
 * Check whether the flash is strobing at the moment or not.
 *
u* Returns: 0 on success or negative error value on failure
 */
extern int led_get_flash_strobe(struct led_classdev_flash *flash,
				bool *state);
/**
 * led_set_flash_brightness - set flash LED brightness
 * @flash: the LED to set
 * @brightness: the brightness to set it to
 *
 * Returns: 0 on success or negative error value on failure
 *
 * Set a flash LED's brightness.
 */
extern int led_set_flash_brightness(struct led_classdev_flash *flash,
					u32 brightness);

/**
 * led_update_flash_brightness - update flash LED brightness
 * @flash: the LED to query
 *
 * Get a flash LED's current brightness and update led_flash->brightness
 * member with the obtained value.
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_update_flash_brightness(struct led_classdev_flash *flash);

/**
 * led_set_flash_timeout - set flash LED timeout
 * @flash: the LED to set
 * @timeout: the flash timeout to set it to
 *
 * Set the flash strobe duration. The duration set by the driver
 * is returned in the timeout argument and may differ from the
 * one that was originally passed.
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_set_flash_timeout(struct led_classdev_flash *flash,
					u32 timeout);

/**
 * led_get_flash_fault - get the flash LED fault
 * @flash: the LED to query
 * @fault: bitmask containing flash faults
 *
 * Get the flash LED fault.
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_get_flash_fault(struct led_classdev_flash *flash,
					u32 *fault);

/**
 * led_set_external_strobe - set the flash LED external_strobe mode
 * @flash: the LED to set
 * @enable: the state to set it to
 *
 * Enable/disable strobing the flash LED with use of external source
 *
  Returns: 0 on success or negative error value on failure
 */
extern int led_set_external_strobe(struct led_classdev_flash *flash,
					bool enable);

/**
 * led_set_indicator_brightness - set indicator LED brightness
 * @flash: the LED to set
 * @brightness: the brightness to set it to
 *
 * Set an indicator LED's brightness.
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_set_indicator_brightness(struct led_classdev_flash *flash,
					u32 led_brightness);

/**
 * led_update_indicator_brightness - update flash indicator LED brightness
 * @flash: the LED to query
 *
 * Get a flash indicator LED's current brightness and update
 * led_flash->indicator_brightness member with the obtained value.
 *
 * Returns: 0 on success or negative error value on failure
 */
extern int led_update_indicator_brightness(struct led_classdev_flash *flash);

#ifdef CONFIG_V4L2_FLASH_LED_CLASS
/**
 * led_get_v4l2_flash_ops - get ops for controlling LED Flash Class
			    device with use of V4L2 Flash controls
 * Returns: v4l2_flash_ops
 */
const struct v4l2_flash_ops *led_get_v4l2_flash_ops(void);
#else
#define led_get_v4l2_flash_ops() (0)
#endif

#endif	/* __LINUX_FLASH_LEDS_H_INCLUDED */

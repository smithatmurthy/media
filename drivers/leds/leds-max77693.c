/*
 * LED Flash Class driver for the flash cell of max77693 mfd.
 *
 *	Copyright (C) 2014, Samsung Electronics Co., Ltd.
 *
 *	Authors: Jacek Anaszewski <j.anaszewski@samsung.com>
 *		 Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <asm/div64.h>
#include <linux/led-class-flash.h>
#include <linux/led-flash-manager.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-private.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <media/v4l2-flash.h>

#define MAX77693_LED_NAME_1		"max77693-flash_1"
#define MAX77693_LED_NAME_2		"max77693-flash_2"

#define MAX77693_TORCH_IOUT_BITS	4

#define MAX77693_TORCH_NO_TIMER		0x40
#define MAX77693_FLASH_TIMER_LEVEL	0x80

#define MAX77693_FLASH_EN_OFF		0
#define MAX77693_FLASH_EN_FLASH		1
#define MAX77693_FLASH_EN_TORCH		2
#define MAX77693_FLASH_EN_ON		3

#define MAX77693_FLASH_EN1_SHIFT	6
#define MAX77693_FLASH_EN2_SHIFT	4
#define MAX77693_TORCH_EN1_SHIFT	2
#define MAX77693_TORCH_EN2_SHIFT	0

#define MAX77693_FLASH_LOW_BATTERY_EN	0x80

#define MAX77693_FLASH_BOOST_FIXED	0x04
#define MAX77693_FLASH_BOOST_LEDNUM_2	0x80

#define MAX77693_FLASH_TIMEOUT_MIN	62500
#define MAX77693_FLASH_TIMEOUT_MAX	1000000
#define MAX77693_FLASH_TIMEOUT_STEP	62500

#define MAX77693_TORCH_TIMEOUT_MIN	262000
#define MAX77693_TORCH_TIMEOUT_MAX	15728000

#define MAX77693_FLASH_IOUT_MIN		15625
#define MAX77693_FLASH_IOUT_MAX_1LED	1000000
#define MAX77693_FLASH_IOUT_MAX_2LEDS	625000
#define MAX77693_FLASH_IOUT_STEP	15625

#define MAX77693_TORCH_IOUT_MIN		15625
#define MAX77693_TORCH_IOUT_MAX		250000
#define MAX77693_TORCH_IOUT_STEP	15625

#define MAX77693_FLASH_VSYS_MIN		2400
#define MAX77693_FLASH_VSYS_MAX		3400
#define MAX77693_FLASH_VSYS_STEP	33

#define MAX77693_FLASH_VOUT_MIN		3300
#define MAX77693_FLASH_VOUT_MAX		5500
#define MAX77693_FLASH_VOUT_STEP	25
#define MAX77693_FLASH_VOUT_RMIN	0x0c

#define MAX77693_LED_STATUS_FLASH_ON	(1 << 3)
#define MAX77693_LED_STATUS_TORCH_ON	(1 << 2)

#define MAX77693_LED_FLASH_INT_FLED2_OPEN	(1 << 0)
#define MAX77693_LED_FLASH_INT_FLED2_SHORT	(1 << 1)
#define MAX77693_LED_FLASH_INT_FLED1_OPEN	(1 << 2)
#define MAX77693_LED_FLASH_INT_FLED1_SHORT	(1 << 3)
#define MAX77693_LED_FLASH_INT_OVER_CURRENT	(1 << 4)

#define MAX77693_MODE_OFF			0
#define MAX77693_MODE_FLASH1			(1 << 0)
#define MAX77693_MODE_FLASH2			(1 << 1)
#define MAX77693_MODE_TORCH1			(1 << 2)
#define MAX77693_MODE_TORCH2			(1 << 3)
#define MAX77693_MODE_FLASH_EXTERNAL1		(1 << 4)
#define MAX77693_MODE_FLASH_EXTERNAL2		(1 << 5)

enum {
	FLED1,
	FLED2
};

enum {
	FLASH,
	TORCH
};

struct max77693_led {
	struct regmap *regmap;
	struct platform_device *pdev;
	struct max77693_led_platform_data *pdata;
	struct mutex lock;

	struct led_classdev_flash ldev1;
	struct work_struct work1_brightness_set;
	struct v4l2_flash *v4l2_flash1;

	struct led_classdev_flash ldev2;
	struct work_struct work2_brightness_set;
	struct v4l2_flash *v4l2_flash2;

	unsigned int torch1_brightness;
	unsigned int torch2_brightness;
	unsigned int flash1_timeout;
	unsigned int flash2_timeout;
	unsigned int current_flash_timeout;
	unsigned int mode_flags;
	u8 torch_iout_reg;
	bool iout_joint;
};

struct max77693_led_settings {
	struct led_flash_setting torch_brightness;
	struct led_flash_setting flash_brightness;
	struct led_flash_setting flash_timeout;
};

static u8 max77693_led_iout_to_reg(u32 ua)
{
	if (ua < MAX77693_FLASH_IOUT_MIN)
		ua = MAX77693_FLASH_IOUT_MIN;
	return (ua - MAX77693_FLASH_IOUT_MIN) / MAX77693_FLASH_IOUT_STEP;
}

static u8 max77693_flash_timeout_to_reg(u32 us)
{
	return (us - MAX77693_FLASH_TIMEOUT_MIN) / MAX77693_FLASH_TIMEOUT_STEP;
}

static inline struct max77693_led *ldev1_to_led(
					struct led_classdev_flash *ldev1)
{
	return container_of(ldev1, struct max77693_led, ldev1);
}

static inline struct max77693_led *ldev2_to_led(
					struct led_classdev_flash *ldev2)
{
	return container_of(ldev2, struct max77693_led, ldev2);
}

static u8 max77693_led_vsys_to_reg(u32 mv)
{
	return ((mv - MAX77693_FLASH_VSYS_MIN) / MAX77693_FLASH_VSYS_STEP) << 2;
}

static u8 max77693_led_vout_to_reg(u32 mv)
{
	return (mv - MAX77693_FLASH_VOUT_MIN) / MAX77693_FLASH_VOUT_STEP +
		MAX77693_FLASH_VOUT_RMIN;
}

/* split composite current @i into two @iout according to @imax weights */
static void max77693_calc_iout(u32 iout[2], u32 i, u32 imax[2])
{
	u64 t = i;

	t *= imax[1];
	do_div(t, imax[0] + imax[1]);

	iout[1] = (u32)t / MAX77693_FLASH_IOUT_STEP * MAX77693_FLASH_IOUT_STEP;
	iout[0] = i - iout[1];
}

static int max77693_set_mode(struct max77693_led *led, unsigned int mode)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct regmap *rmap = led->regmap;
	int ret, v = 0;

	if (mode & MAX77693_MODE_TORCH1) {
		if (p->trigger[FLED1] & MAX77693_LED_TRIG_SOFT)
			v |= MAX77693_FLASH_EN_ON << MAX77693_TORCH_EN1_SHIFT;
	}

	if (mode & MAX77693_MODE_TORCH2) {
		if (p->trigger[FLED2] & MAX77693_LED_TRIG_SOFT)
			v |= MAX77693_FLASH_EN_ON << MAX77693_TORCH_EN2_SHIFT;
	}

	if (mode & MAX77693_MODE_FLASH1) {
		if (p->trigger[FLED1] & MAX77693_LED_TRIG_SOFT)
			v |= MAX77693_FLASH_EN_ON << MAX77693_FLASH_EN1_SHIFT;
	} else if (mode & MAX77693_MODE_FLASH_EXTERNAL1) {
		if (p->trigger[FLED1] & MAX77693_LED_TRIG_EXT)
			v |= MAX77693_FLASH_EN_FLASH << MAX77693_FLASH_EN1_SHIFT;
		/*
		 * Enable hw triggering also for torch mode, as some camera
		 * sensors use torch led to fathom ambient light conditions
		 * before strobing the flash.
		 */
		if (p->trigger[FLED1] & MAX77693_LED_TRIG_EXT)
			v |= MAX77693_FLASH_EN_TORCH << MAX77693_TORCH_EN1_SHIFT;
	}

	if (mode & MAX77693_MODE_FLASH2) {
		if (p->trigger[FLED2] & MAX77693_LED_TRIG_SOFT)
			v |= MAX77693_FLASH_EN_ON << MAX77693_FLASH_EN2_SHIFT;
	} else if (mode & MAX77693_MODE_FLASH_EXTERNAL1) {
		if (p->trigger[FLED2] & MAX77693_LED_TRIG_EXT)
			v |= MAX77693_FLASH_EN_FLASH << MAX77693_FLASH_EN2_SHIFT;
		/*
		 * Enable hw triggering also for torch mode, as some camera
		 * sensors use torch led to fathom ambient light conditions
		 * before strobing the flash.
		 */
		if (p->trigger[FLED2] & MAX77693_LED_TRIG_EXT)
			v |= MAX77693_FLASH_EN_TORCH << MAX77693_TORCH_EN2_SHIFT;
	}

	/* Reset the register only prior setting flash modes */
	if (mode & ~(MAX77693_MODE_TORCH1 | MAX77693_MODE_TORCH2)) {
		ret = regmap_write(rmap, MAX77693_LED_REG_FLASH_EN, 0);
		if (ret < 0)
			return ret;
	}

	return regmap_write(rmap, MAX77693_LED_REG_FLASH_EN, v);
}

static int max77693_add_mode(struct max77693_led *led, unsigned int mode)
{
	int ret;

	/* Span mode on FLED2 for joint iouts case */
	if (led->iout_joint)
		mode |= (mode << 1);

	/*
	 * Torch mode once enabled remains active until turned off,
	 * and thus no action is required in this case.
	 */
	if ((mode & MAX77693_MODE_TORCH1) &&
	    (led->mode_flags & MAX77693_MODE_TORCH1))
		return 0;
	if ((mode & MAX77693_MODE_TORCH2) &&
	    (led->mode_flags & MAX77693_MODE_TORCH2))
		return 0;
	/*
	 * FLASH_EXTERNAL mode activates HW triggered flash and torch
	 * modes in the device. The related register settings interfere
	 * with SW triggerred modes, thus clear them to ensure proper
	 * device configuration.
	 */
	if (mode & MAX77693_MODE_FLASH_EXTERNAL1)
		led->mode_flags &= (~MAX77693_MODE_TORCH1 &
				    ~MAX77693_MODE_FLASH1);
	if (mode & MAX77693_MODE_FLASH_EXTERNAL2)
		led->mode_flags &= (~MAX77693_MODE_TORCH2 &
				    ~MAX77693_MODE_FLASH2);

	led->mode_flags |= mode;

	ret = max77693_set_mode(led, led->mode_flags);
	if (ret < 0)
		return ret;

	/*
	 * Clear flash mode flag after setting the mode to avoid
	 * spurous flash strobing on every subsequent torch mode
	 * setting.
	 */
	if (mode & MAX77693_MODE_FLASH1 ||
	    mode & MAX77693_MODE_FLASH_EXTERNAL1 ||
	    mode & MAX77693_MODE_FLASH2 ||
	    mode & MAX77693_MODE_FLASH_EXTERNAL2)
		led->mode_flags &= ~mode;

	return ret;
}

static int max77693_clear_mode(struct max77693_led *led, unsigned int mode)
{
	int ret;

	/* Span mode on FLED2 for joint iouts case */
	if (led->iout_joint)
		mode |= (mode << 1);

	led->mode_flags &= ~mode;

	ret = max77693_set_mode(led, led->mode_flags);

	return ret;
}

static int max77693_set_torch_current(struct max77693_led *led,
					int led_id,
					u32 micro_amp)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct regmap *rmap = led->regmap;
	u32 iout[2], iout_max[2];
	u8 iout1_reg = 0, iout2_reg = 0;

	iout_max[FLED1] = p->iout_torch[FLED1];
	iout_max[FLED2] = p->iout_torch[FLED2];

	if (led_id == FLED1) {
		/*
		 * Preclude splitting current to FLED2 if we
		 * are driving two separate leds.
		 */
		if (!led->iout_joint)
			iout_max[FLED2] = 0;
		max77693_calc_iout(iout, micro_amp, iout_max);
	} else if (led_id == FLED2) {
		iout_max[FLED1] = 0;
		max77693_calc_iout(iout, micro_amp, iout_max);
	}

	if (led_id == FLED1 || led->iout_joint) {
		iout1_reg = max77693_led_iout_to_reg(iout[FLED1]);
		led->torch_iout_reg &= 0xf0;
	}
	if (led_id == FLED2 || led->iout_joint) {
		iout2_reg = max77693_led_iout_to_reg(iout[FLED2]);
		led->torch_iout_reg &= 0x0f;
	}

	led->torch_iout_reg |= (iout1_reg | iout2_reg <<
					MAX77693_TORCH_IOUT_BITS);

	return regmap_write(rmap, MAX77693_LED_REG_ITORCH,
						led->torch_iout_reg);
}

static int max77693_set_flash_current(struct max77693_led *led,
					int led_id,
					u32 micro_amp)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct regmap *rmap = led->regmap;
	u32 iout[2], iout_max[2];
	u8 iout1_reg, iout2_reg;
	int ret = -EINVAL;

	iout_max[FLED1] = p->iout_flash[FLED1];
	iout_max[FLED2] = p->iout_flash[FLED2];

	if (led_id == FLED1) {
		/*
		 * Preclude splitting current to FLED2 if we
		 * are driving two separate leds.
		 */
		if (!led->iout_joint)
			iout_max[FLED2] = 0;
		max77693_calc_iout(iout, micro_amp, iout_max);
	} else if (led_id == FLED2) {
		iout_max[FLED1] = 0;
		max77693_calc_iout(iout, micro_amp, iout_max);
	}

	if (led_id == FLED1 || led->iout_joint) {
		iout1_reg = max77693_led_iout_to_reg(iout[FLED1]);
		ret = regmap_write(rmap, MAX77693_LED_REG_IFLASH1,
							iout1_reg);
		if (ret < 0)
			return ret;
	}
	if (led_id == FLED2 || led->iout_joint) {
		iout2_reg = max77693_led_iout_to_reg(iout[FLED2]);
		ret = regmap_write(rmap, MAX77693_LED_REG_IFLASH2,
							iout2_reg);
	}

	return ret;
}

static int max77693_set_timeout(struct max77693_led *led, u32 timeout)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct regmap *rmap = led->regmap;
	u8 v;
	int ret;

	v = max77693_flash_timeout_to_reg(timeout);

	if (p->trigger_type[FLASH] == MAX77693_LED_TRIG_TYPE_LEVEL)
		v |= MAX77693_FLASH_TIMER_LEVEL;

	ret = regmap_write(rmap, MAX77693_LED_REG_FLASH_TIMER, v);
	if (ret < 0)
		return ret;

	led->current_flash_timeout = timeout;

	return 0;
}

static int max77693_strobe_status_get(struct max77693_led *led, bool *state)
{
	struct regmap *rmap = led->regmap;
	unsigned int v;
	int ret;

	ret = regmap_read(rmap, MAX77693_LED_REG_FLASH_INT_STATUS, &v);
	if (ret < 0)
		return ret;

	*state = v & MAX77693_LED_STATUS_FLASH_ON;

	return ret;
}

static int max77693_int_flag_get(struct max77693_led *led, unsigned int *v)
{
	struct regmap *rmap = led->regmap;

	return regmap_read(rmap, MAX77693_LED_REG_FLASH_INT, v);
}

static int max77693_setup(struct max77693_led *led)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct regmap *rmap = led->regmap;
	int i, firstLed, lastLed, ret;
	u32 max_flash_curr[2];
	u8 v;

	/*
	 * Initialize only flash current. Torch current doesn't
	 * require initialization as ITORCH register is written with
	 * new value each time brightness_set op is called.
	 */
	if (led->iout_joint) {
		firstLed = FLED1;
		lastLed = FLED1;
		max_flash_curr[FLED1] = p->iout_flash[FLED1] +
					p->iout_flash[FLED2];
	} else {
		firstLed = p->fleds[FLED1] ? FLED1 : FLED2;
		lastLed = p->num_leds == 2 ? FLED2 : firstLed;
		max_flash_curr[FLED1] = p->iout_flash[FLED1];
		max_flash_curr[FLED2] = p->iout_flash[FLED2];
	}

	for (i = firstLed; i <= lastLed; ++i) {
		ret = max77693_set_flash_current(led, i,
					max_flash_curr[i]);
		if (ret < 0)
			return ret;
	}

	v = MAX77693_TORCH_NO_TIMER | MAX77693_LED_TRIG_TYPE_LEVEL;
	ret = regmap_write(rmap, MAX77693_LED_REG_ITORCHTIMER, v);
	if (ret < 0)
		return ret;

	ret = max77693_set_timeout(led,	p->flash_timeout);
	if (ret < 0)
		return ret;

	if (p->low_vsys > 0)
		v = max77693_led_vsys_to_reg(p->low_vsys) |
						MAX77693_FLASH_LOW_BATTERY_EN;
	else
		v = 0;

	ret = regmap_write(rmap, MAX77693_LED_REG_MAX_FLASH1, v);
	if (ret < 0)
		return ret;
	ret = regmap_write(rmap, MAX77693_LED_REG_MAX_FLASH2, 0);
	if (ret < 0)
		return ret;

	if (p->boost_mode == MAX77693_LED_BOOST_FIXED)
		v = MAX77693_FLASH_BOOST_FIXED;
	else
		v = p->boost_mode | p->boost_mode << 1;
	if (p->fleds[FLED1] && p->fleds[FLED2])
		v |= MAX77693_FLASH_BOOST_LEDNUM_2;
	ret = regmap_write(rmap, MAX77693_LED_REG_VOUT_CNTL, v);
	if (ret < 0)
		return ret;

	v = max77693_led_vout_to_reg(p->boost_vout);
	ret = regmap_write(rmap, MAX77693_LED_REG_VOUT_FLASH1, v);
	if (ret < 0)
		return ret;

	return max77693_set_mode(led, MAX77693_MODE_OFF);
}

static int max77693_led_brightness_set(struct max77693_led *led,
					int led_id, enum led_brightness value)
{
	int ret;

	mutex_lock(&led->lock);

	if (value == 0) {
		ret = max77693_clear_mode(led, MAX77693_MODE_TORCH1 << led_id);
		if (ret < 0)
			dev_dbg(&led->pdev->dev,
				"Failed to clear torch mode (%d)\n",
				ret);
		goto unlock;
	}

	ret = max77693_set_torch_current(led, led_id, value *
					     MAX77693_TORCH_IOUT_STEP);
	if (ret < 0) {
		dev_dbg(&led->pdev->dev,
			"Failed to set torch current (%d)\n",
			ret);
		goto unlock;
	}

	ret = max77693_add_mode(led, MAX77693_MODE_TORCH1 << led_id);
	if (ret < 0)
		dev_dbg(&led->pdev->dev,
			"Failed to set torch mode (%d)\n",
			ret);
unlock:
	mutex_unlock(&led->lock);
	return ret;
}

#define MAX77693_LED_BRIGHTNESS_SET_WORK(ID)				\
static void max77693_led##ID##_brightness_set_work(			\
					struct work_struct *work)	\
{									\
	struct max77693_led *led = container_of(work,			\
					struct max77693_led,		\
					work##ID##_brightness_set);	\
									\
	max77693_led_brightness_set(led, FLED##ID,			\
					led->torch##ID##_brightness);	\
}

/* LED subsystem callbacks */

#define MAX77693_LED_TORCH_BRIGHTNESS_SET(ID)				\
static int max77693_led##ID##_torch_brightness_set(			\
				struct led_classdev *led_cdev,		\
				enum led_brightness value)		\
{									\
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);	\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
									\
	return max77693_led_brightness_set(led, FLED##ID, value);	\
}

#define MAX77693_LED_BRIGHTNESS_SET(ID)					\
static void max77693_led##ID##_brightness_set(				\
				struct led_classdev *led_cdev,		\
				enum led_brightness value)		\
{									\
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);	\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
									\
	led->torch##ID##_brightness = value;				\
	schedule_work(&led->work##ID##_brightness_set);			\
}

#define MAX77693_LED_FLASH_BRIGHTNESS_SET(ID)				\
static int max77693_led##ID##_flash_brightness_set(			\
				struct led_classdev_flash *flash,	\
				u32 brightness)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
	int ret;							\
									\
	mutex_lock(&led->lock);						\
	ret = max77693_set_flash_current(led, FLED##ID, brightness);	\
	mutex_unlock(&led->lock);					\
									\
	return ret;							\
}

#define MAX77693_LED_FLASH_STROBE_SET(ID)				\
static int max77693_led##ID##_flash_strobe_set(				\
				struct led_classdev_flash *flash,	\
				bool state)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
	int ret;							\
									\
	mutex_lock(&led->lock);						\
									\
	if (!state) {							\
		ret = max77693_clear_mode(led,				\
					  MAX77693_MODE_FLASH##ID);	\
		goto unlock;						\
	}								\
									\
	if (led->flash##ID##_timeout != led->current_flash_timeout) {	\
		ret = max77693_set_timeout(led,				\
					   led->flash##ID##_timeout);	\
		if (ret < 0)						\
			goto unlock;					\
	}								\
									\
	ret = max77693_add_mode(led, MAX77693_MODE_FLASH##ID);		\
									\
unlock:									\
	mutex_unlock(&led->lock);					\
	return ret;							\
}

#define MAX77693_LED_FLASH_EXTERNAL_STROBE_SET(ID)			\
static int max77693_led##ID##_external_strobe_set(			\
				struct led_classdev_flash *flash,	\
				bool enable)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
	int ret;							\
									\
	mutex_lock(&led->lock);						\
									\
	if (enable)							\
		ret = max77693_add_mode(led,				\
				MAX77693_MODE_FLASH_EXTERNAL##ID);	\
	else								\
		ret = max77693_clear_mode(led,				\
				MAX77693_MODE_FLASH_EXTERNAL##ID);	\
									\
	mutex_unlock(&led->lock);					\
									\
	return ret;							\
}

#define MAX77693_LED_FLASH_FAULT_GET(ID)				\
static int max77693_led##ID##_flash_fault_get(				\
				struct led_classdev_flash *flash,	\
				u32 *fault)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
	unsigned int v;							\
	int ret;							\
									\
	ret = max77693_int_flag_get(led, &v);				\
	if (ret < 0)							\
		return ret;						\
									\
	*fault = 0;							\
									\
	if (v & MAX77693_LED_FLASH_INT_FLED##ID##_OPEN)			\
		*fault |= LED_FAULT_OVER_VOLTAGE;			\
	if (v & MAX77693_LED_FLASH_INT_FLED##ID##_SHORT)		\
		*fault |= LED_FAULT_SHORT_CIRCUIT;			\
	if (v & MAX77693_LED_FLASH_INT_OVER_CURRENT)			\
		*fault |= LED_FAULT_OVER_CURRENT;			\
									\
	return 0;							\
}

#define MAX77693_LED_FLASH_STROBE_GET(ID)				\
static int max77693_led##ID##_flash_strobe_get(				\
				struct led_classdev_flash *flash,	\
				bool *state)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
									\
	if (!state)							\
		return -EINVAL;						\
									\
	return max77693_strobe_status_get(led, state);			\
}

#define MAX77693_LED_FLASH_TIMEOUT_SET(ID)				\
static int max77693_led##ID##_flash_timeout_set(			\
				struct led_classdev_flash *flash,	\
				u32 timeout)				\
{									\
	struct max77693_led *led = ldev##ID##_to_led(flash);		\
									\
	mutex_lock(&led->lock);						\
	led->flash##ID##_timeout = timeout;				\
	mutex_unlock(&led->lock);					\
									\
	return 0;							\
}

MAX77693_LED_BRIGHTNESS_SET(1)
MAX77693_LED_BRIGHTNESS_SET_WORK(1)
MAX77693_LED_TORCH_BRIGHTNESS_SET(1)
MAX77693_LED_FLASH_BRIGHTNESS_SET(1)
MAX77693_LED_FLASH_STROBE_SET(1)
MAX77693_LED_FLASH_STROBE_GET(1)
MAX77693_LED_FLASH_EXTERNAL_STROBE_SET(1)
MAX77693_LED_FLASH_TIMEOUT_SET(1)
MAX77693_LED_FLASH_FAULT_GET(1)

MAX77693_LED_BRIGHTNESS_SET(2)
MAX77693_LED_BRIGHTNESS_SET_WORK(2)
MAX77693_LED_TORCH_BRIGHTNESS_SET(2)
MAX77693_LED_FLASH_BRIGHTNESS_SET(2)
MAX77693_LED_FLASH_STROBE_SET(2)
MAX77693_LED_FLASH_STROBE_GET(2)
MAX77693_LED_FLASH_EXTERNAL_STROBE_SET(2)
MAX77693_LED_FLASH_TIMEOUT_SET(2)
MAX77693_LED_FLASH_FAULT_GET(2)

static void max77693_led_parse_dt(struct max77693_led_platform_data *p,
			    struct device_node *node)
{
	of_property_read_u32_array(node, "iout-torch", p->iout_torch, 2);
	of_property_read_u32_array(node, "iout-flash", p->iout_flash, 2);
	of_property_read_u32_array(node, "maxim,fleds", p->fleds, 2);
	of_property_read_u32_array(node, "maxim,trigger", p->trigger, 2);
	of_property_read_u32_array(node, "maxim,trigger-type", p->trigger_type,
									2);
	of_property_read_u32(node, "flash-timeout", &p->flash_timeout);
	of_property_read_u32(node, "maxim,boost-mode", &p->boost_mode);
	of_property_read_u32(node, "maxim,boost-vout", &p->boost_vout);
	of_property_read_u32(node, "maxim,num-leds", &p->num_leds);
	of_property_read_u32(node, "maxim,vsys-min", &p->low_vsys);
}

static void clamp_align(u32 *v, u32 min, u32 max, u32 step)
{
	*v = clamp_val(*v, min, max);
	if (step > 1)
		*v = (*v - min) / step * step + min;
}

static void max77693_led_validate_platform_data(
					struct max77693_led_platform_data *p)
{
	u32 max;
	int i;

	p->boost_mode = clamp_val(p->boost_mode, MAX77693_LED_BOOST_NONE,
			    MAX77693_LED_BOOST_FIXED);
	p->num_leds = clamp_val(p->num_leds, 1, 2);

	for (i = 0; i < ARRAY_SIZE(p->fleds); ++i)
		p->fleds[i] = clamp_val(p->fleds[i], 0, 1);

	/* Ensure fleds configuration is sane */
	if (!p->fleds[FLED1] && !p->fleds[FLED2]) {
		p->fleds[FLED1] = p->fleds[FLED2] = 1;
		p->num_leds = 1;
	}

	/* Ensure num_leds is consistent with fleds configuration */
	if ((!p->fleds[FLED1] || !p->fleds[FLED2]) && p->num_leds == 2)
		p->num_leds = 1;

	/*
	 * boost must be enabled if current outputs
	 * are connected to separate leds.
	 */
	if ((p->num_leds == 2 || (p->fleds[FLED1] && p->fleds[FLED2])) &&
	    p->boost_mode == MAX77693_LED_BOOST_NONE)
		p->boost_mode = MAX77693_LED_BOOST_FIXED;

	max = p->boost_mode ? MAX77693_FLASH_IOUT_MAX_2LEDS :
				MAX77693_FLASH_IOUT_MAX_1LED;

	if (p->fleds[FLED1]) {
		clamp_align(&p->iout_torch[FLED1], MAX77693_TORCH_IOUT_MIN,
			    MAX77693_TORCH_IOUT_MAX, MAX77693_TORCH_IOUT_STEP);
		clamp_align(&p->iout_flash[FLED1], MAX77693_FLASH_IOUT_MIN,
			    max, MAX77693_FLASH_IOUT_STEP);
	} else {
		p->iout_torch[FLED1] = p->iout_flash[FLED1] = 0;
	}
	if (p->fleds[FLED2]) {
		clamp_align(&p->iout_torch[FLED2], MAX77693_TORCH_IOUT_MIN,
			    MAX77693_TORCH_IOUT_MAX, MAX77693_TORCH_IOUT_STEP);
		clamp_align(&p->iout_flash[FLED2], MAX77693_FLASH_IOUT_MIN,
			    max, MAX77693_FLASH_IOUT_STEP);
	} else {
		p->iout_torch[FLED2] = p->iout_flash[FLED2] = 0;
	}

	for (i = 0; i < ARRAY_SIZE(p->trigger); ++i)
		p->trigger[i] = clamp_val(p->trigger[i], 0, 7);
	for (i = 0; i < ARRAY_SIZE(p->trigger_type); ++i)
		p->trigger_type[i] = clamp_val(p->trigger_type[i],
					MAX77693_LED_TRIG_TYPE_EDGE,
					MAX77693_LED_TRIG_TYPE_LEVEL);

	clamp_align(&p->flash_timeout, MAX77693_FLASH_TIMEOUT_MIN,
		    MAX77693_FLASH_TIMEOUT_MAX, MAX77693_FLASH_TIMEOUT_STEP);

	clamp_align(&p->boost_vout, MAX77693_FLASH_VOUT_MIN,
		    MAX77693_FLASH_VOUT_MAX, MAX77693_FLASH_VOUT_STEP);

	if (p->low_vsys)
		clamp_align(&p->low_vsys, MAX77693_FLASH_VSYS_MIN,
			    MAX77693_FLASH_VSYS_MAX, MAX77693_FLASH_VSYS_STEP);
}

static int max77693_led_get_platform_data(struct max77693_led *led)
{
	struct max77693_led_platform_data *p;
	struct device *dev = &led->pdev->dev;

	if (dev->of_node) {
		p = devm_kzalloc(dev, sizeof(*led->pdata), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		max77693_led_parse_dt(p, dev->of_node);
	} else {
		p = dev_get_platdata(dev);
		if (!p)
			return -ENODEV;
	}
	led->pdata = p;

	max77693_led_validate_platform_data(p);

	return 0;
}

#define MAX77693_LED_INIT_FLASH_OPS(ID)						\
const struct led_flash_ops flash_ops##ID = {					\
										\
	.flash_brightness_set	= max77693_led##ID##_flash_brightness_set,	\
	.strobe_set 		= max77693_led##ID##_flash_strobe_set,		\
	.strobe_get		= max77693_led##ID##_flash_strobe_get,		\
	.timeout_set		= max77693_led##ID##_flash_timeout_set,		\
	.external_strobe_set	= max77693_led##ID##_external_strobe_set,	\
	.fault_get		= max77693_led##ID##_flash_fault_get,		\
};

MAX77693_LED_INIT_FLASH_OPS(1)
MAX77693_LED_INIT_FLASH_OPS(2)

static void max77693_init_flash_settings(struct max77693_led *led,
					 struct max77693_led_settings *s,
					 int led_id)
{
	struct max77693_led_platform_data *p = led->pdata;
	struct led_flash_setting *setting;

	/* Init torch intensity setting */
	setting = &s->torch_brightness;
	setting->min = led->iout_joint ?
				MAX77693_TORCH_IOUT_MIN * 2 :
				MAX77693_TORCH_IOUT_MIN;
	setting->max = led->iout_joint ?
			p->iout_torch[FLED1] + p->iout_torch[FLED2] :
			p->iout_torch[led_id];
	setting->step = MAX77693_TORCH_IOUT_STEP;
	setting->val = setting->max;

	/* Init flash intensity setting */
	setting = &s->flash_brightness;
	setting->min = led->iout_joint ?
				MAX77693_FLASH_IOUT_MIN * 2 :
				MAX77693_FLASH_IOUT_MIN;
	setting->max = led->iout_joint ?
			p->iout_flash[FLED1] + p->iout_flash[FLED2] :
			p->iout_flash[led_id];
	setting->step = MAX77693_FLASH_IOUT_STEP;
	setting->val = setting->max;

	/* Init flash timeout setting */
	setting = &s->flash_timeout;
	setting->min = MAX77693_FLASH_TIMEOUT_MIN;
	setting->max = MAX77693_FLASH_TIMEOUT_MAX;
	setting->step = MAX77693_FLASH_TIMEOUT_STEP;
	setting->val = p->flash_timeout;
}

#ifdef CONFIG_V4L2_FLASH_LED_CLASS
static void max77693_init_v4l2_ctrl_config(struct max77693_led_settings *s,
					struct v4l2_flash_ctrl_config *config)
{
	struct led_flash_setting *setting;
	struct v4l2_ctrl_config *c;

	c = &config->torch_intensity;
	setting = &s->torch_brightness;
	c->min = setting->min;
	c->max = setting->max;
	c->step = setting->step;
	c->def = setting->val;

	c = &config->flash_intensity;
	setting = &s->flash_brightness;
	c->min = setting->min;
	c->max = setting->max;
	c->step = setting->step;
	c->def = setting->val;

	c = &config->flash_timeout;
	setting = &s->flash_timeout;
	c->min = setting->min;
	c->max = setting->max;
	c->step = setting->step;
	c->def = setting->val;

	/* Init flash faults config */
	config->flash_faults =	V4L2_FLASH_FAULT_OVER_VOLTAGE |
				V4L2_FLASH_FAULT_SHORT_CIRCUIT |
				V4L2_FLASH_FAULT_OVER_CURRENT;
}
#else
#define max77693_init_v4l2_ctrl_config(s, config)
#endif

#define MAX77693_LED_REGISTER_FLASH(ID)					\
static int max77693_register_led##ID(struct max77693_led *led)		\
{									\
	struct platform_device *pdev = led->pdev;			\
	struct device *dev = &pdev->dev;				\
	struct led_classdev_flash *flash;				\
	struct led_classdev *led_cdev;					\
	struct max77693_led_platform_data *p = led->pdata;		\
	struct v4l2_flash_ctrl_config v4l2_flash_config;		\
	struct max77693_led_settings settings;				\
	int ret;							\
									\
	flash = &led->ldev##ID;						\
									\
	/* Init flash settings */					\
	max77693_init_flash_settings(led, &settings, FLED##ID);		\
	/* Init V4L2 Flash controls basing on initialized settings */	\
	max77693_init_v4l2_ctrl_config(&settings, &v4l2_flash_config);	\
									\
	/* Init led class */						\
	led_cdev = &flash->led_cdev;					\
	led_cdev->name = MAX77693_LED_NAME_##ID;			\
	led_cdev->brightness_set = max77693_led##ID##_brightness_set;	\
	led_cdev->torch_brightness_set =				\
			max77693_led##ID##_torch_brightness_set;	\
	led_cdev->max_brightness = settings.torch_brightness.val /	\
					MAX77693_TORCH_IOUT_STEP;	\
	led_cdev->flags |= LED_DEV_CAP_FLASH;				\
									\
	INIT_WORK(&led->work##ID##_brightness_set,			\
			max77693_led##ID##_brightness_set_work);	\
									\
	flash->ops = &flash_ops##ID;					\
	flash->brightness = settings.flash_brightness;			\
	flash->timeout = settings.flash_timeout;			\
	led->flash##ID##_timeout = flash->timeout.val;			\
									\
	if (p->trigger[FLED##ID] & MAX77693_LED_TRIG_FLASH)		\
		flash->has_external_strobe = true;			\
									\
	/* Register in the LED subsystem. */				\
	ret = led_classdev_flash_register(&pdev->dev, flash,		\
						dev->of_node);		\
	if (ret < 0)							\
		return ret;						\
									\
	ret = v4l2_flash_init(flash,					\
			      &v4l2_flash_config,			\
			      led_get_v4l2_flash_ops(),			\
			      &led->v4l2_flash##ID);			\
	if (ret < 0)							\
		goto err_v4l2_flash_init;				\
									\
	return 0;							\
									\
err_v4l2_flash_init:							\
	led_classdev_flash_unregister(flash);				\
									\
	return ret;							\
}

MAX77693_LED_REGISTER_FLASH(1)
MAX77693_LED_REGISTER_FLASH(2)

static int max77693_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max77693_dev *iodev = dev_get_drvdata(dev->parent);
	struct max77693_led *led;
	struct max77693_led_platform_data *p;
	int ret;

	led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->pdev = pdev;
	led->regmap = iodev->regmap;
	platform_set_drvdata(pdev, led);
	ret = max77693_led_get_platform_data(led);
	if (ret < 0)
		return -EINVAL;

	p = led->pdata;
	mutex_init(&led->lock);

	if (p->num_leds == 1 && p->fleds[FLED1] && p->fleds[FLED2])
		led->iout_joint = true;

	ret = max77693_setup(led);
	if (ret < 0)
		return ret;

	if (led->iout_joint || p->fleds[FLED1]) {
		ret = max77693_register_led1(led);
		if (ret < 0)
			goto err_register_led1;
	}

	if (!led->iout_joint && p->fleds[FLED2]) {
		ret = max77693_register_led2(led);
		if (ret < 0)
			goto err_register_led2;
	}

	return 0;

err_register_led2:
	if (!p->fleds[FLED1])
		goto err_register_led1;
	v4l2_flash_release(led->v4l2_flash1);
	led_classdev_flash_unregister(&led->ldev1);
err_register_led1:
	mutex_destroy(&led->lock);

	return ret;
}

static int max77693_led_remove(struct platform_device *pdev)
{
	struct max77693_led *led = platform_get_drvdata(pdev);
	struct max77693_led_platform_data *p = led->pdata;

	if (led->iout_joint || p->fleds[FLED1]) {
		v4l2_flash_release(led->v4l2_flash1);
		led_classdev_flash_unregister(&led->ldev1);
		cancel_work_sync(&led->work1_brightness_set);
	}
	if (!led->iout_joint && p->fleds[FLED2]) {
		v4l2_flash_release(led->v4l2_flash2);
		led_classdev_flash_unregister(&led->ldev2);
		cancel_work_sync(&led->work2_brightness_set);
	}
	mutex_destroy(&led->lock);

	return 0;
}

static struct of_device_id max77693_led_dt_match[] = {
	{.compatible = "maxim,max77693-flash"},
	{},
};

static struct platform_driver max77693_led_driver = {
	.probe		= max77693_led_probe,
	.remove		= max77693_led_remove,
	.driver		= {
		.name	= "max77693-flash",
		.owner	= THIS_MODULE,
		.of_match_table = max77693_led_dt_match,
	},
};

module_platform_driver(max77693_led_driver);

MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_AUTHOR("Andrzej Hajda <a.hajda@samsung.com>");
MODULE_DESCRIPTION("Maxim MAX77693 led flash driver");
MODULE_LICENSE("GPL");

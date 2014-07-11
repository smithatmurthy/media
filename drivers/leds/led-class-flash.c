/*
 * LED Flash Class interface
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/led-class-flash.h>
#include <linux/led-flash-manager.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <media/v4l2-flash.h>
#include "leds.h"

#define has_flash_op(flash, op)				\
	(flash && flash->ops->op)

#define call_flash_op(flash, op, args...)		\
	((has_flash_op(flash, op)) ?			\
			(flash->ops->op(flash, args)) :	\
			-EINVAL)

static ssize_t flash_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long state;
	ssize_t ret;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	ret = led_set_flash_brightness(flash, state);
	if (ret < 0)
		goto unlock;

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t flash_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	/* no lock needed for this */
	led_update_flash_brightness(flash);

	return sprintf(buf, "%u\n", flash->brightness.val);
}
static DEVICE_ATTR_RW(flash_brightness);

static ssize_t max_flash_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->brightness.max);
}
static DEVICE_ATTR_RO(max_flash_brightness);

static ssize_t indicator_brightness_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long state;
	ssize_t ret;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	ret = led_set_indicator_brightness(flash, state);
	if (ret < 0)
		goto unlock;

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t indicator_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	/* no lock needed for this */
	led_update_indicator_brightness(flash);

	return sprintf(buf, "%u\n", flash->indicator_brightness->val);
}
static DEVICE_ATTR_RW(indicator_brightness);

static ssize_t max_indicator_brightness_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->indicator_brightness->max);
}
static DEVICE_ATTR_RO(max_indicator_brightness);

static ssize_t flash_strobe_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long state;
	ssize_t ret = -EINVAL;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &state);
	if (ret)
		goto unlock;

	if (state < 0 || state > 1) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = led_set_flash_strobe(flash, state);
	if (ret < 0)
		goto unlock;
	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t flash_strobe_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	bool state;
	int ret;

	/* no lock needed for this */
	ret = led_get_flash_strobe(flash, &state);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%u\n", state);
}
static DEVICE_ATTR_RW(flash_strobe);

static ssize_t flash_timeout_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long flash_timeout;
	ssize_t ret;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &flash_timeout);
	if (ret)
		goto unlock;

	ret = led_set_flash_timeout(flash, flash_timeout);
	if (ret < 0)
		goto unlock;

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t flash_timeout_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->timeout.val);
}
static DEVICE_ATTR_RW(flash_timeout);

static ssize_t max_flash_timeout_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->timeout.max);
}
static DEVICE_ATTR_RO(max_flash_timeout);

static ssize_t flash_fault_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	u32 fault;
	int ret;

	ret = led_get_flash_fault(flash, &fault);
	if (ret < 0)
		return -EINVAL;

	return sprintf(buf, "0x%8.8x\n", fault);
}
static DEVICE_ATTR_RO(flash_fault);

static ssize_t external_strobe_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long external_strobe;
	ssize_t ret;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &external_strobe);
	if (ret)
		goto unlock;

	if (external_strobe > 1) {
		ret = -EINVAL;
		goto unlock;
	}

	ret = led_set_external_strobe(flash, external_strobe);
	if (ret < 0)
		goto unlock;
	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t external_strobe_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->external_strobe);
}
static DEVICE_ATTR_RW(external_strobe);

static struct attribute *led_flash_strobe_attrs[] = {
	&dev_attr_flash_strobe.attr,
	NULL,
};

static struct attribute *led_flash_indicator_attrs[] = {
	&dev_attr_indicator_brightness.attr,
	&dev_attr_max_indicator_brightness.attr,
	NULL,
};

static struct attribute *led_flash_timeout_attrs[] = {
	&dev_attr_flash_timeout.attr,
	&dev_attr_max_flash_timeout.attr,
	NULL,
};

static struct attribute *led_flash_brightness_attrs[] = {
	&dev_attr_flash_brightness.attr,
	&dev_attr_max_flash_brightness.attr,
	NULL,
};

static struct attribute *led_flash_external_strobe_attrs[] = {
	&dev_attr_external_strobe.attr,
	NULL,
};

static struct attribute *led_flash_fault_attrs[] = {
	&dev_attr_flash_fault.attr,
	NULL,
};

static struct attribute_group led_flash_strobe_group = {
	.attrs = led_flash_strobe_attrs,
};

static struct attribute_group led_flash_brightness_group = {
	.attrs = led_flash_brightness_attrs,
};

static struct attribute_group led_flash_timeout_group = {
	.attrs = led_flash_timeout_attrs,
};

static struct attribute_group led_flash_indicator_group = {
	.attrs = led_flash_indicator_attrs,
};

static struct attribute_group led_flash_fault_group = {
	.attrs = led_flash_fault_attrs,
};

static struct attribute_group led_flash_external_strobe_group = {
	.attrs = led_flash_external_strobe_attrs,
};

static void led_flash_resume(struct led_classdev *led_cdev)
{
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	call_flash_op(flash, flash_brightness_set, flash->brightness.val);
	call_flash_op(flash, timeout_set, flash->timeout.val);
	call_flash_op(flash, indicator_brightness_set,
				flash->indicator_brightness->val);
}

#ifdef CONFIG_V4L2_FLASH_LED_CLASS
const struct v4l2_flash_ops led_flash_v4l2_ops = {
	.torch_brightness_set = led_set_torch_brightness,
	.torch_brightness_update = led_update_brightness,
	.flash_brightness_set = led_set_flash_brightness,
	.flash_brightness_update = led_update_flash_brightness,
	.indicator_brightness_set = led_set_indicator_brightness,
	.indicator_brightness_update = led_update_indicator_brightness,
	.strobe_set = led_set_flash_strobe,
	.strobe_get = led_get_flash_strobe,
	.timeout_set = led_set_flash_timeout,
	.external_strobe_set = led_set_external_strobe,
	.fault_get = led_get_flash_fault,
	.sysfs_lock = led_sysfs_lock,
	.sysfs_unlock = led_sysfs_unlock,
};

const struct v4l2_flash_ops *led_get_v4l2_flash_ops(void)
{
	return &led_flash_v4l2_ops;
}
EXPORT_SYMBOL_GPL(led_get_v4l2_flash_ops);
#endif

static void led_flash_remove_sysfs_groups(struct led_classdev_flash *flash)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	int i;

	for (i = 0; i < LED_FLASH_MAX_SYSFS_GROUPS; ++i)
		if (flash->sysfs_groups[i])
			sysfs_remove_group(&led_cdev->dev->kobj,
						flash->sysfs_groups[i]);
}

static int led_flash_create_sysfs_groups(struct led_classdev_flash *flash)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	const struct led_flash_ops *ops = flash->ops;
	int ret, num_sysfs_groups = 0;

	memset(flash->sysfs_groups, 0, sizeof(*flash->sysfs_groups) *
						LED_FLASH_MAX_SYSFS_GROUPS);

	ret = sysfs_create_group(&led_cdev->dev->kobj, &led_flash_strobe_group);
	if (ret < 0)
		goto err_create_group;
	flash->sysfs_groups[num_sysfs_groups++] = &led_flash_strobe_group;

	if (flash->indicator_brightness) {
		ret = sysfs_create_group(&led_cdev->dev->kobj,
					&led_flash_indicator_group);
		if (ret < 0)
			goto err_create_group;
		flash->sysfs_groups[num_sysfs_groups++] =
					&led_flash_indicator_group;
	}

	if (ops->flash_brightness_set) {
		ret = sysfs_create_group(&led_cdev->dev->kobj,
					&led_flash_brightness_group);
		if (ret < 0)
			goto err_create_group;
		flash->sysfs_groups[num_sysfs_groups++] =
					&led_flash_brightness_group;
	}

	if (ops->timeout_set) {
		ret = sysfs_create_group(&led_cdev->dev->kobj,
					&led_flash_timeout_group);
		if (ret < 0)
			goto err_create_group;
		flash->sysfs_groups[num_sysfs_groups++] =
					&led_flash_timeout_group;
	}

	if (ops->fault_get) {
		ret = sysfs_create_group(&led_cdev->dev->kobj,
					&led_flash_fault_group);
		if (ret < 0)
			goto err_create_group;
		flash->sysfs_groups[num_sysfs_groups++] =
					&led_flash_fault_group;
	}

	if (flash->has_external_strobe) {
		ret = sysfs_create_group(&led_cdev->dev->kobj,
					&led_flash_external_strobe_group);
		if (ret < 0)
			goto err_create_group;
		flash->sysfs_groups[num_sysfs_groups++] =
					&led_flash_external_strobe_group;
	}

	return 0;

err_create_group:
	led_flash_remove_sysfs_groups(flash);
	return ret;
}

int led_classdev_flash_register(struct device *parent,
				struct led_classdev_flash *flash,
				struct device_node *node)
{
	struct led_classdev *led_cdev;
	const struct led_flash_ops *ops;
	int ret = -EINVAL;

	if (!flash)
		return -EINVAL;

	led_cdev = &flash->led_cdev;

	/* Torch capability is default for every LED Flash Class device */
	led_cdev->flags |= LED_DEV_CAP_TORCH;

	if (led_cdev->flags & LED_DEV_CAP_FLASH) {
		if (!led_cdev->torch_brightness_set)
			return -EINVAL;

		ops = flash->ops;
		if (!ops || !ops->strobe_set)
			return -EINVAL;

		if ((led_cdev->flags & LED_DEV_CAP_INDICATOR) &&
		    (!flash->indicator_brightness ||
		     !ops->indicator_brightness_set))
			return -EINVAL;

		led_cdev->flash_resume = led_flash_resume;
	}

	/* Register led class device */
	ret = led_classdev_register(parent, led_cdev);
	if (ret < 0)
		return -EINVAL;

	/* Register in the flash manager if there is related data to parse */
	if (node) {
		ret = led_flash_manager_register_flash(flash, node);
		if (ret < 0)
			goto err_flash_manager_register;
	}

	/* Create flash led specific sysfs attributes */
	ret = led_flash_create_sysfs_groups(flash);
	if (ret < 0)
		goto err_create_sysfs_groups;

	return 0;

err_create_sysfs_groups:
	led_flash_manager_unregister_flash(flash);
err_flash_manager_register:
	led_classdev_unregister(led_cdev);

	return ret;
}
EXPORT_SYMBOL_GPL(led_classdev_flash_register);

void led_classdev_flash_unregister(struct led_classdev_flash *flash)
{
	led_flash_remove_sysfs_groups(flash);
	led_flash_manager_unregister_flash(flash);
	led_classdev_unregister(&flash->led_cdev);
}
EXPORT_SYMBOL_GPL(led_classdev_flash_unregister);

int led_set_flash_strobe(struct led_classdev_flash *flash, bool state)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	int ret = 0;

	if (flash->external_strobe)
		return -EBUSY;

	/* strobe can be stopped without flash manager involvement */
	if (!state)
		return call_flash_op(flash, strobe_set, state);

	/*
	 * Flash manager needs to be involved in setting flash
	 * strobe if there were strobe gates defined in the
	 * device tree binding. This call blocks the caller for
	 * the current flash timeout period if state == true and
	 * the flash led device depends on shared muxes. Locking is
	 * required for assuring that nobody will reconfigure muxes
	 * in the meantime.
	 */
	if ((led_cdev->flags & LED_DEV_CAP_FL_MANAGER))
		ret = led_flash_manager_setup_strobe(flash, false);
	else
		ret = call_flash_op(flash, strobe_set, true);

	return ret;
}
EXPORT_SYMBOL_GPL(led_set_flash_strobe);

int led_get_flash_strobe(struct led_classdev_flash *flash, bool *state)
{
	return call_flash_op(flash, strobe_get, state);
}
EXPORT_SYMBOL_GPL(led_get_flash_strobe);

void led_clamp_align(struct led_flash_setting *s)
{
	u32 v, offset;

	v = s->val + s->step / 2;
	v = clamp(v, s->min, s->max);
	offset = v - s->min;
	offset = s->step * (offset / s->step);
	s->val = s->min + offset;
}

int led_set_flash_timeout(struct led_classdev_flash *flash, u32 timeout)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct led_flash_setting *s = &flash->timeout;
	int ret = 0;

	s->val = timeout;
	led_clamp_align(s);

	if (!(led_cdev->flags & LED_SUSPENDED))
		ret = call_flash_op(flash, timeout_set, s->val);

	return ret;
}
EXPORT_SYMBOL_GPL(led_set_flash_timeout);

int led_get_flash_fault(struct led_classdev_flash *flash, u32 *fault)
{
	return call_flash_op(flash, fault_get, fault);
}
EXPORT_SYMBOL_GPL(led_get_flash_fault);

int led_set_external_strobe(struct led_classdev_flash *flash, bool enable)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	int ret;

	if (flash->has_external_strobe) {
		/*
		 * Some flash led devices need altering their register
		 * settings to start listen to the external strobe signal.
		 */
		if (has_flash_op(flash, external_strobe_set)) {
			ret = call_flash_op(flash, external_strobe_set, enable);
			if (ret < 0)
				return ret;
		}

		flash->external_strobe = enable;

		/*
		 * Flash manager needs to be involved in setting external
		 * strobe mode if there were strobe gates defined in the
		 * device tree binding. This call blocks the caller for
		 * the current flash timeout period if enable == true and
		 * the flash led device depends on shared muxes. Locking is
		 * required for assuring that nobody will reconfigure muxes
		 * while the flash device is awaiting external strobe signal.
		 */
		if (enable && (led_cdev->flags & LED_DEV_CAP_FL_MANAGER)) {
			ret = led_flash_manager_setup_strobe(flash, true);
			if (ret < 0)
				return ret;
		}
	} else if (enable) {
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(led_set_external_strobe);

int led_set_flash_brightness(struct led_classdev_flash *flash,
				u32 brightness)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct led_flash_setting *s = &flash->brightness;
	int ret = 0;

	s->val = brightness;
	led_clamp_align(s);

	if (!(led_cdev->flags & LED_SUSPENDED))
		ret = call_flash_op(flash, flash_brightness_set, s->val);

	return ret;
}
EXPORT_SYMBOL_GPL(led_set_flash_brightness);

int led_update_flash_brightness(struct led_classdev_flash *flash)
{
	struct led_flash_setting *s = &flash->brightness;
	u32 brightness;
	int ret = 0;

	if (has_flash_op(flash, flash_brightness_get)) {
		ret = call_flash_op(flash, flash_brightness_get,
						&brightness);
		if (ret < 0)
			return ret;
		s->val = brightness;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(led_update_flash_brightness);

int led_set_indicator_brightness(struct led_classdev_flash *flash,
					u32 brightness)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct led_flash_setting *s = flash->indicator_brightness;
	int ret = 0;

	if (!s)
		return -EINVAL;

	s->val = brightness;
	led_clamp_align(s);

	if (!(led_cdev->flags & LED_SUSPENDED))
		ret = call_flash_op(flash, indicator_brightness_set, s->val);

	return ret;
}
EXPORT_SYMBOL_GPL(led_set_indicator_brightness);

int led_update_indicator_brightness(struct led_classdev_flash *flash)
{
	struct led_flash_setting *s = flash->indicator_brightness;
	u32 brightness;
	int ret = 0;

	if (!s)
		return -EINVAL;

	if (has_flash_op(flash, indicator_brightness_get)) {
		ret = call_flash_op(flash, indicator_brightness_get,
							&brightness);
		if (ret < 0)
			return ret;
		s->val = brightness;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(led_update_indicator_brightness);

MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Flash Class Interface");

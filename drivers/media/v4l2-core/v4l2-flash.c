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

#include <linux/led-class-flash.h>
#include <linux/mutex.h>
#include <linux/of_led_flash_manager.h>
#include <linux/slab.h>
#include <media/v4l2-flash.h>

#define call_flash_op(v4l2_flash, op, args...)			\
		(v4l2_flash->ops->op  ?				\
			v4l2_flash->ops->op(args) :		\
			-EINVAL)

static struct v4l2_device *v4l2_dev;
static int registered_flashes;

static inline enum led_brightness v4l2_flash_intensity_to_led_brightness(
					struct v4l2_ctrl_config *config,
					s32 intensity)
{
	return ((intensity - config->min) / config->step) + 1;
}

static inline s32 v4l2_flash_led_brightness_to_intensity(
					struct v4l2_ctrl_config *config,
					enum led_brightness brightness)
{
	return ((brightness - 1) * config->step) + config->min;
}

static int v4l2_flash_g_volatile_ctrl(struct v4l2_ctrl *c)

{
	struct v4l2_flash *v4l2_flash = v4l2_ctrl_to_v4l2_flash(c);
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct v4l2_flash_ctrl_config *config = &v4l2_flash->config;
	struct v4l2_flash_ctrl *ctrl = &v4l2_flash->ctrl;
	bool is_strobing;
	int ret;

	switch (c->id) {
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		/*
		 * Update torch brightness only if in TORCH_MODE,
		 * as otherwise brightness_update op returns 0,
		 * which would spuriously inform user space that
		 * V4L2_CID_FLASH_TORCH_INTENSITY control value
		 * has changed.
		 */
		if (ctrl->led_mode->val == V4L2_FLASH_LED_MODE_TORCH) {
			ret = call_flash_op(v4l2_flash, torch_brightness_update,
							led_cdev);
			if (ret < 0)
				return ret;
			ctrl->torch_intensity->val =
				v4l2_flash_led_brightness_to_intensity(
						&config->torch_intensity,
						led_cdev->brightness);
		}
		return 0;
	case V4L2_CID_FLASH_INTENSITY:
		ret = call_flash_op(v4l2_flash, flash_brightness_update,
					flash);
		if (ret < 0)
			return ret;
		/* no conversion is needed */
		c->val = flash->brightness.val;
		return 0;
	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		ret = call_flash_op(v4l2_flash, indicator_brightness_update,
						flash);
		if (ret < 0)
			return ret;
		/* no conversion is needed */
		c->val = flash->indicator_brightness->val;
		return 0;
	case V4L2_CID_FLASH_STROBE_STATUS:
		ret = call_flash_op(v4l2_flash, strobe_get, flash,
							&is_strobing);
		if (ret < 0)
			return ret;
		c->val = is_strobing;
		return 0;
	case V4L2_CID_FLASH_FAULT:
		/* led faults map directly to V4L2 flash faults */
		ret = call_flash_op(v4l2_flash, fault_get, flash, &c->val);
		return ret;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		c->val = flash->external_strobe;
		return 0;
	case V4L2_CID_FLASH_STROBE_PROVIDER:
		c->val = flash->strobe_provider_id;
		return 0;
	default:
		return -EINVAL;
	}
}

static int v4l2_flash_s_ctrl(struct v4l2_ctrl *c)
{
	struct v4l2_flash *v4l2_flash = v4l2_ctrl_to_v4l2_flash(c);
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct v4l2_flash_ctrl *ctrl = &v4l2_flash->ctrl;
	struct v4l2_flash_ctrl_config *config = &v4l2_flash->config;
	enum led_brightness torch_brightness;
	bool external_strobe;
	int ret;

	switch (c->id) {
	case V4L2_CID_FLASH_LED_MODE:
		switch (c->val) {
		case V4L2_FLASH_LED_MODE_NONE:
			call_flash_op(v4l2_flash, torch_brightness_set,
							&flash->led_cdev, 0);
			return call_flash_op(v4l2_flash, strobe_set, flash,
							false);
		case V4L2_FLASH_LED_MODE_FLASH:
			/* Turn off torch LED */
			call_flash_op(v4l2_flash, torch_brightness_set,
							&flash->led_cdev, 0);
			external_strobe = (ctrl->source->val ==
					V4L2_FLASH_STROBE_SOURCE_EXTERNAL);
			return call_flash_op(v4l2_flash, external_strobe_set,
						flash, external_strobe);
		case V4L2_FLASH_LED_MODE_TORCH:
			/* Stop flash strobing */
			ret = call_flash_op(v4l2_flash, strobe_set, flash,
							false);
			if (ret)
				return ret;

			torch_brightness =
				v4l2_flash_intensity_to_led_brightness(
						&config->torch_intensity,
						ctrl->torch_intensity->val);
			call_flash_op(v4l2_flash, torch_brightness_set,
					&flash->led_cdev, torch_brightness);
			return ret;
		}
		break;
	case V4L2_CID_FLASH_STROBE_SOURCE:
		external_strobe = (c->val == V4L2_FLASH_STROBE_SOURCE_EXTERNAL);

		return call_flash_op(v4l2_flash, external_strobe_set, flash,
							external_strobe);
	case V4L2_CID_FLASH_STROBE:
		if (ctrl->led_mode->val != V4L2_FLASH_LED_MODE_FLASH ||
		    ctrl->source->val != V4L2_FLASH_STROBE_SOURCE_SOFTWARE)
			return -EINVAL;
		return call_flash_op(v4l2_flash, strobe_set, flash, true);
	case V4L2_CID_FLASH_STROBE_STOP:
		return call_flash_op(v4l2_flash, strobe_set, flash, false);
	case V4L2_CID_FLASH_TIMEOUT:
		return call_flash_op(v4l2_flash, timeout_set, flash, c->val);
	case V4L2_CID_FLASH_INTENSITY:
		/* no conversion is needed */
		return call_flash_op(v4l2_flash, flash_brightness_set, flash,
								c->val);
	case V4L2_CID_FLASH_INDICATOR_INTENSITY:
		/* no conversion is needed */
		return call_flash_op(v4l2_flash, indicator_brightness_set,
						flash, c->val);
	case V4L2_CID_FLASH_TORCH_INTENSITY:
		/*
		 * If not in MODE_TORCH don't call led-class brightness_set
		 * op, as it would result in turning the torch led on.
		 * Instead the value is cached only and will be written
		 * to the device upon transition to MODE_TORCH.
		 */
		if (ctrl->led_mode->val == V4L2_FLASH_LED_MODE_TORCH) {
			torch_brightness =
				v4l2_flash_intensity_to_led_brightness(
						&config->torch_intensity,
						ctrl->torch_intensity->val);
			call_flash_op(v4l2_flash, torch_brightness_set,
					&flash->led_cdev, torch_brightness);
		}
		return 0;
	case V4L2_CID_FLASH_STROBE_PROVIDER:
		flash->strobe_provider_id = c->val;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops v4l2_flash_ctrl_ops = {
	.g_volatile_ctrl = v4l2_flash_g_volatile_ctrl,
	.s_ctrl = v4l2_flash_s_ctrl,
};

static int v4l2_flash_init_strobe_providers_menu(struct v4l2_flash *v4l2_flash)
{
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct led_flash_strobe_provider *provider;
	struct v4l2_ctrl *ctrl;
	int i = 0;

	v4l2_flash->strobe_providers_menu =
			kzalloc(sizeof(char *) * (flash->num_strobe_providers),
					GFP_KERNEL);
	if (!v4l2_flash->strobe_providers_menu)
		return -ENOMEM;

	list_for_each_entry(provider, &flash->strobe_providers, list)
		v4l2_flash->strobe_providers_menu[i++] =
						(char *) provider->name;

	ctrl = v4l2_ctrl_new_std_menu_items(
		&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
		V4L2_CID_FLASH_STROBE_PROVIDER,
		flash->num_strobe_providers - 1,
		0, 0,
		(const char * const *) v4l2_flash->strobe_providers_menu);

	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	return 0;
}

static int v4l2_flash_init_controls(struct v4l2_flash *v4l2_flash)

{
	struct led_classdev_flash *flash = v4l2_flash->flash;
	const struct led_flash_ops *flash_ops = flash->ops;
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct v4l2_flash_ctrl_config *config = &v4l2_flash->config;
	struct v4l2_ctrl *ctrl;
	struct v4l2_ctrl_config *ctrl_cfg;
	bool has_flash = led_cdev->flags & LED_DEV_CAP_FLASH;
	bool has_indicator = led_cdev->flags & LED_DEV_CAP_INDICATOR;
	bool has_strobe_providers = (flash->num_strobe_providers > 1);
	unsigned int mask;
	int ret, max, num_ctrls;

	num_ctrls = has_flash ? 5 : 2;
	if (has_flash) {
		if (flash_ops->flash_brightness_set)
			++num_ctrls;
		if (flash_ops->timeout_set)
			++num_ctrls;
		if (flash_ops->strobe_get)
			++num_ctrls;
		if (has_indicator)
			++num_ctrls;
		if (config->flash_faults)
			++num_ctrls;
		if (has_strobe_providers)
			++num_ctrls;
	}

	v4l2_ctrl_handler_init(&v4l2_flash->hdl, num_ctrls);

	mask = 1 << V4L2_FLASH_LED_MODE_NONE |
	       1 << V4L2_FLASH_LED_MODE_TORCH;
	if (flash)
		mask |= 1 << V4L2_FLASH_LED_MODE_FLASH;

	/* Configure FLASH_LED_MODE ctrl */
	v4l2_flash->ctrl.led_mode = v4l2_ctrl_new_std_menu(
			&v4l2_flash->hdl,
			&v4l2_flash_ctrl_ops, V4L2_CID_FLASH_LED_MODE,
			V4L2_FLASH_LED_MODE_TORCH, ~mask,
			V4L2_FLASH_LED_MODE_NONE);

	/* Configure TORCH_INTENSITY ctrl */
	ctrl_cfg = &config->torch_intensity;
	ctrl = v4l2_ctrl_new_std(&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
				 V4L2_CID_FLASH_TORCH_INTENSITY,
				 ctrl_cfg->min, ctrl_cfg->max,
				 ctrl_cfg->step, ctrl_cfg->def);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_flash->ctrl.torch_intensity = ctrl;

	if (has_flash) {
		/* Configure FLASH_STROBE_SOURCE ctrl */
		mask = 1 << V4L2_FLASH_STROBE_SOURCE_SOFTWARE;

		if (flash->has_external_strobe) {
			mask |= 1 << V4L2_FLASH_STROBE_SOURCE_EXTERNAL;
			max = V4L2_FLASH_STROBE_SOURCE_EXTERNAL;
		} else {
			max = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;
		}

		v4l2_flash->ctrl.source = v4l2_ctrl_new_std_menu(
					&v4l2_flash->hdl,
					&v4l2_flash_ctrl_ops,
					V4L2_CID_FLASH_STROBE_SOURCE,
					max,
					~mask,
					V4L2_FLASH_STROBE_SOURCE_SOFTWARE);
		if (v4l2_flash->ctrl.source)
			v4l2_flash->ctrl.source->flags |=
						V4L2_CTRL_FLAG_VOLATILE;

		/* Configure FLASH_STROBE ctrl */
		ctrl = v4l2_ctrl_new_std(&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
					  V4L2_CID_FLASH_STROBE, 0, 1, 1, 0);

		/* Configure FLASH_STROBE_STOP ctrl */
		ctrl = v4l2_ctrl_new_std(&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
					  V4L2_CID_FLASH_STROBE_STOP,
					  0, 1, 1, 0);

		/* Configure FLASH_STROBE_STATUS ctrl */
		ctrl = v4l2_ctrl_new_std(&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
					 V4L2_CID_FLASH_STROBE_STATUS,
					 0, 1, 1, 1);

		if (flash_ops->strobe_get)
			if (ctrl)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
					       V4L2_CTRL_FLAG_READ_ONLY;

		if (flash_ops->timeout_set) {
			/* Configure FLASH_TIMEOUT ctrl */
			ctrl_cfg = &config->flash_timeout;
			ctrl = v4l2_ctrl_new_std(
					&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
					V4L2_CID_FLASH_TIMEOUT, ctrl_cfg->min,
					ctrl_cfg->max, ctrl_cfg->step,
					ctrl_cfg->def);
			if (ctrl)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}

		if (flash_ops->flash_brightness_set) {
			/* Configure FLASH_INTENSITY ctrl */
			ctrl_cfg = &config->flash_intensity;
			ctrl = v4l2_ctrl_new_std(
					&v4l2_flash->hdl,
					&v4l2_flash_ctrl_ops,
					V4L2_CID_FLASH_INTENSITY,
					ctrl_cfg->min, ctrl_cfg->max,
					ctrl_cfg->step, ctrl_cfg->def);
			if (ctrl)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}

		if (config->flash_faults) {
			/* Configure FLASH_FAULT ctrl */
			ctrl = v4l2_ctrl_new_std(&v4l2_flash->hdl,
						 &v4l2_flash_ctrl_ops,
						 V4L2_CID_FLASH_FAULT, 0,
						 config->flash_faults,
						 0, 0);
			if (ctrl)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
					       V4L2_CTRL_FLAG_READ_ONLY;
		}
		if (has_indicator) {
			/* Configure FLASH_INDICATOR_INTENSITY ctrl */
			ctrl_cfg = &config->indicator_intensity;
			ctrl = v4l2_ctrl_new_std(
					&v4l2_flash->hdl, &v4l2_flash_ctrl_ops,
					V4L2_CID_FLASH_INDICATOR_INTENSITY,
					ctrl_cfg->min, ctrl_cfg->max,
					ctrl_cfg->step, ctrl_cfg->def);
			if (ctrl)
				ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
		}

		if (has_strobe_providers) {
			/* Configure V4L2_CID_FLASH_STROBE_PROVIDERS ctrl */
			ret = v4l2_flash_init_strobe_providers_menu(v4l2_flash);
			if (ret < 0)
				goto error_free_handler;
		}
	}

	if (v4l2_flash->hdl.error) {
		ret = v4l2_flash->hdl.error;
		goto error_free_handler;
	}

	ret = v4l2_ctrl_handler_setup(&v4l2_flash->hdl);
	if (ret < 0)
		goto error_free_handler;

	v4l2_flash->sd.ctrl_handler = &v4l2_flash->hdl;

	return 0;

error_free_handler:
	v4l2_ctrl_handler_free(&v4l2_flash->hdl);
	return ret;
}

/*
 * V4L2 subdev internal operations
 */

static int v4l2_flash_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_flash *v4l2_flash = v4l2_subdev_to_v4l2_flash(sd);
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct led_classdev *led_cdev = &flash->led_cdev;

	mutex_lock(&led_cdev->led_lock);
	call_flash_op(v4l2_flash, sysfs_lock, led_cdev);
	mutex_unlock(&led_cdev->led_lock);

	return 0;
}

static int v4l2_flash_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_flash *v4l2_flash = v4l2_subdev_to_v4l2_flash(sd);
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct led_classdev *led_cdev = &flash->led_cdev;

	mutex_lock(&led_cdev->led_lock);
	call_flash_op(v4l2_flash, sysfs_unlock, led_cdev);
	mutex_unlock(&led_cdev->led_lock);

	return 0;
}

int v4l2_flash_register(struct v4l2_flash *v4l2_flash)
{
	struct led_classdev_flash *flash = v4l2_flash->flash;
	struct led_classdev *led_cdev = &flash->led_cdev;
	int ret;

	if (!v4l2_dev) {
		v4l2_dev = kzalloc(sizeof(*v4l2_dev), GFP_KERNEL);
		if (!v4l2_dev)
			return -ENOMEM;

		strlcpy(v4l2_dev->name, "v4l2-flash-manager",
						sizeof(v4l2_dev->name));
		ret = v4l2_device_register(NULL, v4l2_dev);
		if (ret < 0) {
			dev_err(led_cdev->dev->parent,
				 "Failed to register v4l2_device: %d\n", ret);
			goto err_v4l2_device_register;
		}
	}

	ret = v4l2_device_register_subdev(v4l2_dev, &v4l2_flash->sd);
	if (ret < 0) {
		dev_err(led_cdev->dev->parent,
			 "Failed to register v4l2_subdev: %d\n", ret);
		goto err_v4l2_device_register;
	}

	ret = v4l2_device_register_subdev_node(&v4l2_flash->sd, v4l2_dev);
	if (ret < 0) {
		dev_err(led_cdev->dev->parent,
			 "Failed to register v4l2_subdev node: %d\n", ret);
		goto err_register_subdev_node;
	}

	++registered_flashes;

	return 0;

err_register_subdev_node:
	v4l2_device_unregister_subdev(&v4l2_flash->sd);
err_v4l2_device_register:
	kfree(v4l2_flash->strobe_providers_menu);
	if (v4l2_dev && registered_flashes == 0) {
		v4l2_device_unregister(v4l2_dev);
		kfree(v4l2_dev);
		v4l2_dev = NULL;
	}

	return ret;
}

static void v4l2_flash_unregister(struct v4l2_flash *v4l2_flash)
{
	if (registered_flashes == 0)
		return;

	v4l2_device_unregister_subdev(&v4l2_flash->sd);

	--registered_flashes;

	if (registered_flashes == 0) {
		v4l2_device_unregister(v4l2_dev);
		kfree(v4l2_dev);
		v4l2_dev = NULL;
	}
}

static const struct v4l2_subdev_internal_ops v4l2_flash_subdev_internal_ops = {
	.open = v4l2_flash_open,
	.close = v4l2_flash_close,
};

static const struct v4l2_subdev_core_ops v4l2_flash_core_ops = {
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
};

static const struct v4l2_subdev_ops v4l2_flash_subdev_ops = {
	.core = &v4l2_flash_core_ops,
};

int v4l2_flash_init(struct led_classdev_flash *flash,
		    struct v4l2_flash_ctrl_config *config,
		    const struct v4l2_flash_ops *flash_ops,
		    struct v4l2_flash **out_flash)
{
	struct v4l2_flash *v4l2_flash;
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct v4l2_subdev *sd;
	int ret;

	if (!flash || !config || !out_flash)
		return -EINVAL;

	v4l2_flash = kzalloc(sizeof(*v4l2_flash), GFP_KERNEL);
	if (!v4l2_flash)
		return -ENOMEM;

	sd = &v4l2_flash->sd;
	v4l2_flash->flash = flash;
	v4l2_flash->ops = flash_ops;
	sd->dev = led_cdev->dev->parent;
	v4l2_subdev_init(sd, &v4l2_flash_subdev_ops);
	sd->internal_ops = &v4l2_flash_subdev_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), led_cdev->name);

	v4l2_flash->config = *config;
	ret = v4l2_flash_init_controls(v4l2_flash);
	if (ret < 0)
		goto err_init_controls;

	ret = media_entity_init(&sd->entity, 0, NULL, 0);
	if (ret < 0)
		goto err_init_entity;

	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_FLASH;

	ret = v4l2_flash_register(v4l2_flash);
	if (ret < 0)
		goto err_init_entity;

	*out_flash = v4l2_flash;

	return 0;

err_init_entity:
	media_entity_cleanup(&sd->entity);
err_init_controls:
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	kfree(v4l2_flash);

	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_flash_init);

void v4l2_flash_release(struct v4l2_flash *v4l2_flash)
{
	if (!v4l2_flash)
		return;

	v4l2_flash_unregister(v4l2_flash);
	v4l2_ctrl_handler_free(v4l2_flash->sd.ctrl_handler);
	media_entity_cleanup(&v4l2_flash->sd.entity);
	kfree(v4l2_flash->strobe_providers_menu);
	kfree(v4l2_flash);
}
EXPORT_SYMBOL_GPL(v4l2_flash_release);

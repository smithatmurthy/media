/*
 *  LED Flash Manager for maintaining LED Flash Class devices
 *  along with their corresponding muxex, faciliating dynamic
 *  reconfiguration of the strobe signal source.
 *
 *	Copyright (C) 2014 Samsung Electronics Co., Ltd
 *	Author: Jacek Anaszewski <j.anaszewski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation."
 */

#include <linux/delay.h>
#include <linux/led-class-flash.h>
#include <linux/led-flash-gpio-mux.h>
#include <linux/led-flash-manager.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/of_led_flash_manager.h>
#include <linux/slab.h>

static LIST_HEAD(flash_list);
static LIST_HEAD(mux_bound_list);
static LIST_HEAD(mux_waiting_list);
static DEFINE_MUTEX(fm_lock);

static ssize_t strobe_provider_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);
	unsigned long provider_id;
	ssize_t ret;

	mutex_lock(&led_cdev->led_lock);

	if (led_sysfs_is_locked(led_cdev)) {
		ret = -EBUSY;
		goto unlock;
	}

	ret = kstrtoul(buf, 10, &provider_id);
	if (ret)
		goto unlock;

	if (provider_id > flash->num_strobe_providers - 1) {
		ret = -ERANGE;
		goto unlock;
	}

	flash->strobe_provider_id = provider_id;

	ret = size;
unlock:
	mutex_unlock(&led_cdev->led_lock);
	return ret;
}

static ssize_t strobe_provider_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", flash->strobe_provider_id);
}
static DEVICE_ATTR_RW(strobe_provider);

static ssize_t available_strobe_providers_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_flash_strobe_provider *provider =
		container_of(attr, struct led_flash_strobe_provider, attr);
	const char *no_name = "undefined";
	const char *provider_name;

	provider_name = provider->name ? provider->name :
					 no_name;

	return sprintf(buf, "%s\n", provider_name);
}

static ssize_t blocking_strobe_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct led_classdev_flash *flash = lcdev_to_flash(led_cdev);

	return sprintf(buf, "%u\n", !!flash->num_shared_muxes);
}
static DEVICE_ATTR_RO(blocking_strobe);

static int led_flash_manager_create_providers_attrs(
					struct led_classdev_flash *flash)
{
	struct led_flash_strobe_provider *provider;
	struct led_classdev *led_cdev = &flash->led_cdev;
	int cnt_attr = 0;
	int ret;

	list_for_each_entry(provider, &flash->strobe_providers, list) {
		provider->attr.show = available_strobe_providers_show;
		provider->attr.attr.mode = S_IRUGO;

		sprintf(provider->attr_name, "strobe_provider%d",
							cnt_attr++);
		provider->attr.attr.name = provider->attr_name;

		sysfs_attr_init(&provider->attr.attr);

		ret = sysfs_create_file(&led_cdev->dev->kobj,
						&provider->attr.attr);
		if (ret < 0)
			goto error_create_attr;

		provider->attr_registered = true;
	}

	/*
	 * strobe_provider attribute is required only if there have been more
	 * than one strobe source defined for the LED Flash Class device.
	 */
	if (cnt_attr > 1) {
		ret = sysfs_create_file(&led_cdev->dev->kobj,
					&dev_attr_strobe_provider.attr);
		if (ret < 0)
			goto error_create_attr;
	}

	return 0;

error_create_attr:
	list_for_each_entry(provider, &flash->strobe_providers, list) {
		if (!provider->attr_registered)
			break;
		sysfs_remove_file(&led_cdev->dev->kobj, &provider->attr.attr);
	}

	return ret;
}

static void led_flash_manager_remove_providers_attrs(
					struct led_classdev_flash *flash)
{
	struct led_flash_strobe_provider *provider;
	struct led_classdev *led_cdev = &flash->led_cdev;
	int cnt_attr = 0;

	list_for_each_entry(provider, &flash->strobe_providers, list) {
		if (!provider->attr_registered)
			break;
		sysfs_remove_file(&led_cdev->dev->kobj, &provider->attr.attr);
		provider->attr_registered = false;
		++cnt_attr;
	}

	/*
	 * If there was more than one strobe_providerN attr to remove
	 * than there is also strobe_provider attr to remove.
	 */
	if (cnt_attr > 1)
		sysfs_remove_file(&led_cdev->dev->kobj,
				  &dev_attr_strobe_provider.attr);
}

/* Return mux associated with gate */
static struct led_flash_mux *led_flash_manager_get_mux_by_gate(
					struct led_flash_strobe_gate *gate,
					struct list_head *mux_list)
{
	struct led_flash_mux *mux;

	list_for_each_entry(mux, mux_list, list)
		if (mux->node == gate->mux_node)
			return mux;

	return NULL;
}

/* Setup all muxes in the gate list */
static int led_flash_manager_setup_muxes(struct led_classdev_flash *flash,
					  struct list_head *gate_list)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	struct led_flash_strobe_gate *gate;
	struct led_flash_mux *mux;
	struct device *dev = led_cdev->dev->parent;
	int ret = 0;

	list_for_each_entry(gate, gate_list, list) {
		mux = led_flash_manager_get_mux_by_gate(gate, &mux_bound_list);
		if (!mux) {
			dev_err(dev, "Flash mux not bound (%s)\n",
				gate->mux_node->name);
			return -ENODEV;
		}

		ret = mux->ops->select_line(gate->line_id,
						mux->private_data);
	}

	return ret;
}

/*
 * Setup all muxes required to open the route
 * to the external strobe signal provider
 */
static int led_flash_manager_select_strobe_provider(
					struct led_classdev_flash *flash,
					int provider_id)
{
	struct led_flash_strobe_provider *provider;
	int ret, provider_cnt = 0;

	list_for_each_entry(provider, &flash->strobe_providers, list)
		if (provider_cnt++ == provider_id) {
			ret = led_flash_manager_setup_muxes(flash,
						&provider->strobe_gates);
			return ret;
		}

	return -EINVAL;
}

/*
 * Setup all muxes required to open the route
 * either to software or external strobe source.
 */
static int led_flash_manager_set_external_strobe(
					struct led_classdev_flash *flash,
					bool external)
{
	int ret;

	if (external)
		ret = led_flash_manager_select_strobe_provider(flash,
						flash->strobe_provider_id);
	else
		ret = led_flash_manager_setup_muxes(flash,
						&flash->software_strobe_gates);

	return ret;
}

/* Notify flash manager that async mux is available. */
int led_flash_manager_bind_async_mux(struct led_flash_mux *async_mux)
{
	struct led_flash_mux *mux;
	bool mux_found = false;
	int ret = 0;

	if (!async_mux)
		return -EINVAL;

	mutex_lock(&fm_lock);

	/*
	 * Check whether the LED Flash Class device using this
	 * mux has been already registered in the flash manager.
	 */
	list_for_each_entry(mux, &mux_waiting_list, list)
		if (async_mux->node == mux->node) {
			/* Move the mux to the bound muxes list */

			list_move(&mux->list, &mux_bound_list);
			mux_found = true;

			if (!try_module_get(async_mux->owner)) {
				ret = -ENODEV;
				goto unlock;
			}

			break;
		}

	if (!mux_found) {
		/* This is a new mux - create its representation */
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux) {
			ret = -ENOMEM;
			goto unlock;
		}

		INIT_LIST_HEAD(&mux->refs);

		/* Add the mux to the bound list. */
		list_add(&mux->list, &mux_bound_list);
	}

	mux->ops = async_mux->ops;
	mux->node = async_mux->node;
	mux->owner = async_mux->owner;

unlock:
	mutex_unlock(&fm_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(led_flash_manager_bind_async_mux);

int led_flash_manager_unbind_async_mux(struct device_node *mux_node)
{
	struct led_flash_mux *mux;
	int ret = -ENODEV;

	mutex_lock(&fm_lock);

	/*
	 * Mux can be unbound only when is not used by any
	 * flash led device, otherwise this is erroneous call.
	 */
	list_for_each_entry(mux, &mux_waiting_list, list)
		if (mux->node == mux_node) {
			list_move(&mux->list, &mux_waiting_list);
			ret = 0;
			break;
		}

	mutex_unlock(&fm_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(led_flash_manager_unbind_async_mux);

struct led_flash_mux_ops gpio_mux_ops = {
	.select_line = led_flash_gpio_mux_select_line,
	.release_private_data = led_flash_gpio_mux_release,
};

static int __add_mux_ref(struct led_flash_mux_ref *ref,
				struct led_flash_mux *mux)
{
	struct led_flash_mux_ref *r;
	int ret;

	/*
	 * A flash can be associated with a mux through
	 * more than one gate - increment mux reference
	 * count in such a case.
	 */
	list_for_each_entry(r, &mux->refs, list)
		if (r->flash == ref->flash)
			return 0;

	/* protect async mux against rmmod */
	if (mux->owner) {
		ret = try_module_get(mux->owner);
		if (ret < 0)
			return ret;
	}

	list_add(&ref->list, &mux->refs);
	++mux->num_refs;

	if (mux->num_refs == 2) {
		list_for_each_entry(r, &mux->refs, list) {
			++r->flash->num_shared_muxes;
		}
		return 0;
	}

	if (mux->num_refs > 2)
		++ref->flash->num_shared_muxes;

	return 0;
}

static void __remove_mux_ref(struct led_flash_mux_ref *ref,
				struct led_flash_mux *mux)
{
	struct led_flash_mux_ref *r;

	/* decrement async mux refcount */
	if (mux->owner)
		module_put(mux->owner);

	list_del(&ref->list);
	--mux->num_refs;

	if (mux->num_refs == 1) {
		r = list_first_entry(&mux->refs, struct led_flash_mux_ref,
						list);
		--r->flash->num_shared_muxes;
		return;
	}

	if (mux->num_refs > 1)
		--ref->flash->num_shared_muxes;
}

/*
 * Parse mux node and add the mux it refers to either to waiting
 * or bound list depending on the mux type (gpio or asynchronous).
 */
static int led_flash_manager_parse_mux_node(struct led_classdev_flash *flash,
					    struct led_flash_strobe_gate *gate)
{
	struct led_flash_mux *mux;
	struct device_node *async_mux_node;
	struct led_flash_gpio_mux *gpio_mux;
	struct led_flash_mux_ref *ref;
	int ret = -EINVAL;

	/* Create flash ref to be added to the mux references list */
	ref = kzalloc(sizeof(*ref), GFP_KERNEL);
	if (!ref)
		return -ENOMEM;
	ref->flash = flash;

	/* if this is async mux update gate's mux_node accordingly */
	async_mux_node = of_parse_phandle(gate->mux_node, "mux-async", 0);
	if (async_mux_node)
		gate->mux_node = async_mux_node;

	/* Check if the mux isn't already on waiting list */
	list_for_each_entry(mux, &mux_waiting_list, list)
		if (mux->node == gate->mux_node)
			return __add_mux_ref(ref, mux);

	/* Check if the mux isn't already on bound list */
	list_for_each_entry(mux, &mux_bound_list, list)
		if (mux->node == gate->mux_node)
			return __add_mux_ref(ref, mux);

	/* This is a new mux - create its representation */
	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (!mux)
		return -ENOMEM;

	mux->node = gate->mux_node;

	INIT_LIST_HEAD(&mux->refs);

	/* Increment reference count */
	ret = __add_mux_ref(ref, mux);
	if (ret < 0)
		return ret;

	/*
	 * Check if this mux has its own driver
	 * or this is standard gpio mux.
	 */
	if (async_mux_node) {
		/* Add async mux to the waiting list */
		list_add(&mux->list, &mux_waiting_list);
	} else {
		/* Create default gpio mux */
		ret = led_flash_gpio_mux_create(&gpio_mux, gate->mux_node);
		if (ret < 0)
			goto err_gpio_mux_init;

		/* Register gpio mux device */
		mux->private_data = gpio_mux;
		mux->ops = &gpio_mux_ops;
		list_add(&mux->list, &mux_bound_list);
	}

	return 0;

err_gpio_mux_init:
	kfree(mux);
	kfree(ref);

	return ret;
}

static void led_flash_manager_release_mux(struct led_flash_mux *mux)
{
	if (mux->ops->release_private_data) {
		if (mux->ops->release_private_data)
			mux->ops->release_private_data(mux->private_data);
		list_del(&mux->list);
		kfree(mux);
	}
}

static void led_flash_manager_remove_flash_refs(
					struct led_classdev_flash *flash)
{
	struct led_flash_mux_ref *ref, *rn;
	struct led_flash_mux *mux, *mn;

	/*
	 * Remove references to the flash from
	 * all the muxes on the list.
	 */
	list_for_each_entry_safe(mux, mn, &mux_bound_list, list)
		/* Seek for matching flash ref */
		list_for_each_entry_safe(ref, rn, &mux->refs, list)
			if (ref->flash == flash) {
				/* Decrement reference count */
				__remove_mux_ref(ref, mux);
				kfree(ref);
				/*
				 * Release mux if there are no more
				 * references pointing to it.
				 */
				if (list_empty(&mux->refs))
					led_flash_manager_release_mux(mux);
			}
}

int led_flash_manager_setup_strobe(struct led_classdev_flash *flash,
				 bool external)
{
	u32 flash_timeout = flash->timeout.val / 1000;
	int ret;

	mutex_lock(&fm_lock);

	/* Setup muxes */
	ret = led_flash_manager_set_external_strobe(flash, external);
	if (ret < 0)
		goto unlock;

	/*
	 * Trigger software strobe under flash manager lock
	 * to make sure that nobody will reconfigure muxes
	 * in the meantime.
	 */
	if (!external) {
		ret = flash->ops->strobe_set(flash, true);
		if (ret < 0)
			goto unlock;
	}

	/*
	 * Hold lock for the time of strobing if the
	 * LED Flash Class device depends on shared muxes.
	 */
	if (flash->num_shared_muxes > 0) {
		msleep(flash_timeout);
		/*
		 * external strobe is turned on only for the time of
		 * this call if there are shared muxes involved
		 * in strobe signal routing.
		 */
		flash->external_strobe = false;
	}

unlock:
	mutex_unlock(&fm_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(led_flash_manager_setup_strobe);

int led_flash_manager_create_sysfs_attrs(struct led_classdev_flash *flash)
{
	struct led_classdev *led_cdev = &flash->led_cdev;
	int ret;

	/*
	 * Create sysfs attribtue for indicating if activation of
	 * software or external flash strobe will block the caller.
	 */
	sysfs_attr_init(&dev_attr_blocking_strobe.attr);
	ret = sysfs_create_file(&led_cdev->dev->kobj,
					&dev_attr_blocking_strobe.attr);
	if (ret < 0)
		return ret;

	/* Create strobe_providerN attributes */
	ret = led_flash_manager_create_providers_attrs(flash);
	if (ret < 0)
		goto err_create_provider_attrs;

	return 0;

err_create_provider_attrs:
	sysfs_remove_file(&led_cdev->dev->kobj,
				&dev_attr_blocking_strobe.attr);

	return ret;
}

void led_flash_manager_remove_sysfs_attrs(struct led_classdev_flash *flash)
{
	struct led_classdev *led_cdev = &flash->led_cdev;

	led_flash_manager_remove_providers_attrs(flash);
	sysfs_remove_file(&led_cdev->dev->kobj,
				&dev_attr_blocking_strobe.attr);
}

int led_flash_manager_register_flash(struct led_classdev_flash *flash,
				     struct device_node *node)
{
	struct led_flash_strobe_provider *provider;
	struct led_flash_strobe_gate *gate;
	struct led_classdev_flash *fl;
	struct led_classdev *led_cdev = &flash->led_cdev;
	int ret = 0;

	if (!flash || !node)
		return -EINVAL;

	mutex_lock(&fm_lock);

	/* Don't allow to register the same flash more than once */
	list_for_each_entry(fl, &flash_list, list)
		if (fl == flash)
			goto unlock;

	INIT_LIST_HEAD(&flash->software_strobe_gates);
	INIT_LIST_HEAD(&flash->strobe_providers);

	ret = of_led_flash_manager_parse_dt(flash, node);
	if (ret < 0)
		goto unlock;

	/*
	 * Register mux devices declared by the flash device
	 * if they have not been yet known to the flash manager.
	 */
	list_for_each_entry(gate, &flash->software_strobe_gates, list) {
		ret = led_flash_manager_parse_mux_node(flash, gate);
		if (ret < 0)
			goto err_parse_mux_node;
	}

	list_for_each_entry(provider, &flash->strobe_providers, list) {
		list_for_each_entry(gate, &provider->strobe_gates, list) {
			ret = led_flash_manager_parse_mux_node(flash, gate);
			if (ret < 0)
				goto err_parse_mux_node;
		}
		++flash->num_strobe_providers;
	}

	/*
	 * It doesn't make sense to register in the flash manager
	 * if there are no strobe providers defined.
	 */
	if (flash->num_strobe_providers == 0)
		goto unlock;

	led_cdev->flags |= LED_DEV_CAP_FL_MANAGER;
	flash->has_external_strobe = true;
	flash->strobe_provider_id = 0;

	ret = led_flash_manager_create_sysfs_attrs(flash);
	if (ret < 0)
		goto err_parse_mux_node;

	/* Add flash to the list of flashes maintained by the flash manager. */
	list_add_tail(&flash->list, &flash_list);

	mutex_unlock(&fm_lock);

	return ret;

err_parse_mux_node:
	led_flash_manager_remove_flash_refs(flash);
	of_led_flash_manager_release_dt_data(flash);
unlock:
	mutex_unlock(&fm_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(led_flash_manager_register_flash);

void led_flash_manager_unregister_flash(struct led_classdev_flash *flash)
{
	struct led_classdev_flash *fl, *n;
	bool found_flash = false;

	mutex_lock(&fm_lock);

	/* Remove flash from the list, if found */
	list_for_each_entry_safe(fl, n, &flash_list, list)
		if (fl == flash) {
			found_flash = true;
			break;
		}

	if (!found_flash)
		goto unlock;

	list_del(&fl->list);

	led_flash_manager_remove_sysfs_attrs(flash);

	/* Remove all references to the flash */
	led_flash_manager_remove_flash_refs(flash);
	of_led_flash_manager_release_dt_data(flash);

unlock:
	mutex_unlock(&fm_lock);
}
EXPORT_SYMBOL_GPL(led_flash_manager_unregister_flash);

MODULE_AUTHOR("Jacek Anaszewski <j.anaszewski@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LED Flash Manager");

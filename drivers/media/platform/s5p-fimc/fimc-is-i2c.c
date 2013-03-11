/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#define pr_fmt(fmt) "%s:%d " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/of_i2c.h>
#include <linux/platform_device.h>
#include "fimc-is-i2c.h"

/*
 * An empty algorithm is used as the actual I2C bus controller driver
 * is implemented in the FIMC-IS subsystem firmware and the host CPU
 * doesn't touch the hardware.
 */
static const struct i2c_algorithm fimc_is_i2c_algorithm;

static int fimc_is_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct i2c_adapter *i2c_adap;
	int ret;

	i2c_adap = devm_kzalloc(&pdev->dev, sizeof(*i2c_adap), GFP_KERNEL);

	i2c_adap->dev.of_node = node;
	i2c_adap->dev.parent = &pdev->dev;
	strlcpy(i2c_adap->name, "exynos4x12-is-i2c", sizeof(i2c_adap->name));
	i2c_adap->owner = THIS_MODULE;
	i2c_adap->algo = &fimc_is_i2c_algorithm;
	i2c_adap->class = I2C_CLASS_SPD;

	ret = i2c_add_adapter(i2c_adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add I2C bus %s\n",
						node->full_name);
		return ret;
	}
	of_i2c_register_devices(i2c_adap);

	return 0;
}

static int fimc_is_i2c_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id fimc_is_i2c_of_match[] = {
	{ .compatible = FIMC_IS_I2C_COMPATIBLE },
	{ },
};
MODULE_DEVICE_TABLE(of, fimc_is_i2c_of_match);

static struct platform_driver fimc_is_i2c_driver = {
	.probe		= fimc_is_i2c_probe,
	.remove		= fimc_is_i2c_remove,
	.driver = {
		.of_match_table = fimc_is_i2c_of_match,
		.name		= "fimc-is-i2c",
		.owner		= THIS_MODULE,
	}
};

int fimc_is_register_i2c_driver(void)
{
	return platform_driver_register(&fimc_is_i2c_driver);
}

void fimc_is_unregister_i2c_driver(void)
{
	platform_driver_unregister(&fimc_is_i2c_driver);
}


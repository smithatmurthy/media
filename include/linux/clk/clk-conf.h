/*
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

struct device_node;

#if defined(CONFIG_OF) && defined(CONFIG_COMMON_CLK)
int of_clk_device_init(struct device_node *node);
#else
static inline int of_clk_device_init(struct device_node *node)
{
	return 0;
}
#endif

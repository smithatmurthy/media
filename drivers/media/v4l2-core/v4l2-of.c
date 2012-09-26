/*
 * V4L2 OF binding parsing library
 *
 * Copyright (C) 2012 Renesas Electronics Corp.
 * Author: Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * Copyright (C) 2012 - 2013 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/types.h>

#include <media/v4l2-of.h>

/**
 * v4l2_of_parse_mipi_csi2() - parse MIPI CSI-2 bus properties
 * @node: pointer to endpoint device_node
 * @endpoint: pointer to v4l2_of_endpoint data structure
 *
 * Return: 0 on success or negative error value otherwise.
 */
int v4l2_of_parse_mipi_csi2(const struct device_node *node,
			    struct v4l2_of_endpoint *endpoint)
{
	struct v4l2_mbus_mipi_csi2 *mipi_csi2 = &endpoint->mbus.mipi_csi2;
	u32 data_lanes[ARRAY_SIZE(mipi_csi2->data_lanes)];
	struct property *prop;
	const __be32 *lane = NULL;
	u32 v;
	int i = 0;

	prop = of_find_property(node, "data-lanes", NULL);
	if (!prop)
		return -EINVAL;
	do {
		lane = of_prop_next_u32(prop, lane, &data_lanes[i]);
	} while (lane && i++ < ARRAY_SIZE(data_lanes));

	mipi_csi2->num_data_lanes = i;
	while (i--)
		mipi_csi2->data_lanes[i] = data_lanes[i];

	if (!of_property_read_u32(node, "clock-lanes", &v))
		mipi_csi2->clock_lane = v;

	if (of_get_property(node, "clock-noncontinuous", &v))
		endpoint->mbus.flags |= V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK;

	return 0;
}
EXPORT_SYMBOL(v4l2_of_parse_mipi_csi2);

/**
 * v4l2_of_parse_parallel_bus() - parse parallel bus properties
 * @node: pointer to endpoint device_node
 * @endpoint: pointer to v4l2_of_endpoint data structure
 */
void v4l2_of_parse_parallel_bus(const struct device_node *node,
				struct v4l2_of_endpoint *endpoint)
{
	unsigned int flags = 0;
	u32 v;

	if (WARN_ON(!endpoint))
		return;

	if (!of_property_read_u32(node, "hsync-active", &v))
		flags |= v ? V4L2_MBUS_HSYNC_ACTIVE_HIGH :
			V4L2_MBUS_HSYNC_ACTIVE_LOW;

	if (!of_property_read_u32(node, "vsync-active", &v))
		flags |= v ? V4L2_MBUS_VSYNC_ACTIVE_HIGH :
			V4L2_MBUS_VSYNC_ACTIVE_LOW;

	if (!of_property_read_u32(node, "pclk-sample", &v))
		flags |= v ? V4L2_MBUS_PCLK_SAMPLE_RISING :
			V4L2_MBUS_PCLK_SAMPLE_FALLING;

	if (!of_property_read_u32(node, "field-even-active", &v))
		flags |= v ? V4L2_MBUS_FIELD_EVEN_HIGH :
			V4L2_MBUS_FIELD_EVEN_LOW;
	if (flags)
		endpoint->mbus.type = V4L2_MBUS_PARALLEL;
	else
		endpoint->mbus.type = V4L2_MBUS_BT656;

	if (!of_property_read_u32(node, "data-active", &v))
		flags |= v ? V4L2_MBUS_DATA_ACTIVE_HIGH :
			V4L2_MBUS_DATA_ACTIVE_LOW;

	if (of_get_property(node, "slave-mode", &v))
		flags |= V4L2_MBUS_SLAVE;

	if (!of_property_read_u32(node, "bus-width", &v))
		endpoint->mbus.parallel.bus_width = v;

	if (!of_property_read_u32(node, "data-shift", &v))
		endpoint->mbus.parallel.data_shift = v;

	endpoint->mbus.flags = flags;
}
EXPORT_SYMBOL(v4l2_of_parse_parallel_bus);

/**
 * v4l2_of_parse_endpoint() - parse all endpoint node properties
 * @node: pointer to endpoint device_node
 * @endpoint: pointer to v4l2_of_endpoint data structure
 *
 * All properties are optional. If none are found, we don't set any flags.
 * This means the port has a static configuration and no properties have
 * to be specified explicitly.
 * If any properties that identify the bus as parallel are found and
 * slave-mode isn't set, we set V4L2_MBUS_MASTER. Similarly, if we recognise
 * the bus as serial CSI-2 and clock-noncontinuous isn't set, we set the
 * V4L2_MBUS_CSI2_CONTINUOUS_CLOCK flag.
 * The caller should hold a reference to @node.
 */
void v4l2_of_parse_endpoint(const struct device_node *node,
			    struct v4l2_of_endpoint *endpoint)
{
	const struct device_node *port_node = of_get_parent(node);
	struct v4l2_of_mbus *mbus = &endpoint->mbus;
	bool data_lanes_present = false;

	memset(endpoint, 0, sizeof(*endpoint));

	endpoint->local_node = node;
	/*
	 * It doesn't matter whether the two calls below succeed. If they
	 * don't then the default value 0 is used.
	 */
	of_property_read_u32(port_node, "reg", &endpoint->port);
	of_property_read_u32(node, "reg", &endpoint->id);

	v4l2_of_parse_parallel_bus(node, endpoint);

	/* If any parallel bus properties have been found, skip serial ones. */
	if (mbus->parallel.bus_width || mbus->parallel.data_shift ||
	    mbus->flags) {
		/* Default parallel bus-master. */
		if (!(mbus->flags & V4L2_MBUS_SLAVE))
			mbus->flags |= V4L2_MBUS_MASTER;
		return;
	}

	mbus->type = V4L2_MBUS_CSI2;

	if (!v4l2_of_parse_mipi_csi2(node, endpoint))
		data_lanes_present = true;

	if ((mbus->mipi_csi2.clock_lane || data_lanes_present) &&
	    !(mbus->flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK)) {
		/* Default CSI-2: continuous clock. */
		mbus->flags |= V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	}
}
EXPORT_SYMBOL(v4l2_of_parse_endpoint);

/*
 * Return a refcounted next 'endpoint' device_node. Contrary to the common OF
 * practice, we do not drop the reference to previous, users have to do it
 * themselves, when they're done with the node.
 */
struct device_node *v4l2_of_get_next_endpoint(const struct device_node *parent,
					struct device_node *previous)
{
	struct device_node *child, *port;

	if (!parent)
		return NULL;

	if (!previous) {
		/*
		 * If this is the first call, we have to find a port within
		 * this node.
		 */
		for_each_child_of_node(parent, port) {
			if (!of_node_cmp(port->name, "port"))
				break;
		}
		if (port) {
			/* Found a port, get an endpoint. */
			child = of_get_next_child(port, NULL);
			of_node_put(port);
		} else {
			child = NULL;
		}
		if (!child)
			pr_err("%s(): no endpoint nodes specified for %s\n",
			       __func__, parent->full_name);
	} else {
		port = of_get_parent(previous);
		if (!port)
			/* Hm, has someone given us the root node?... */
			return NULL;

		/* Avoid dropping previous refcount to 0. */
		of_node_get(previous);
		child = of_get_next_child(port, previous);
		if (child) {
			of_node_put(port);
			return child;
		}

		/* No more endpoints under this port, try the next one. */
		do {
			port = of_get_next_child(parent, port);
			if (!port)
				return NULL;
		} while (of_node_cmp(port->name, "port"));

		/* Pick up the first endpoint in this port. */
		child = of_get_next_child(port, NULL);
		of_node_put(port);
	}

	return child;
}
EXPORT_SYMBOL(v4l2_of_get_next_endpoint);

/**
 * v4l2_of_get_remote_port_parent() - get remote port's parent node
 * @node: pointer to a local endpoint device_node
 *
 * Return: Remote device node associated with remote endpoint node linked
 *	   to @node. Use of_node_put() on it when done.
 */
struct device_node *v4l2_of_get_remote_port_parent(
			       const struct device_node *node)
{
	struct device_node *np;
	unsigned int lv = 3;

	/* Get remote endpoint node. */
	np = of_parse_phandle(node, "remote-endpoint", 0);

	/* Walk 3 levels up only if there is 'ports' node. */
	while (np && lv && (lv != 1 || !of_node_cmp(np->name, "ports"))) {
		lv--;
		np = of_get_next_parent(np);
	}
	return np;
}
EXPORT_SYMBOL(v4l2_of_get_remote_port_parent);

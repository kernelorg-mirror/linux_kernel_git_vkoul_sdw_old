// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-18 Intel Corporation.

#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"

struct sdw_master_sysfs {
	struct device dev;
	struct sdw_bus *bus;
};

#define to_sdw_device(_dev) \
	container_of(_dev, struct sdw_master_sysfs, dev)

/*
 * The sysfs for properties reflects the MIPI description as given
 * in the MIPI DisCo spec
 *
 * Base file is:
 *	sdw-master-N
 *      |---- clock-stop-modes
 *      |---- max-clock-frequency
 *      |---- clock-frequencies
 *      |---- default-frame-rows
 *      |---- default-frame-cols
 *      |---- dynamic-frame-shape
 *      |---- command-error-threshold
 */

#define sdw_master_attr(field, format_string)			\
static ssize_t field##_show(struct device *dev,			\
			       struct device_attribute *attr,	\
			       char *buf)			\
{								\
	struct sdw_master_sysfs *master = to_sdw_device(dev);	\
	return sprintf(buf, format_string, master->bus->prop.field);	\
}								\
static DEVICE_ATTR_RO(field)

sdw_master_attr(clk_stop_mode, "%x\n");
sdw_master_attr(max_freq, "%d\n");
sdw_master_attr(default_row, "%x\n");
sdw_master_attr(default_col, "%x\n");
sdw_master_attr(dynamic_frame, "%x\n");
sdw_master_attr(err_threshold, "%x\n");

static ssize_t clock_frequencies_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_master_sysfs *master = to_sdw_device(dev);
	ssize_t size = 0;
	int i;

	for (i = 0; i < master->bus->prop.num_freq; i++)
		size += sprintf(buf + size, "%8d ",
				master->bus->prop.freq[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(clock_frequencies);

static ssize_t clock_gears_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_master_sysfs *master = to_sdw_device(dev);
	ssize_t size = 0;
	int i;

	for (i = 0; i < master->bus->prop.num_clk_gears; i++)
		size += sprintf(buf + size, "%8d ",
				master->bus->prop.clk_gears[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(clock_gears);

static struct attribute *master_node_attrs[] = {
	&dev_attr_clk_stop_mode.attr,
	&dev_attr_max_freq.attr,
	&dev_attr_default_row.attr,
	&dev_attr_default_col.attr,
	&dev_attr_dynamic_frame.attr,
	&dev_attr_err_threshold.attr,
	&dev_attr_clock_frequencies.attr,
	&dev_attr_clock_gears.attr,
	NULL,
};

static const struct attribute_group sdw_master_node_group = {
	.attrs = master_node_attrs,
};

static const struct attribute_group *sdw_master_node_groups[] = {
	&sdw_master_node_group,
	NULL
};

static void sdw_device_release(struct device *dev)
{
	struct sdw_master_sysfs *master = to_sdw_device(dev);

	kfree(master);
}

static struct device_type sdw_device_type = {
	.name =	"sdw_device",
	.release = sdw_device_release,
};

int sdw_sysfs_bus_init(struct sdw_bus *bus)
{
	struct sdw_master_sysfs *master;
	int err;

	if (bus->sysfs) {
		dev_err(bus->dev, "SDW sysfs is already initialized\n");
		return -EIO;
	}

	master = bus->sysfs = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return -ENOMEM;

	master->bus = bus;
	master->dev.type = &sdw_device_type;
	master->dev.bus = &sdw_bus_type;
	master->dev.parent = bus->dev;
	master->dev.groups = sdw_master_node_groups;
	dev_set_name(&master->dev, "sdw-master:%x", bus->link_id);

	err = device_register(&master->dev);
	if (err)
		put_device(&master->dev);

	return err;
}

void sdw_sysfs_bus_exit(struct sdw_bus *bus)
{
	struct sdw_master_sysfs *master = bus->sysfs;

	if (!master)
		return;

	master->bus = NULL;
	put_device(&master->dev);
	bus->sysfs = NULL;
}

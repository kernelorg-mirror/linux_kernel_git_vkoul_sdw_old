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

/*
 * Slave sysfs
 */

/*
 * The sysfs for Slave reflects the MIPI description as given
 * in the MIPI DisCo spec
 *
 * Base file is device
 *	|---- mipi_revision
 *	|---- wake_capable
 *	|---- test_mode_capable
 *	|---- simple_clk_stop_capable
 *	|---- clk_stop_timeout
 *	|---- ch_prep_timeout
 *	|---- reset_behave
 *	|---- high_PHY_capable
 *	|---- paging_support
 *	|---- bank_delay_support
 *	|---- p15_behave
 *	|---- master_count
 *	|---- source_ports
 *	|---- sink_ports
 *	|---- dp0
 *		|---- max_word
 *		|---- min_word
 *		|---- words
 *		|---- flow_controlled
 *		|---- simple_ch_prep_sm
 *		|---- device_interrupts
 *	|---- dpN
 *		|---- max_word
 *		|---- min_word
 *		|---- words
 *		|---- type
 *		|---- max_grouping
 *		|---- simple_ch_prep_sm
 *		|---- ch_prep_timeout
 *		|---- device_interrupts
 *		|---- max_ch
 *		|---- min_ch
 *		|---- ch
 *		|---- ch_combinations
 *		|---- modes
 *		|---- max_async_buffer
 *		|---- block_pack_mode
 *		|---- port_encoding
 *		|---- bus_min_freq
 *		|---- bus_max_freq
 *		|---- bus_freq
 *		|---- max_freq
 *		|---- min_freq
 *		|---- freq
 *		|---- prep_ch_behave
 *		|---- glitchless
 *
 */

#define SLAVE_ATTR(type)					\
static ssize_t type##_show(struct device *dev,			\
		struct device_attribute *attr, char *buf)	\
{								\
	struct sdw_slave *slave = dev_to_sdw_dev(dev);		\
	return sprintf(buf, "0x%x\n", slave->prop.type);	\
}								\
static DEVICE_ATTR_RO(type)

SLAVE_ATTR(mipi_revision);
SLAVE_ATTR(wake_capable);
SLAVE_ATTR(test_mode_capable);
SLAVE_ATTR(clk_stop_mode1);
SLAVE_ATTR(simple_clk_stop_capable);
SLAVE_ATTR(clk_stop_timeout);
SLAVE_ATTR(ch_prep_timeout);
SLAVE_ATTR(reset_behave);
SLAVE_ATTR(high_PHY_capable);
SLAVE_ATTR(paging_support);
SLAVE_ATTR(bank_delay_support);
SLAVE_ATTR(p15_behave);
SLAVE_ATTR(master_count);
SLAVE_ATTR(source_ports);
SLAVE_ATTR(sink_ports);

static ssize_t modalias_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);

	return sdw_slave_modalias(slave, buf, 256);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *slave_dev_attrs[] = {
	&dev_attr_mipi_revision.attr,
	&dev_attr_wake_capable.attr,
	&dev_attr_test_mode_capable.attr,
	&dev_attr_clk_stop_mode1.attr,
	&dev_attr_simple_clk_stop_capable.attr,
	&dev_attr_clk_stop_timeout.attr,
	&dev_attr_ch_prep_timeout.attr,
	&dev_attr_reset_behave.attr,
	&dev_attr_high_PHY_capable.attr,
	&dev_attr_paging_support.attr,
	&dev_attr_bank_delay_support.attr,
	&dev_attr_p15_behave.attr,
	&dev_attr_master_count.attr,
	&dev_attr_source_ports.attr,
	&dev_attr_sink_ports.attr,
	&dev_attr_modalias.attr,
	NULL,
};

static struct attribute_group sdw_slave_dev_attr_group = {
	.attrs	= slave_dev_attrs,
};

const struct attribute_group *sdw_slave_dev_attr_groups[] = {
	&sdw_slave_dev_attr_group,
	NULL
};

/*
 * DP-N properties
 */
struct sdw_dpn_sysfs {
	struct device dev;
	struct sdw_dpn_prop *dpn;
};

#define to_sdw_dpn(_dev) \
	container_of(_dev, struct sdw_dpn_sysfs, dev)

#define sdw_dpn_attr(field, format_string)			\
static ssize_t field##_show(struct device *dev,			\
			       struct device_attribute *attr,	\
			       char *buf)			\
{								\
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);	\
	return sprintf(buf, format_string, sysfs->dpn->field);	\
}								\
static DEVICE_ATTR_RO(field)

sdw_dpn_attr(max_word, "%d\n");
sdw_dpn_attr(min_word, "%d\n");
sdw_dpn_attr(max_grouping, "%d\n");
sdw_dpn_attr(device_interrupts, "%d\n");
sdw_dpn_attr(max_ch, "%d\n");
sdw_dpn_attr(min_ch, "%d\n");
sdw_dpn_attr(modes, "%d\n");
sdw_dpn_attr(max_async_buffer, "%d\n");
sdw_dpn_attr(block_pack_mode, "%d\n");
sdw_dpn_attr(port_encoding, "%d\n");

static ssize_t slave_ch_prep_timeout_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);	\

	return sprintf(buf, "%d ", sysfs->dpn->ch_prep_timeout);
}
static DEVICE_ATTR_RO(slave_ch_prep_timeout);

static ssize_t words_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);	\
	ssize_t size = 0;
	int i;

	for (i = 0; i < sysfs->dpn->num_words; i++)
		size += sprintf(buf + size, "%d ", sysfs->dpn->words[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(words);

static ssize_t channels_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);	\
	ssize_t size = 0;
	int i;

	for (i = 0; i < sysfs->dpn->num_ch; i++)
		size += sprintf(buf + size, "%d ", sysfs->dpn->ch[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(channels);

static ssize_t ch_combinations_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);	\
	ssize_t size = 0;
	int i;

	for (i = 0; i < sysfs->dpn->num_ch_combinations; i++)
		size += sprintf(buf + size, "%d ",
				sysfs->dpn->ch_combinations[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(ch_combinations);

static struct attribute *dpn_attrs[] = {
	&dev_attr_max_word.attr,
	&dev_attr_min_word.attr,
	&dev_attr_max_grouping.attr,
	&dev_attr_device_interrupts.attr,
	&dev_attr_max_ch.attr,
	&dev_attr_min_ch.attr,
	&dev_attr_modes.attr,
	&dev_attr_max_async_buffer.attr,
	&dev_attr_block_pack_mode.attr,
	&dev_attr_port_encoding.attr,
	&dev_attr_slave_ch_prep_timeout.attr,
	&dev_attr_words.attr,
	&dev_attr_channels.attr,
	&dev_attr_ch_combinations.attr,
	NULL,
};

static const struct attribute_group dpn_group = {
	.attrs = dpn_attrs,
};

static const struct attribute_group *dpn_groups[] = {
	&dpn_group,
	NULL
};

static void sdw_dpn_release(struct device *dev)
{
	struct sdw_dpn_sysfs *sysfs = to_sdw_dpn(dev);

	kfree(sysfs);
}

static struct device_type sdw_dpn_type = {
	.name =	"sdw_dpn",
	.release = sdw_dpn_release,
};

/*
 * DP0 sysfs
 */

struct sdw_dp0_sysfs {
	struct device dev;
	struct sdw_dp0_prop *dp0;
};

#define to_sdw_dp0(_dev) \
	container_of(_dev, struct sdw_dp0_sysfs, dev)

struct sdw_dp0_attribute {
	struct attribute attr;
	ssize_t (*show)(struct sdw_slave *slave,
			struct sdw_dp0_prop *dp0, char *buf);
};

static ssize_t word_bits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);	\
	ssize_t size = 0;
	int i;

	for (i = 0; i < sysfs->dp0->num_words; i++)
		size += sprintf(buf + size, "%d ", sysfs->dp0->words[i]);
	size += sprintf(buf + size, "\n");

	return size;
}
static DEVICE_ATTR_RO(word_bits);

static ssize_t min_word_bits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	return sprintf(buf, "%d\n", sysfs->dp0->min_word);
}
static DEVICE_ATTR_RO(min_word_bits);

static ssize_t max_word_bits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	return sprintf(buf, "%d\n", sysfs->dp0->max_word);
}
static DEVICE_ATTR_RO(max_word_bits);

static ssize_t flow_controlled_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	return sprintf(buf, "%d\n", sysfs->dp0->flow_controlled);
}
static DEVICE_ATTR_RO(flow_controlled);

static ssize_t ch_prep_sm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	return sprintf(buf, "%d\n", sysfs->dp0->simple_ch_prep_sm);
}
static DEVICE_ATTR_RO(ch_prep_sm);

static ssize_t impl_def_interrupts_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	return sprintf(buf, "%d\n", sysfs->dp0->device_interrupts);
}
static DEVICE_ATTR_RO(impl_def_interrupts);

static struct attribute *dp0_attrs[] = {
	&dev_attr_word_bits.attr,
	&dev_attr_min_word_bits.attr,
	&dev_attr_max_word_bits.attr,
	&dev_attr_flow_controlled.attr,
	&dev_attr_ch_prep_sm.attr,
	&dev_attr_impl_def_interrupts.attr,
	NULL,
};

static const struct attribute_group dp0_group = {
	.attrs = dp0_attrs,
};

static const struct attribute_group *dp0_groups[] = {
	&dp0_group,
	NULL
};

static void sdw_dp0_release(struct device *dev)
{
	struct sdw_dp0_sysfs *sysfs = to_sdw_dp0(dev);

	kfree(sysfs);
}

static struct device_type sdw_dp0_type = {
	.name =	"sdw_dp0",
	.release = sdw_dp0_release,
};

struct sdw_slave_sysfs {
	struct sdw_slave *slave;
	struct sdw_dp0_sysfs *dp0;
	unsigned int num_dpns;
	struct sdw_dpn_sysfs **dpns;

};

static struct sdw_dpn_sysfs *sdw_sysfs_slave_dpn_init(
		struct sdw_slave *slave, struct sdw_dpn_prop *prop, bool src)
{
	struct sdw_dpn_sysfs *dpn;
	int err;

	dpn = kzalloc(sizeof(*dpn), GFP_KERNEL);
	if (!dpn)
		return NULL;

	dpn->dev.type = &sdw_dpn_type;
	dpn->dev.parent = &slave->dev;
	dpn->dev.groups = dpn_groups;
	dpn->dpn = prop;

	if (src)
		dev_set_name(&dpn->dev, "src-dp%x", prop->num);
	else
		dev_set_name(&dpn->dev, "sink-dp%x", prop->num);

	err = device_register(&dpn->dev);
	if (err) {
		put_device(&dpn->dev);
		dpn = NULL;
	}

	return dpn;
}

static void sdw_sysfs_slave_dpn_exit(struct sdw_slave_sysfs *sysfs)
{
	int i;

	for (i = 0; i < sysfs->num_dpns; i++) {
		if (sysfs->dpns[i])
			put_device(&sysfs->dpns[i]->dev);
	}
}

int sdw_sysfs_slave_init(struct sdw_slave *slave)
{
	struct sdw_slave_sysfs *sysfs;
	unsigned int src_dpns, sink_dpns, i, j;
	int err;

	if (slave->sysfs) {
		dev_err(&slave->dev, "SDW Slave sysfs is already initialized\n");
		err = -EIO;
		goto err_ret;
	}

	sysfs = slave->sysfs = kzalloc(sizeof(*sysfs), GFP_KERNEL);
	if (!sysfs) {
		err =-ENOMEM;
		goto err_ret;
	}

	sysfs->slave = slave;

	if (slave->prop.dp0_prop) {
		struct sdw_dp0_sysfs *dp0;

		dp0 = kzalloc(sizeof(*dp0), GFP_KERNEL);
		if (!dp0) {
			err = -ENOMEM;
			goto err_free_sysfs;
		}

		dp0->dev.type = &sdw_dp0_type;
		dp0->dev.parent = &slave->dev;
		dp0->dev.groups = dp0_groups;
		dp0->dp0 = slave->prop.dp0_prop;
		dev_set_name(&dp0->dev, "dp0");
		err = device_register(&dp0->dev);
		if (err)
			goto err_put_dp0;

		sysfs->dp0 = dp0;
	}

	src_dpns = hweight32(slave->prop.source_ports);
	sink_dpns = hweight32(slave->prop.sink_ports);
	sysfs->num_dpns = src_dpns + sink_dpns;

	sysfs->dpns = kcalloc(sysfs->num_dpns, sizeof(**sysfs->dpns), GFP_KERNEL);
	if (!sysfs->dpns) {
		err = -ENOMEM;
		goto err_put_dp0;
	}


	for (i = 0; i < src_dpns; i++) {
		sysfs->dpns[i] = sdw_sysfs_slave_dpn_init(
				slave, &slave->prop.src_dpn_prop[i], true);
		if (!sysfs->dpns[i]) {
			err = -ENOMEM;
			goto err_dpn;
		}
	}

	for (j = 0; j < sink_dpns; j++) {
		sysfs->dpns[i + j] = sdw_sysfs_slave_dpn_init(
				slave, &slave->prop.sink_dpn_prop[j], false);
		if (!sysfs->dpns[i + j]) {
			err = -ENOMEM;
			goto err_dpn;
		}
	}
	return 0;

err_dpn:
	sdw_sysfs_slave_dpn_exit(sysfs);
err_put_dp0:
	put_device(&sysfs->dp0->dev);
err_free_sysfs:
	kfree(sysfs);
	sysfs->slave = NULL;
err_ret:
	return err;

}

void sdw_sysfs_slave_exit(struct sdw_slave *slave)
{
	struct sdw_slave_sysfs *sysfs = slave->sysfs;

	if (!sysfs)
		return;

	sdw_sysfs_slave_dpn_exit(sysfs);
	kfree(sysfs->dpns);
	put_device(&sysfs->dp0->dev);
	kfree(sysfs);
	slave->sysfs = NULL;
}

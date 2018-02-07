// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-18 Intel Corporation.

/*
 *  stream.c - SoundWire Bus stream operations.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

#define CREATE_TRACE_POINTS
#include <trace/events/sdw.h>

/**
 * sdw_release_stream: Free the assigned stream runtime
 *
 * @stream: SoundWire stream runtime
 *
 * sdw_release_stream should be called only once per stream
 */
void sdw_release_stream(struct sdw_stream_runtime *stream)
{
	kfree(stream);
}
EXPORT_SYMBOL(sdw_release_stream);

/**
 * sdw_alloc_stream: Allocate and return stream runtime
 *
 * @stream_name: SoundWire stream name
 *
 * Allocates a SoundWire stream runtime instance.
 * sdw_alloc_stream should be called only once per stream
 */
struct sdw_stream_runtime *sdw_alloc_stream(char *stream_name)
{
	struct sdw_stream_runtime *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream)
		return NULL;

	stream->name = stream_name;
	stream->state = SDW_STREAM_ALLOC;

	return stream;
}
EXPORT_SYMBOL(sdw_alloc_stream);

/**
 * sdw_alloc_master_rt: Allocates and initialize Master runtime handle
 *
 * @bus: SDW bus instance
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle.
 */
static struct sdw_master_runtime
*sdw_alloc_master_rt(struct sdw_bus *bus,
			struct sdw_stream_config *stream_config,
			struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;

	m_rt = stream->m_rt;
	if (m_rt)
		goto stream_config;

	m_rt = kzalloc(sizeof(*m_rt), GFP_KERNEL);
	if (!m_rt)
		return NULL;

	/* Initialization of Master runtime handle */
	INIT_LIST_HEAD(&m_rt->port_list);
	INIT_LIST_HEAD(&m_rt->slave_list);
	stream->m_rt = m_rt;

	list_add_tail(&m_rt->bus_node, &bus->m_rt_list);

stream_config:
	m_rt->ch_count = stream_config->ch_count;
	m_rt->bus = bus;
	m_rt->stream = stream;

	return m_rt;
}

/**
 * sdw_alloc_slave_rt: Allocate and initialize Slave runtime handle.
 *
 * @slave: Slave handle
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle
 */
static struct sdw_slave_runtime
*sdw_alloc_slave_rt(struct sdw_slave *slave,
			struct sdw_stream_config *stream_config,
			struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt = NULL;

	s_rt = kzalloc(sizeof(*s_rt), GFP_KERNEL);
	if (!s_rt)
		return NULL;

	INIT_LIST_HEAD(&s_rt->port_list);

	s_rt->ch_count = stream_config->ch_count;
	s_rt->direction = stream_config->direction;
	s_rt->slave = slave;

	return s_rt;
}

static void sdw_master_port_deconfig(struct sdw_bus *bus,
			struct sdw_master_runtime *m_rt)
{
	struct sdw_port_runtime *p_rt, *_p_rt;

	list_for_each_entry_safe(p_rt, _p_rt,
			&m_rt->port_list, port_node) {

		list_del(&p_rt->port_node);
		kfree(p_rt);
	}
}

static void sdw_slave_port_deconfig(struct sdw_bus *bus,
		struct sdw_slave *slave,
		struct sdw_stream_runtime *stream)
{
	struct sdw_port_runtime *p_rt, *_p_rt;
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_slave_runtime *s_rt;

	list_for_each_entry(s_rt, &m_rt->slave_list, m_rt_node) {

		if (s_rt->slave != slave)
			continue;

		list_for_each_entry_safe(p_rt, _p_rt,
				&s_rt->port_list, port_node) {

			list_del(&p_rt->port_node);
			kfree(p_rt);
		}
	}
}

/**
 * sdw_release_slave_stream: Free Slave(s) runtime handle
 *
 * @slave: Slave handle.
 * @stream: Stream runtime handle.
 */
static void sdw_release_slave_stream(struct sdw_slave *slave,
			struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt, *_s_rt;
	struct sdw_master_runtime *m_rt = stream->m_rt;

	/* Retrieve Slave runtime handle */
	list_for_each_entry_safe(s_rt, _s_rt,
			&m_rt->slave_list, m_rt_node) {

		if (s_rt->slave == slave) {
			list_del(&s_rt->m_rt_node);
			kfree(s_rt);
			return;
		}
	}
}

static void sdw_release_master_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_slave_runtime *s_rt, *_s_rt;

	list_for_each_entry_safe(s_rt, _s_rt,
			&m_rt->slave_list, m_rt_node) {
		sdw_release_slave_stream(s_rt->slave, stream);
	}

	list_del(&m_rt->bus_node);
	stream->m_rt = NULL;
	kfree(m_rt);
}

/**
 * sdw_stream_remove_master: Remove master from sdw_stream
 *
 * @bus: SDW Bus instance
 * @stream: Soundwire stream
 *
 * This removes and frees port_rt and master_rt from a stream
 */

int sdw_stream_remove_master(struct sdw_bus *bus,
		struct sdw_stream_runtime *stream)
{
	mutex_lock(&bus->bus_lock);

	sdw_release_master_stream(stream);
	sdw_master_port_deconfig(bus, stream->m_rt);
	stream->state = SDW_STREAM_RELEASE;

	mutex_unlock(&bus->bus_lock);

	return 0;
}
EXPORT_SYMBOL(sdw_stream_remove_master);

/**
 * sdw_stream_remove_slave: Remove slave from sdw_stream
 *
 * @slave: SDW Slave instance
 * @stream: Soundwire stream
 *
 * This removes and frees port_rt and slave_rt from a stream
 */

int sdw_stream_remove_slave(struct sdw_slave *slave,
		struct sdw_stream_runtime *stream)
{
	mutex_lock(&slave->bus->bus_lock);

	sdw_slave_port_deconfig(slave->bus, slave, stream);
	sdw_release_slave_stream(slave, stream);

	mutex_unlock(&slave->bus->bus_lock);
//	trace_sdw_config_stream(&slave->bus, slave,
//				stream_config, stream->name);

	return 0;
}
EXPORT_SYMBOL(sdw_stream_remove_slave);

static int sdw_config_stream(struct device *dev,
		struct sdw_stream_runtime *stream,
		struct sdw_stream_config *stream_config)
{

	/*
	 * Update the stream rate, channel and bps based on data
	 * transmitter. For more than one transmitter (multilink),
	 * match the rate, bps and increment number of channels.
	 */
	if ((stream->params.rate) &&
			(stream->params.rate != stream_config->frame_rate)) {
		dev_err(dev, "rate for multilink not matching, stream:%s",
				stream->name);
		return -EINVAL;
	}

	if ((stream->params.bps) &&
			(stream->params.bps != stream_config->bps)) {
		dev_err(dev, "bps for multilink not matching, stream:%s",
				stream->name);
		return -EINVAL;
	}

	stream->params.rate = stream_config->frame_rate;
	stream->params.bps = stream_config->bps;
	stream->params.ch_count += stream_config->ch_count;
	stream->type = stream_config->type;

	return 0;
}

static struct sdw_port_runtime *sdw_port_alloc(struct device *dev,
				struct sdw_ports_config *ports_config,
				int port_index)
{
	struct sdw_port_runtime *p_rt;

	p_rt = kzalloc(sizeof(*p_rt), GFP_KERNEL);
	if (!p_rt)
		return NULL;

	p_rt->ch_mask = ports_config->port_config[port_index].ch_mask;
	p_rt->num = ports_config->port_config[port_index].num;

	if (!SDW_VALID_PORT_RANGE(p_rt->num)) {
		dev_err(dev,
			"SoundWire: Invalid port number :%d", p_rt->num);
		kfree(p_rt);
		return NULL;
	}

	return p_rt;

}

static int sdw_master_port_config(struct sdw_bus *bus,
			struct sdw_master_runtime *m_rt,
			struct sdw_ports_config *ports_config)
{
	struct sdw_port_runtime *p_rt;
	int i;

	/* Iterate for number of ports to perform initialization */
	for (i = 0; i < ports_config->count; i++) {

		p_rt = sdw_port_alloc(bus->dev, ports_config, i);
		if (!p_rt)
			return -ENOMEM;

		/*
		 * TODO: Check port capabilities for requested
		 * configuration (audio mode support)
		 */

		list_add_tail(&p_rt->port_node, &m_rt->port_list);
	}

	return 0;
}

static int sdw_slave_port_config(struct sdw_slave *slave,
			struct sdw_slave_runtime *s_rt,
			struct sdw_ports_config *ports_config)
{
	struct sdw_port_runtime *p_rt;
	int i;

	/* Iterate for number of ports to perform initialization */
	for (i = 0; i < ports_config->count; i++) {

		p_rt = sdw_port_alloc(&slave->dev, ports_config, i);
		if (!p_rt)
			return -ENOMEM;

		/*
		 * TODO: Check port capabilities for requested
		 * configuration (audio mode support)
		 */

		list_add_tail(&p_rt->port_node, &s_rt->port_list);
	}

	return 0;
}

/**
 * sdw_stream_add_master: Allocate and add master runtime to a stream
 *
 * @bus: SDW Bus instance
 * @stream_config: Stream configuration for audio stream
 * @ports_config: Port configuration for audio stream
 * @stream: Soundwire stream
 */
int sdw_stream_add_master(struct sdw_bus *bus,
		struct sdw_stream_config *stream_config,
		struct sdw_ports_config *ports_config,
		struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;
	int ret;

	mutex_lock(&bus->bus_lock);

	m_rt = sdw_alloc_master_rt(bus, stream_config, stream);
	if (!m_rt) {
		dev_err(bus->dev,
				"Master runtime config failed for stream:%s",
				stream->name);
		ret = -EINVAL;
		goto error;
	}

	ret = sdw_master_port_config(bus, m_rt, ports_config);
	if (ret)
		goto port_error;

//	for (i = 0; i < ports_config->count; i++)
//		trace_sdw_config_ports(bus, slave,
//				&ports_config->port_config[i], stream->name);


	stream->state = SDW_STREAM_CONFIG;
	goto error;

port_error:
	sdw_release_master_stream(stream);

error:
	mutex_unlock(&bus->bus_lock);
	return ret;

}
EXPORT_SYMBOL(sdw_stream_add_master);

/**
 * sdw_stream_add_slave: Allocate and add master runtime to a stream
 *
 * @slave: SDW Slave instance
 * @stream_config: Stream configuration for audio stream
 * @ports_config: Port configuration for audio stream
 * @stream: Soundwire stream
 */
int sdw_stream_add_slave(struct sdw_slave *slave,
		struct sdw_stream_config *stream_config,
		struct sdw_ports_config *ports_config,
		struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt;
	struct sdw_master_runtime *m_rt;
	int ret;

	mutex_lock(&slave->bus->bus_lock);

	/*
	 * If this API is invoked by slave first then m_rt is not valid.
	 * So, allocate that and add the slave to it.
	 */
	m_rt = sdw_alloc_master_rt(slave->bus, stream_config, stream);
	if (!m_rt) {
		dev_err(&slave->dev,
				"alloc master runtime failed for stream:%s",
				stream->name);
		ret = -EINVAL;
		goto error;
	}

	s_rt = sdw_alloc_slave_rt(slave, stream_config, stream);
	if (!s_rt) {
		dev_err(&slave->dev,
				"Slave runtime config failed for stream:%s",
				stream->name);
		ret = -EINVAL;
		goto port_error;
	}

	ret = sdw_config_stream(&slave->dev, stream, stream_config);
	if (ret)
		goto port_error;

	list_add_tail(&s_rt->m_rt_node, &m_rt->slave_list);

	ret = sdw_slave_port_config(slave, s_rt, ports_config);
	if (ret)
		goto port_error;

	stream->state = SDW_STREAM_CONFIG;
	goto error;

port_error:
	sdw_release_master_stream(stream);
error:
	mutex_unlock(&slave->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_stream_add_slave);

/**
 * sdw_get_slave_dpn_prop: Get Slave port capabilities
 *
 * @slave: Slave handle
 * @direction: Data direction.
 * @port_num: Port number
 */
struct sdw_dpn_prop *sdw_get_slave_dpn_prop(struct sdw_slave *slave,
				enum sdw_data_direction direction,
				unsigned int port_num)
{
	struct sdw_dpn_prop *dpn_prop;
	u8 num_ports;
	int i;

	if (direction == SDW_DATA_DIR_OUT) {
		num_ports = hweight32(slave->prop.source_ports);
		dpn_prop = slave->prop.src_dpn_prop;
	} else {
		num_ports = hweight32(slave->prop.sink_ports);
		dpn_prop = slave->prop.sink_dpn_prop;
	}

	for (i = 0; i < num_ports; i++) {
		dpn_prop = &dpn_prop[i];

		if (dpn_prop->num == port_num)
			return &dpn_prop[i];
	}

	return NULL;
}

/**
 * sdw_prepare_stream: Prepare SoundWire stream
 *
 * @stream: Soundwire stream
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_prepare_stream(struct sdw_stream_runtime *stream)
{
	int ret = 0;

	if (!stream) {
		pr_err("SoundWire: Handle not found for stream");
		return -EINVAL;
	}

	mutex_lock(&stream->m_rt->bus->bus_lock);

	if (stream->state == SDW_STREAM_DISABLE)
		goto error;

	if ((stream->state != SDW_STREAM_CONFIG) &&
		(stream->state != SDW_STREAM_DEPREPARE)) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_prepare_stream(stream);
	if (ret < 0) {
		pr_err("Prepare for stream:%s failed: %d", stream->name, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->m_rt->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_prepare_stream);

/**
 * sdw_enable_stream: Enable SoundWire stream
 *
 * @stream: Soundwire stream
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_enable_stream(struct sdw_stream_runtime *stream)
{
	int ret = 0;

	if (!stream) {
		pr_err("SoundWire: Handle not found for stream");
		return -EINVAL;
	}

	mutex_lock(&stream->m_rt->bus->bus_lock);

	if (stream->state == SDW_STREAM_ENABLE)
		goto error;

	if ((stream->state != SDW_STREAM_PREPARE) &&
		(stream->state != SDW_STREAM_DISABLE)) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_enable_stream(stream);
	if (ret < 0) {
		pr_err("Enable for stream:%s failed: %d", stream->name, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->m_rt->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_enable_stream);

/**
 * sdw_disable_stream: Disable SoundWire stream
 *
 * @stream: Soundwire stream
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_disable_stream(struct sdw_stream_runtime *stream)
{
	int ret = 0;

	if (!stream) {
		pr_err("SoundWire: Handle not found for stream");
		return -EINVAL;
	}

	mutex_lock(&stream->m_rt->bus->bus_lock);

	if (stream->state == SDW_STREAM_DISABLE)
		goto error;

	if (stream->state != SDW_STREAM_ENABLE) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_disable_stream(stream);
	if (ret < 0) {
		pr_err("Disable for stream:%s failed: %d", stream->name, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->m_rt->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_disable_stream);

/**
 * sdw_deprepare_stream: Deprepare SoundWire stream
 *
 * @stream: Soundwire stream
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_deprepare_stream(struct sdw_stream_runtime *stream)
{
	int ret = 0;

	if (!stream) {
		pr_err("SoundWire: Handle not found for stream");
		return -EINVAL;
	}

	mutex_lock(&stream->m_rt->bus->bus_lock);

	if (stream->state != SDW_STREAM_DISABLE) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_deprepare_stream(stream);
	if (ret < 0) {
		pr_err("De-prepare for stream:%d failed: %d", ret, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->m_rt->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_deprepare_stream);

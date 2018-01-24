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
 * sdw_alloc_stream: Allocate and return an unique stream runtime
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
	INIT_LIST_HEAD(&stream->master_list);
	stream->state = SDW_STREAM_ALLOC;

	return stream;
}
EXPORT_SYMBOL(sdw_alloc_stream);

static struct sdw_master_runtime
*sdw_find_master_rt(struct sdw_bus *bus,
			struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;

	/* Retrieve Bus handle if already available */
	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		if (m_rt->bus == bus)
			return m_rt;
	}

	return NULL;
}

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

	m_rt = sdw_find_master_rt(bus, stream);
	if (m_rt)
		goto stream_config;

	m_rt = kzalloc(sizeof(*m_rt), GFP_KERNEL);
	if (!m_rt)
		return NULL;

	/* Initialization of Master runtime handle */
	INIT_LIST_HEAD(&m_rt->slave_list);
	list_add_tail(&m_rt->stream_node, &stream->master_list);

	list_add_tail(&m_rt->bus_node, &bus->m_rt_list);

stream_config:
	m_rt->ch_count = stream_config->ch_count;
	m_rt->direction = stream_config->direction;
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


	s_rt->ch_count = stream_config->ch_count;
	s_rt->direction = stream_config->direction;
	s_rt->slave = slave;

	return s_rt;
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
	struct sdw_master_runtime *m_rt;

	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
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
}

/**
 * sdw_release_master_stream: Free Master runtime handle
 *
 * @m_rt: master runtime
 * @stream: Stream runtime handle.
 */
static void sdw_release_master_stream(struct sdw_master_runtime *m_rt,
			struct sdw_stream_runtime *stream)
{
	struct sdw_slave_runtime *s_rt, *_s_rt;

	list_for_each_entry_safe(s_rt, _s_rt,
			&m_rt->slave_list, m_rt_node) {
		sdw_release_slave_stream(s_rt->slave, stream);
	}

	list_del(&m_rt->stream_node);
	list_del(&m_rt->bus_node);
	kfree(m_rt);
}

/**
 * sdw_stream_remove_master: Remove master from sdw_stream
 *
 * @bus: SDW Bus instance
 * @stream: Soundwire stream
 *
 * This removes and frees master_rt from a stream
 */

int sdw_stream_remove_master(struct sdw_bus *bus,
		struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt, *_m_rt;

	mutex_lock(&bus->bus_lock);

	list_for_each_entry_safe(m_rt, _m_rt,
			&stream->master_list, stream_node) {

		if (m_rt->bus != bus)
			continue;

		sdw_release_master_stream(m_rt, stream);
	}

	if (list_empty(&stream->master_list))
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
 * This removes and frees slave_rt from a stream
 */

int sdw_stream_remove_slave(struct sdw_slave *slave,
		struct sdw_stream_runtime *stream)
{
	mutex_lock(&slave->bus->bus_lock);

	sdw_release_slave_stream(slave, stream);

	mutex_unlock(&slave->bus->bus_lock);

	return 0;
}
EXPORT_SYMBOL(sdw_stream_remove_slave);

static int sdw_config_stream(struct device *dev,
		struct sdw_stream_runtime *stream,
		struct sdw_stream_config *stream_config)
{
	if (stream_config->direction != SDW_DATA_DIR_OUT)
		return 0;

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

/**
 * sdw_stream_add_master: Allocate and add master runtime to a stream
 *
 * @bus: SDW Bus instance
 * @stream_config: Stream configuration for audio stream
 * @stream: Soundwire stream
 */
int sdw_stream_add_master(struct sdw_bus *bus,
		struct sdw_stream_config *stream_config,
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

	ret = sdw_config_stream(bus->dev, stream, stream_config);
	if (ret)
		goto error;

	stream->state = SDW_STREAM_CONFIG;

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
 * @stream: Soundwire stream
 */
int sdw_stream_add_slave(struct sdw_slave *slave,
		struct sdw_stream_config *stream_config,
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
		goto error;
	}

	ret = sdw_config_stream(&slave->dev, stream, stream_config);
	if (ret)
		goto error;

	list_add_tail(&s_rt->m_rt_node, &m_rt->slave_list);

	stream->state = SDW_STREAM_CONFIG;

error:
	mutex_unlock(&slave->bus->bus_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_stream_add_slave);

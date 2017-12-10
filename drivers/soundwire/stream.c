// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

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

struct sdw_core core;

void _sdw_bus_init(void)
{
	/* Initialize Global core lock */
	mutex_init(&core.lock);

	INIT_LIST_HEAD(&core.stream_list);

	/* Valid stream tag number start from 1 */
	set_bit(0, core.tags);
}

/**
 * sdw_release_stream_tag: Free the assigned stream tag
 *
 * @tag: Stream tag
 */
void sdw_release_stream_tag(int tag)
{
	struct sdw_stream_runtime *stream, *_stream;

	mutex_lock(&core.lock);

	list_for_each_entry_safe(stream, _stream, &core.stream_list, node) {
		if (stream->tag == tag) {
			list_del(&stream->node);
			kfree(stream);
			clear_bit(tag, core.tags);
			break;
		}
	}

	mutex_unlock(&core.lock);
}
EXPORT_SYMBOL(sdw_release_stream_tag);

/**
 * sdw_alloc_stream_tag: Allocates an return an unique stream_tag
 *
 * Stream tag is a unique identifier for each SoundWire stream across all
 * SoundWire bus instances. It is a software concept defined by bus for
 * stream management. It is not defined by MIPI SoundWire Spec. This API
 * needs to be called once per SoundWire stream.
 */
int sdw_alloc_stream_tag(void)
{
	struct sdw_stream_runtime *stream;
	int ret = 0;
	int bit;

	mutex_lock(&core.lock);

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (!stream) {
		mutex_unlock(&core.lock);
		return -ENOMEM;
	}

	mutex_init(&stream->stream_lock);

	bit = find_first_zero_bit(core.tags, SDW_NUM_STREAM_TAGS);
	if (bit == SDW_NUM_STREAM_TAGS) {
		pr_err("SDW: Free stream tag not found");
		ret = -EINVAL;
		goto error;
	}

	stream->tag = bit;
	set_bit(bit, core.tags);

	INIT_LIST_HEAD(&stream->master_list);
	stream->state = SDW_STREAM_ALLOC;
	list_add_tail(&stream->node, &core.stream_list);

	mutex_unlock(&core.lock);
	return stream->tag;

error:
	kfree(stream);
	mutex_unlock(&core.lock);

	return ret;
}
EXPORT_SYMBOL(sdw_alloc_stream_tag);

/**
 * sdw_config_master_stream: Allocates and initialize Master runtime handle
 *
 * @bus: SDW bus instance
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle.
 */
static struct sdw_master_runtime
*sdw_config_master_stream(struct sdw_bus *bus,
			struct sdw_stream_config *stream_config,
			struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;

	/* Retrieve Bus handle if already available */
	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		if (m_rt->bus == bus)
			return m_rt;
	}

	m_rt = kzalloc(sizeof(*m_rt), GFP_KERNEL);
	if (!m_rt)
		return NULL;

	/* Initialization of Master runtime handle */
	INIT_LIST_HEAD(&m_rt->port_list);
	INIT_LIST_HEAD(&m_rt->slave_list);
	list_add_tail(&m_rt->stream_node, &stream->master_list);

	mutex_lock(&bus->bus_lock);
	list_add_tail(&m_rt->bus_node, &bus->rt_list);
	mutex_unlock(&bus->bus_lock);

	m_rt->ch_count = stream_config->ch_count;
	m_rt->direction = stream_config->direction;
	m_rt->bus = bus;
	m_rt->stream = stream;

	return m_rt;
}

/**
 * sdw_config_slave_stream: Allocate and initialize Slave runtime handle.
 *
 * @slave: Slave handle
 * @stream_config: Stream configuration
 * @stream: Stream runtime handle
 */
static struct sdw_slave_runtime
*sdw_config_slave_stream(struct sdw_slave *slave,
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

/**
 * sdw_release_slave_stream: Free Slave runtime handle
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
					&m_rt->slave_list, master_node) {

			if (s_rt->slave == slave) {
				list_del(&s_rt->master_node);
				kfree(s_rt);
			}
		}
	}
}

/**
 * sdw_release_master_stream: Free Master runtime handle
 *
 * @bus: Bus handle.
 * @stream: Stream runtime handle.
 */
static void sdw_release_master_stream(struct sdw_bus *bus,
			struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt, *_m_rt;
	struct sdw_slave_runtime *s_rt, *_s_rt;

	list_for_each_entry_safe(m_rt, _m_rt,
			&stream->master_list, stream_node) {

		if (m_rt->bus != bus)
			continue;

		list_for_each_entry_safe(s_rt, _s_rt, &m_rt->slave_list,
							master_node) {
			list_del(&s_rt->master_node);
			kfree(s_rt);
		}

		list_del(&m_rt->stream_node);

		mutex_lock(&bus->bus_lock);
		list_del(&m_rt->bus_node);
		mutex_unlock(&bus->bus_lock);

		kfree(m_rt);
	}
}

static struct sdw_stream_runtime *sdw_find_stream(int tag)
{
	struct sdw_stream_runtime *stream = NULL;

	mutex_lock(&core.lock);

	list_for_each_entry(stream, &core.stream_list, node) {
		if (stream->tag == tag)
			break;
	}

	mutex_unlock(&core.lock);
	return stream;
}

/**
 * sdw_release_stream: Release SDW stream
 *
 * @bus: SDW Bus instance
 * @slave: Slave handle
 * @tag: Stream_tag
 *
 * This API de-configures SoundWire stream. This needs to be called by all
 * the Master(s) and Slave(s) with this stream.
 *
 * Master calls this function Slave handle as NULL.
 */
int sdw_release_stream(struct sdw_bus *bus, struct sdw_slave *slave, int tag)
{
	struct sdw_stream_runtime *stream;

	if ((!bus) && (!slave)) {
		pr_err("SoundWire: Bus/Slave handle not set as expected");
		return -EINVAL;
	}

	stream = sdw_find_stream(tag);
	if (!stream) {
		dev_err(bus->dev, "Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	mutex_lock(&stream->stream_lock);

	/* Call release API of Master/Slave */
	if (!slave)
		sdw_release_master_stream(bus, stream);
	else
		sdw_release_slave_stream(slave, stream);

	if (list_empty(&stream->master_list))
		stream->state = SDW_STREAM_RELEASE;

	mutex_unlock(&stream->stream_lock);

	return 0;
}
EXPORT_SYMBOL(sdw_release_stream);

/**
 * sdw_config_stream: Configures SoundWire stream
 *
 * @bus: SDW Bus instance
 * @slave: Slave handle
 * @stream_config: Stream configuration for audio stream
 * @tag: Stream_tag
 *
 * This API configures SoundWire stream. This needs to be called by all the
 * Master(s) and Slave(s) with this stream.
 *
 * Master calls this function with Slave handle as NULL.
 */
int sdw_config_stream(struct sdw_bus *bus, struct sdw_slave *slave,
		struct sdw_stream_config *stream_config, int tag)
{
	struct sdw_master_runtime *m_rt = NULL;
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_stream_runtime *stream;
	int ret = 0;

	if ((!bus) && (!slave)) {
		pr_err("SoundWire: Bus/Slave handle not set as expected");
		return -EINVAL;
	}

	stream = sdw_find_stream(tag);
	if (!stream) {
		dev_err(bus->dev, "Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	trace_sdw_config_stream(bus, slave, stream_config, tag);

	mutex_lock(&stream->stream_lock);

	/* Initialize Master runtime handle */
	m_rt = sdw_config_master_stream(bus, stream_config, stream);
	if (!m_rt) {
		dev_err(bus->dev, "Master runtime configuration failed");
		ret = -EINVAL;
		goto error;
	}

	if (slave) {
		s_rt = sdw_config_slave_stream(slave, stream_config, stream);
		if (!s_rt) {
			dev_err(bus->dev, "Slave runtime configuration failed");
			ret = -EINVAL;
			goto error;
		}
	}

	if (stream_config->direction == SDW_DATA_DIR_OUT) {
		/*
		 * Update the stream rate, channel and bps based on data
		 * transmitter. For more than one transmitter (multilink),
		 * match the rate, bps and increment number of channels.
		 */
		if ((stream->params.rate) &&
			(stream->params.rate != stream_config->frame_rate)) {
			dev_err(bus->dev, "rate for multilink not matching");
			ret = -EINVAL;
			goto error;
		}

		if ((stream->params.bps) &&
			(stream->params.bps != stream_config->bps)) {
			dev_err(bus->dev, "bps for multilink not matching");
			ret = -EINVAL;
			goto error;
		}

		stream->params.rate = stream_config->frame_rate;
		stream->params.bps = stream_config->bps;
		stream->params.ch_count += stream_config->ch_count;
		stream->type = stream_config->type;
	}

	if (slave)
		list_add_tail(&s_rt->master_node, &m_rt->slave_list);

	stream->state = SDW_STREAM_CONFIG;

error:
	mutex_unlock(&stream->stream_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_config_stream);

/**
 * sdw_get_slv_dpn_prop: Get Slave port capabilities
 *
 * @slave: Slave handle
 * @direction: Data direction.
 * @port_num: Port number
 */
struct sdw_dpn_prop *sdw_get_slv_dpn_prop(struct sdw_slave *slave,
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

static void _sdw_port_deconfig(struct sdw_bus *bus, struct sdw_slave *slave,
					struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_port_runtime *p_rt, *__p_rt;

	if (slave)
		goto slave_deconf;

	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		if (m_rt->bus != bus)
			continue;

		list_for_each_entry_safe(p_rt, __p_rt,
				&m_rt->port_list, port_node) {

			list_del(&p_rt->port_node);
			kfree(p_rt);
		}
	}

	return;

slave_deconf:

	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		list_for_each_entry(s_rt, &m_rt->slave_list, master_node) {

			if (s_rt->slave != slave)
				continue;

			list_for_each_entry_safe(p_rt, __p_rt,
					&s_rt->port_list, port_node) {

				list_del(&p_rt->port_node);
				kfree(p_rt);
			}
		}
	}
}

static int _sdw_port_config(struct sdw_bus *bus, struct sdw_slave *slave,
					struct sdw_stream_runtime *stream,
					struct sdw_ports_config *ports_config)
{
	struct sdw_master_runtime *m_rt = NULL;
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_port_runtime *p_rt;
	bool found = false;
	int i;

	if (!slave) {
		list_for_each_entry(m_rt, &stream->master_list, stream_node) {
			if (m_rt->bus == bus) {
				found = true;
				break;
			}
		}
	} else {
		list_for_each_entry(m_rt, &stream->master_list, stream_node) {
			list_for_each_entry(s_rt,
					&m_rt->slave_list, master_node) {
				if (s_rt->slave == slave) {
					found = true;
					break;
				}
			}
		}
	}

	if (!found) {
		pr_err("SoundWire: Bus/Slave handle not found for this port");
		return -EINVAL;
	}

	/* Iterate for number of ports to perform initialization */
	for (i = 0; i < ports_config->num; i++) {

		p_rt = kzalloc(sizeof(*p_rt), GFP_KERNEL);
		if (!p_rt)
			return -ENOMEM;

		p_rt->ch_mask = ports_config->port_config[i].ch_mask;
		p_rt->num = ports_config->port_config[i].num;

		if (!SDW_VALID_PORT_RANGE(p_rt->num)) {
			pr_err("SoundWire: Invalid port number :%d", p_rt->num);
			return -EINVAL;
		}

		/*
		 * TODO: Check port capabilities for requested
		 * configuration (audio mode support)
		 */

		if (!slave)
			list_add_tail(&p_rt->port_node, &m_rt->port_list);
		else
			list_add_tail(&p_rt->port_node, &s_rt->port_list);
	}

	return 0;
}

/**
 * sdw_config_ports: Configure Master or Slave Port(s)
 *
 * @bus: SDW Bus instance
 * @slave: Slave handle
 * @ports_config: Port(s) configuration
 * @tag: Stream tag
 *
 * This API configures SoundWire ports. This needs to be called by all the
 * Master(s) and Slave(s) with this stream.
 *
 * Master calls this function with Slave handle as NULL.
 */
int sdw_config_ports(struct sdw_bus *bus, struct sdw_slave *slave,
		struct sdw_ports_config *ports_config, int tag)
{
	struct sdw_stream_runtime *stream;
	int i, ret;

	if ((!bus) && (!slave)) {
		pr_err("SoundWire: Bus/Slave handle not set as expected");
		return -EINVAL;
	}

	stream = sdw_find_stream(tag);
	if (!stream) {
		dev_err(bus->dev, "Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	for (i = 0; i < ports_config->num; i++)
		trace_sdw_config_ports(bus, slave,
				&ports_config->port_config[i], tag);

	mutex_lock(&stream->stream_lock);

	/* Configure Master/Slave port */
	if (!slave)
		ret = _sdw_port_config(bus, NULL, stream, ports_config);
	else
		ret = _sdw_port_config(bus, slave, stream, ports_config);

	mutex_unlock(&stream->stream_lock);
	return ret;
}
EXPORT_SYMBOL(sdw_config_ports);

/**
 * sdw_release_ports: Release Master or Slave Port(s)
 *
 * @bus: SDW Bus instance
 * @slave: Slave handle
 * @tag: Stream tag
 *
 * This API releases SoundWire ports. This needs to be called by all the
 * Master(s) and Slave(s) with this stream.
 *
 * Master calls this function with Slave handle as NULL.
 */
int sdw_release_ports(struct sdw_bus *bus, struct sdw_slave *slave, int tag)
{
	struct sdw_stream_runtime *stream;

	if ((!bus) && (!slave)) {
		pr_err("SoundWire: Bus/Slave handle not set as expected");
		return -EINVAL;
	}

	stream = sdw_find_stream(tag);
	if (!stream) {
		dev_err(bus->dev, "Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	mutex_lock(&stream->stream_lock);

	/* Release Master/Slave port */
	if (!slave)
		_sdw_port_deconfig(bus, NULL, stream);
	else
		_sdw_port_deconfig(bus, slave, stream);

	mutex_unlock(&stream->stream_lock);
	return 0;
}
EXPORT_SYMBOL(sdw_release_ports);

static void sdw_acquire_bus_lock(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;
	struct sdw_bus *bus = NULL;

	/* Iterate for all Master(s) in Master list */
	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		bus = m_rt->bus;
		mutex_lock(&bus->bus_lock);
	}
}

static void sdw_release_bus_lock(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = NULL;
	struct sdw_bus *bus = NULL;

	/* Iterate for all Master(s) in Master list */
	list_for_each_entry(m_rt, &stream->master_list, stream_node) {
		bus = m_rt->bus;
		mutex_unlock(&bus->bus_lock);
	}
}

/**
 * sdw_prepare_stream: Prepare SoundWire stream
 *
 * @tag: stream tag
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_prepare_stream(int tag)
{
	struct sdw_stream_runtime *stream = NULL;
	int ret = 0;

	stream = sdw_find_stream(tag);
	if (!stream) {
		pr_err("SoundWire: Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	sdw_acquire_bus_lock(stream);
	mutex_lock(&stream->stream_lock);

	if ((stream->state != SDW_STREAM_CONFIG) &&
		(stream->state != SDW_STREAM_DEPREPARE)) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_prepare_stream(stream);
	if (ret < 0) {
		pr_err("Prepare for stream:%d failed: %d", tag, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->stream_lock);
	sdw_release_bus_lock(stream);
	return ret;
}
EXPORT_SYMBOL(sdw_prepare_stream);

/**
 * sdw_enable_stream: Enable SoundWire stream
 *
 * @tag: stream tag
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_enable_stream(int tag)
{
	struct sdw_stream_runtime *stream = NULL;
	int ret = 0;

	stream = sdw_find_stream(tag);
	if (!stream) {
		pr_err("SoundWire: Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	sdw_acquire_bus_lock(stream);
	mutex_lock(&stream->stream_lock);

	if (stream->state == SDW_STREAM_ENABLE)
		goto error;

	if ((stream->state != SDW_STREAM_PREPARE) &&
		(stream->state != SDW_STREAM_DISABLE)) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_enable_stream(stream);
	if (ret < 0) {
		pr_err("Enable for stream:%d failed: %d", tag, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->stream_lock);
	sdw_release_bus_lock(stream);
	return ret;
}
EXPORT_SYMBOL(sdw_enable_stream);

/**
 * sdw_disable_stream: Disable SoundWire stream
 *
 * @tag: stream tag
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_disable_stream(int tag)
{
	struct sdw_stream_runtime *stream = NULL;
	int ret = 0;

	stream = sdw_find_stream(tag);
	if (!stream) {
		pr_err("SoundWire: Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	sdw_acquire_bus_lock(stream);
	mutex_lock(&stream->stream_lock);

	if (stream->state == SDW_STREAM_DISABLE)
		goto error;

	if (stream->state != SDW_STREAM_ENABLE) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_disable_stream(stream);
	if (ret < 0) {
		pr_err("Disable for stream:%d failed: %d", tag, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->stream_lock);
	sdw_release_bus_lock(stream);
	return ret;
}
EXPORT_SYMBOL(sdw_disable_stream);

/**
 * sdw_deprepare_stream: Deprepare SoundWire stream
 *
 * @tag: stream tag
 *
 * Documentation/soundwire/stream.txt explains this API in detail
 */
int sdw_deprepare_stream(int tag)
{
	struct sdw_stream_runtime *stream = NULL;
	int ret;

	stream = sdw_find_stream(tag);
	if (!stream) {
		pr_err("SoundWire: Handle not found for stream tag: %d", tag);
		return -EINVAL;
	}

	sdw_acquire_bus_lock(stream);
	mutex_lock(&stream->stream_lock);

	if (stream->state != SDW_STREAM_DISABLE) {
		ret = -EINVAL;
		goto error;
	}

	ret = _sdw_deprepare_stream(stream);
	if (ret < 0) {
		pr_err("De-prepare for stream:%d failed: %d", tag, ret);
		goto error;
	}

error:
	mutex_unlock(&stream->stream_lock);
	sdw_release_bus_lock(stream);
	return ret;
}
EXPORT_SYMBOL(sdw_deprepare_stream);

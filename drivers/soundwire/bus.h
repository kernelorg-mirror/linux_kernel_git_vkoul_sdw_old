// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_BUS_H
#define __SDW_BUS_H

#include <trace/events/sdw.h>

#if IS_ENABLED(CONFIG_ACPI)
int sdw_acpi_find_slaves(struct sdw_bus *bus);
#else
static inline int sdw_acpi_find_slaves(struct sdw_bus *bus)
{
	return -ENOTSUPP;
}
#endif

void sdw_extract_slave_id(struct sdw_bus *bus,
			u64 addr, struct sdw_slave_id *id);

extern const struct attribute_group *sdw_slave_dev_attr_groups[];

#ifdef CONFIG_DEBUG_FS
struct sdw_bus_debugfs *sdw_bus_debugfs_init(struct sdw_bus *bus);
void sdw_bus_debugfs_exit(struct sdw_bus_debugfs *d);
struct dentry *sdw_bus_debugfs_get_root(struct sdw_bus_debugfs *d);
struct sdw_slave_debugfs *sdw_slave_debugfs_init(struct sdw_slave *slave);
void sdw_slave_debugfs_exit(struct sdw_slave_debugfs *d);
void sdw_debugfs_init(void);
void sdw_debugfs_exit(void);
#else
struct sdw_bus_debugfs *sdw_bus_debugfs_init(struct sdw_bus *bus)
{ return NULL; }

void sdw_bus_debugfs_exit(struct sdw_bus_debugfs *d) {}

struct dentry *sdw_bus_debugfs_get_root(struct sdw_bus_debugfs *d)
{ return NULL; }

struct sdw_slave_debugfs *sdw_slave_debugfs_init(struct sdw_slave *slave)
{ return NULL; }

void sdw_slave_debugfs_exit(struct sdw_slave_debugfs *d) {}

void sdw_debugfs_init(void) {}

void sdw_debugfs_exit(void) {}

#endif

enum {
	SDW_MSG_FLAG_READ = 0,
	SDW_MSG_FLAG_WRITE,
};

/**
 * struct sdw_msg - Message structure
 * @addr: Register address accessed in the Slave
 * @len: number of messages
 * @dev_num: Slave device number
 * @addr_page1: SCP address page 1 Slave register
 * @addr_page2: SCP address page 2 Slave register
 * @flags: transfer flags, indicate if xfer is read or write
 * @buf: message data buffer
 * @ssp_sync: Send message at SSP (Stream Synchronization Point)
 * @page: address requires paging
 */
struct sdw_msg {
	u16 addr;
	u16 len;
	u8 dev_num;
	u8 addr_page1;
	u8 addr_page2;
	u8 flags;
	u8 *buf;
	bool ssp_sync;
	bool page;
};

#define SDW_NUM_STREAM_TAGS		128
#define SDW_DOUBLE_RATE_FACTOR		2
#define SDW_FREQ_MOD_FACTOR		3000
#define SDW_STRM_RATE_GROUPING		1
#define SDW_READ_INTR_CLEAR_RETRY	10

extern int rows[SDW_FRAME_ROWS];
extern int cols[SDW_FRAME_COLS];

/**
 * sdw_stream_state: Stream states
 *
 * @SDW_STREAM_ALLOC: New stream allocated.
 * @SDW_STREAM_CONFIG: Stream configured
 * @SDW_STREAM_PREPARE: Stream prepared
 * @SDW_STREAM_ENABLE: Stream enabled
 * @SDW_STREAM_DISABLE: Stream disabled
 * @SDW_STREAM_DEPREPARE: Stream de-prepared
 * @SDW_STREAM_RELEASE: Stream released
 */
enum sdw_stream_state {
	SDW_STREAM_ALLOC = 0,
	SDW_STREAM_CONFIG = 1,
	SDW_STREAM_PREPARE = 2,
	SDW_STREAM_ENABLE = 3,
	SDW_STREAM_DISABLE = 4,
	SDW_STREAM_DEPREPARE = 5,
	SDW_STREAM_RELEASE = 6,
};

/**
 * sdw_stream_params: Stream parameters
 *
 * @rate: Sampling frequency, in Hz
 * @ch_count: Number of channels
 * @bps: bits per channel sample
 */
struct sdw_stream_params {
	unsigned int rate;
	unsigned int ch_count;
	unsigned int bps;
};

/**
 * sdw_port_runtime: Runtime port parameters for Master or Slave
 *
 * @num: Port number. For audio streams, valid port number ranges from
 * [1,14]
 * @ch_mask: Channel mask
 * @transport_params: Transport parameters
 * @port_params: Port parameters
 * @port_node: List node for Master or Slave port_list
 *
 * SoundWire spec has no mention of ports for Master interface but the
 * concept is logically extended.
 */
struct sdw_port_runtime {
	int num;
	int ch_mask;
	struct sdw_transport_params transport_params;
	struct sdw_port_params port_params;
	struct list_head port_node;
};

/**
 * sdw_slave_runtime: Runtime Stream parameters for Slave
 *
 * @slave: Slave handle
 * @direction: Data direction w.r.t Slave Port(s)
 * @ch_count: Channel count of the Slave w.r.t stream
 * @port_list: List of Slave Ports for this Stream. This list is
 * used for computing and programming transport parameters, port
 * parameters, prepare, enable, disable and de-prepare of Slave port
 * @master_node: Node to be added in sdw_master_runtime slave_list which
 * maintains list of Slave runtime associated with Master runtime for
 * this stream
 */
struct sdw_slave_runtime {
	struct sdw_slave *slave;
	enum sdw_data_direction direction;
	unsigned int ch_count;
	struct list_head port_list;
	struct list_head master_node;
};

/**
 * sdw_master_runtime: Runtime stream parameters for Master
 *
 * @bus: Bus handle
 * @stream: Stream runtime handle
 * @direction: Data direction w.r.t Master Port(s)
 * @ch_count: Channel count of the Master w.r.t stream
 * @port_list: List of Master Ports for this Stream. This list is
 * used for computing and programming transport parameters, port
 * parameters of Master port
 * @slave_list: List of the Slave runtime associated with this
 * Master for stream
 * @stream_node: Node to be added in sdw_stream_runtime master_list
 * which maintains list of Master(s) part of stream
 * @bus_node: Node to be added in sdw_bus rt_list which maintains list
 * of Master runtime instance of all stream(s) running on Bus
 */
struct sdw_master_runtime {
	struct sdw_bus *bus;
	struct sdw_stream_runtime *stream;
	enum sdw_data_direction direction;
	unsigned int ch_count;
	struct list_head port_list;
	struct list_head slave_list;
	struct list_head stream_node;
	struct list_head bus_node;
};

/**
 * sdw_stream_runtime: Runtime stream parameters
 *
 * @tag: Unique stream tag number
 * @stream_lock: Lock for stream
 * @params: Stream parameters
 * @state: Current state of the stream
 * @type: Stream type PCM or PDM
 * @master_list: List of Masters part of this stream
 * @node: Node for sdw_core stream list
 */
struct sdw_stream_runtime {
	int tag;
	struct mutex stream_lock;
	struct sdw_stream_params params;
	enum sdw_stream_state state;
	enum sdw_stream_type type;
	struct list_head master_list;
	struct list_head node;
};

/**
 * sdw_core: Global SoundWire structure
 *
 * @lock: Global lock for all bus instances
 * @stream_list: List holding active SoundWire stream(s)
 * @tags: Bitmap for unique stream tags
 * Bit set implies used number, bit clear implies unused number.
 */
struct sdw_core {
	struct mutex lock;
	struct list_head stream_list;
	DECLARE_BITMAP(tags, SDW_NUM_STREAM_TAGS);
};

void _sdw_bus_init(void);
int _sdw_prepare_stream(struct sdw_stream_runtime *stream);
int _sdw_enable_stream(struct sdw_stream_runtime *stream);
int _sdw_deprepare_stream(struct sdw_stream_runtime *stream);
int _sdw_disable_stream(struct sdw_stream_runtime *stream);

struct sdw_dpn_prop *sdw_get_slv_dpn_prop(struct sdw_slave *slave,
				enum sdw_data_direction direction,
				unsigned int port_num);
int sdw_configure_dpn_intr(struct sdw_slave *slave, int port,
					bool enable, int mask);

int sdw_transfer_trace_reg(void);
void sdw_transfer_trace_unreg(void);

int sdw_transfer(struct sdw_bus *bus, struct sdw_msg *msg);
int sdw_transfer_defer(struct sdw_bus *bus, struct sdw_msg *msg,
				struct sdw_defer *defer);

int sdw_fill_msg(struct sdw_msg *msg, struct sdw_slave *slave,
		u32 addr, size_t count, u16 dev_num, u8 flags, u8 *buf);

/* Retrieve and return channel count from channel mask */
static inline int sdw_ch_mask_to_ch(int ch_mask)
{
	int c = 0;

	for (c = 0; ch_mask; ch_mask >>= 1)
		c += ch_mask & 1;

	return c;
}

/* Fill transport parameter data structure */
static inline void sdw_fill_xport_params(struct sdw_transport_params *params,
					int port_num, bool grp_ctrl_valid,
					int grp_ctrl, int sample_int,
					int off1, int off2,
					int hstart, int hstop,
					int pack_mode, int lane_ctrl)
{
	params->port_num = port_num;
	params->blk_grp_ctrl_valid = grp_ctrl_valid;
	params->blk_grp_ctrl = grp_ctrl;
	params->sample_interval = sample_int;
	params->offset1 = off1;
	params->offset2 = off2;
	params->hstart = hstart;
	params->hstop = hstop;
	params->blk_pkg_mode = pack_mode;
	params->lane_ctrl = lane_ctrl;

	/* Tracing for transport parameters */
	trace_sdw_xport_params(params);

}

/* Fill port parameter data structure */
static inline void sdw_fill_port_params(struct sdw_port_params *params,
					int port_num, int bps,
					int flow_mode, int data_mode)
{

	params->num = port_num;
	params->bps = bps;
	params->flow_mode = flow_mode;
	params->data_mode = data_mode;

	/* Tracing for transport parameters */
	trace_sdw_port_params(params);

}

/* Read-Modify-Write Slave register */
static inline int
sdw_update(struct sdw_slave *slave, u32 addr, u8 mask, u8 val)
{
	int tmp;

	tmp = sdw_read(slave, addr);
	if (tmp < 0)
		return tmp;

	tmp = (tmp & ~mask) | val;
	return sdw_write(slave, addr, tmp);
}
#endif /* __SDW_BUS_H */

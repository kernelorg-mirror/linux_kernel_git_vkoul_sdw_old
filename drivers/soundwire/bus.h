// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

#ifndef __SDW_BUS_H
#define __SDW_BUS_H

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

/**
 * sdw_slave_runtime: Runtime Stream parameters for Slave
 *
 * @slave: Slave handle
 * @direction: Data direction w.r.t Slave Port(s)
 * @ch_count: Channel count of the Slave w.r.t stream
 * @m_rt_node: Node to be added in sdw_master_runtime slave_list which
 * maintains list of Slave runtime associated with Master runtime for
 * this stream
 */
struct sdw_slave_runtime {
	struct sdw_slave *slave;
	enum sdw_data_direction direction;
	unsigned int ch_count;
	struct list_head m_rt_node;
};

/**
 * sdw_master_runtime: Runtime stream parameters for Master
 *
 * @bus: Bus handle
 * @stream: Stream runtime handle
 * @direction: Data direction w.r.t Master Port(s)
 * @ch_count: Channel count of the Master w.r.t stream
 * @slave_list: List of the Slave runtime associated with this
 * Master for stream
 * @stream_node: Node to be added in sdw_stream_runtime master
 * runtime list which maintains list of Master(s) part of stream
 * @bus_node: Node to be added in sdw_bus rt_list which maintains list
 * of Master runtime instance of all stream(s) running on Bus
 */
struct sdw_master_runtime {
	struct sdw_bus *bus;
	struct sdw_stream_runtime *stream;
	enum sdw_data_direction direction;
	unsigned int ch_count;
	struct list_head slave_list;
	struct list_head stream_node;
	struct list_head bus_node;
};

int sdw_transfer(struct sdw_bus *bus, struct sdw_msg *msg);
int sdw_transfer_defer(struct sdw_bus *bus, struct sdw_msg *msg,
				struct sdw_defer *defer);

#define SDW_READ_INTR_CLEAR_RETRY	10

int sdw_fill_msg(struct sdw_msg *msg, struct sdw_slave *slave,
		u32 addr, size_t count, u16 dev_num, u8 flags, u8 *buf);

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

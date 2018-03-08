// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-18 Intel Corporation.

/*
 *  runtime.c - SoundWire Stream runtime operations.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/mod_devicetable.h>
#include <linux/soundwire/sdw.h>
#include "bus.h"

/*
 * Array of supported rows and columns as per MIPI SoundWire Specification 1.1
 *
 * The rows are arranged as per the array index value programmed
 * in register. The index 15 has dummy value 0 in order to fill hole.
 */
int rows[SDW_FRAME_ROWS] = {48, 50, 60, 64, 75, 80, 125, 147,
			96, 100, 120, 128, 150, 160, 250, 0,
			192, 200, 240, 256, 72, 144, 90, 180};

int cols[SDW_FRAME_COLS] = {2, 4, 6, 8, 10, 12, 14, 16};

static int sdw_find_col_index(int col)
{
	int i;

	for (i = 0; i < SDW_FRAME_COLS; i++) {
		if (cols[i] == col)
			return i;
	}

	pr_warn("Requested column not found, selecting lowest column no: 2\n");
	return 0;
}

static int sdw_find_row_index(int row)
{
	int i;

	for (i = 0; i < SDW_FRAME_ROWS; i++) {
		if (rows[i] == row)
			return i;
	}

	pr_warn("Requested row not found, selecting lowest row no: 48\n");
	return 0;
}

static int _sdw_program_slave_port_params(struct sdw_bus *bus,
				struct sdw_slave *slave,
				struct sdw_transport_params *t_params,
				enum sdw_dpn_type type)
{
	u32 addr1, addr2, addr3, addr4;
	int ret;
	u8 wbuf;

	if (bus->params.next_bank) {
		addr1 = SDW_DPN_OFFSETCTRL2_B1(t_params->port_num);
		addr2 = SDW_DPN_BLOCKCTRL3_B1(t_params->port_num);
		addr3 = SDW_DPN_SAMPLECTRL2_B1(t_params->port_num);
		addr4 = SDW_DPN_HCTRL_B1(t_params->port_num);
	} else {
		addr1 = SDW_DPN_OFFSETCTRL2_B0(t_params->port_num);
		addr2 = SDW_DPN_BLOCKCTRL3_B0(t_params->port_num);
		addr3 = SDW_DPN_SAMPLECTRL2_B0(t_params->port_num);
		addr4 = SDW_DPN_HCTRL_B0(t_params->port_num);
	}

	/* Program DPN_OffsetCtrl2 registers */
	ret = sdw_write(slave, addr1, t_params->offset2);
	if (ret < 0) {
		dev_err(bus->dev, "DPN_OffsetCtrl2 register write failed");
		return ret;
	}

	/* Program DPN_BlockCtrl3 register */
	ret = sdw_write(slave, addr2, t_params->blk_pkg_mode);
	if (ret < 0) {
		dev_err(bus->dev, "DPN_BlockCtrl3 register write failed");
		return ret;
	}

	if (type != SDW_DPN_FULL)
		return ret;

	/* Program DPN_SampleCtrl2 register */
	wbuf = ((t_params->sample_interval - 1) &
			SDW_DPN_SAMPLECTRL_HIGH) >>
			SDW_REG_SHIFT(SDW_DPN_SAMPLECTRL_HIGH);

	ret = sdw_write(slave, addr3, wbuf);
	if (ret < 0) {
		dev_err(bus->dev, "DPN_SampleCtrl2 register write failed");
		return ret;
	}

	/* Program DPN_HCtrl register */
	wbuf = (t_params->hstop | (t_params->hstart <<
				SDW_REG_SHIFT(SDW_DPN_HCTRL_HSTART)));
	ret = sdw_write(slave, addr4, wbuf);
	if (ret < 0)
		dev_err(bus->dev, "DPN_HCtrl register write failed");

	return ret;
}

static int sdw_program_slave_port_params(struct sdw_bus *bus,
			struct sdw_slave_runtime *s_rt,
			struct sdw_port_runtime *p_rt)
{
	struct sdw_transport_params *t_params = &p_rt->transport_params;
	struct sdw_port_params *p_params = &p_rt->port_params;
	struct sdw_slave_prop *slave_prop = &s_rt->slave->prop;
	u32 addr1, addr2, addr3, addr4, addr5, addr6;
	struct sdw_dpn_prop *dpn_prop;
	int ret;
	u8 wbuf;

	dpn_prop = sdw_get_slave_dpn_prop(s_rt->slave,
					s_rt->direction,
					t_params->port_num);
	if (!dpn_prop)
		return -EINVAL;

	addr1 = SDW_DPN_PORTCTRL(t_params->port_num);
	addr2 = SDW_DPN_BLOCKCTRL1(t_params->port_num);

	if (bus->params.next_bank) {
		addr3 = SDW_DPN_SAMPLECTRL1_B1(t_params->port_num);
		addr4 = SDW_DPN_OFFSETCTRL1_B1(t_params->port_num);
		addr5 = SDW_DPN_BLOCKCTRL2_B1(t_params->port_num);
		addr6 = SDW_DPN_LANECTRL_B1(t_params->port_num);

	} else {
		addr3 = SDW_DPN_SAMPLECTRL1_B0(t_params->port_num);
		addr4 = SDW_DPN_OFFSETCTRL1_B0(t_params->port_num);
		addr5 = SDW_DPN_BLOCKCTRL2_B0(t_params->port_num);
		addr6 = SDW_DPN_LANECTRL_B0(t_params->port_num);
	}

	/* Program DPN_PortCtrl register */
	wbuf = (p_params->flow_mode | (p_params->data_mode <<
			SDW_REG_SHIFT(SDW_DPN_PORTCTRL_DATAMODE)));
	ret = sdw_update(s_rt->slave, addr1, 0xF, wbuf);
	if (ret < 0) {
		dev_err(&s_rt->slave->dev,
			"DPN_PortCtrl register write failed for port %d",
			t_params->port_num);
		return ret;
	}

	/* Program DPN_BlockCtrl1 register */
	ret = sdw_write(s_rt->slave, addr2, (p_params->bps - 1));
	if (ret < 0) {
		dev_err(&s_rt->slave->dev,
			"DPN_BlockCtrl1 register write failed for port %d",
			t_params->port_num);
		return ret;
	}

	/* Program DPN_SampleCtrl1 register */
	wbuf = (t_params->sample_interval - 1) & SDW_DPN_SAMPLECTRL_LOW;
	ret = sdw_write(s_rt->slave, addr3, wbuf);
	if (ret < 0) {
		dev_err(&s_rt->slave->dev,
			"DPN_SampleCtrl1 register write failed for port %d",
			t_params->port_num);
		return ret;
	}

	/* Program DPN_OffsetCtrl1 registers */
	ret = sdw_write(s_rt->slave, addr4, t_params->offset1);
	if (ret < 0) {
		dev_err(&s_rt->slave->dev,
			"DPN_OffsetCtrl1 register write failed for port %d",
			t_params->port_num);
		return ret;
	}

	/* Program DPN_BlockCtrl2 register*/
	if (t_params->blk_grp_ctrl_valid) {
		ret = sdw_write(s_rt->slave, addr5, t_params->blk_grp_ctrl);
		if (ret < 0) {
			dev_err(&s_rt->slave->dev,
				"DPN_BlockCtrl2 reg write failed for port %d",
				t_params->port_num);
			return ret;
		}
	}

	/* program DPN_LaneCtrl register */
	if (slave_prop->lane_control_support) {
		ret = sdw_write(s_rt->slave, addr6, t_params->lane_ctrl);
		if (ret < 0) {
			dev_err(&s_rt->slave->dev,
				"DPN_LaneCtrl register write failed for port %d",
				t_params->port_num);
			return ret;
		}
	}

	if (dpn_prop->type != SDW_DPN_SIMPLE) {
		ret = _sdw_program_slave_port_params(bus, s_rt->slave,
						t_params, dpn_prop->type);
		if (ret < 0)
			dev_err(&s_rt->slave->dev,
				"Transport reg write failed for port: %d",
				t_params->port_num);
	}

	return ret;
}

static int sdw_program_master_port_params(struct sdw_bus *bus,
		struct sdw_port_runtime *p_rt)
{
	int ret;

	/* Set transport parameters */
	ret = bus->port_ops->dpn_set_port_transport_params(bus,
					&p_rt->transport_params,
					bus->params.next_bank);
	if (ret < 0)
		return ret;

	/* Set port parameters */
	return bus->port_ops->dpn_set_port_params(bus,
				&p_rt->port_params,
				bus->params.next_bank);
}

/**
 * sdw_program_port_params: Programs transport parameters of Master(s)
 * and Slave(s)
 *
 * @m_rt: Master stream runtime
 */
static int sdw_program_port_params(struct sdw_master_runtime *m_rt)
{
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_bus *bus = m_rt->bus;
	struct sdw_port_runtime *p_rt;
	int ret = 0;

	/* Program transport & port parameters for Slave(s) */
	list_for_each_entry(s_rt, &m_rt->slave_list, m_rt_node) {
		list_for_each_entry(p_rt, &s_rt->port_list, port_node) {

			ret = sdw_program_slave_port_params(bus, s_rt, p_rt);
			if (ret < 0)
				return ret;

		}
	}

	/* Program transport & port parameters for Master(s) */
	list_for_each_entry(p_rt, &m_rt->port_list, port_node) {
		ret = sdw_program_master_port_params(bus, p_rt);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int sdw_enable_disable_slave_ports(struct sdw_bus *bus,
				struct sdw_slave_runtime *s_rt,
				struct sdw_port_runtime *p_rt, bool en)
{
	struct sdw_transport_params *t_params = &p_rt->transport_params;
	u32 addr;
	int ret;

	if (bus->params.next_bank)
		addr = SDW_DPN_CHANNELEN_B1(p_rt->num);
	else
		addr = SDW_DPN_CHANNELEN_B0(p_rt->num);

	if (en)
		ret = sdw_update(s_rt->slave, addr, 0xFF, p_rt->ch_mask);
	else
		ret = sdw_update(s_rt->slave, addr, 0xFF, 0x0);

	if (ret < 0)
		dev_err(&s_rt->slave->dev,
			"Slave chn_en reg write failed:%d port:%d",
			ret, t_params->port_num);

	return ret;
}

static int sdw_enable_disable_mstr_ports(struct sdw_master_runtime *m_rt,
			struct sdw_port_runtime *p_rt, bool en)
{
	struct sdw_transport_params *t_params = &p_rt->transport_params;
	struct sdw_bus *bus = m_rt->bus;
	struct sdw_enable_ch enable_ch;
	int ret = 0;

	enable_ch.num = p_rt->num;
	enable_ch.ch_mask = p_rt->ch_mask;
	enable_ch.enable = en;

	/* Perform Master port channel(s) enable/disable */
	if (bus->port_ops->dpn_port_enable_ch) {
		ret = bus->port_ops->dpn_port_enable_ch(bus,
				&enable_ch, bus->params.next_bank);
		if (ret < 0) {
			dev_err(bus->dev,
				"Master chn_en write failed:%d port:%d",
				ret, t_params->port_num);
			return ret;
		}
	}

	return 0;
}

/**
 * sdw_enable_disable_ports: Enable/disable port(s) for Master and
 * Slave(s)
 *
 * @m_rt: Master stream runtime
 * @en: mode (enable/disable)
 */
static int sdw_enable_disable_ports(struct sdw_master_runtime *m_rt, bool en)
{
	struct sdw_port_runtime *s_port, *m_port;
	struct sdw_slave_runtime *s_rt = NULL;
	int ret = 0;

	/* Enable/Disable Slave port(s) */
	list_for_each_entry(s_rt, &m_rt->slave_list, m_rt_node) {
		list_for_each_entry(s_port, &s_rt->port_list, port_node) {
			ret = sdw_enable_disable_slave_ports(m_rt->bus, s_rt,
							s_port, en);
			if (ret < 0)
				return ret;
		}
	}

	/* Enable/Disable Master port(s) */
	list_for_each_entry(m_port, &m_rt->port_list, port_node) {
		ret = sdw_enable_disable_mstr_ports(m_rt, m_port, en);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int sdw_prep_deprep_slave_ports(struct sdw_bus *bus,
			struct sdw_slave_runtime *s_rt,
			struct sdw_port_runtime *p_rt, bool prep)
{
	const struct sdw_slave_ops *ops = s_rt->slave->ops;
	struct completion *port_ready = NULL;
	struct sdw_dpn_prop *dpn_prop;
	struct sdw_prepare_ch prep_ch;
	unsigned int time_left;
	bool intr = false;
	int ret = 0;
	u32 addr;

	prep_ch.num = p_rt->num;
	prep_ch.ch_mask = p_rt->ch_mask;

	dpn_prop = sdw_get_slave_dpn_prop(s_rt->slave,
					s_rt->direction,
					prep_ch.num);
	if (!dpn_prop) {
		dev_err(bus->dev,
			"Slave Port:%d properties not found", prep_ch.num);
		return -EINVAL;
	}

	prep_ch.prepare = prep;

	prep_ch.bank = bus->params.next_bank;

	if ((dpn_prop->device_interrupts) || (!dpn_prop->simple_ch_prep_sm))
		intr = true;

	/* Enable interrupt before Port prepare */
	if ((prep) && (intr)) {
		ret = sdw_configure_dpn_intr(s_rt->slave, p_rt->num, prep,
						dpn_prop->device_interrupts);
		if (ret < 0)
			return ret;
	}

	/* Inform slave about the impending port prepare */
	if (ops->port_prep) {
		ret = ops->port_prep(s_rt->slave, &prep_ch,
					SDW_OPS_PORT_PRE_PREP);
		if (ret < 0) {
			dev_err(&s_rt->slave->dev,
				"Slave Port Pre-Prepare failed: %d", ret);
			return ret;
		}
	}

	/* Prepare Slave port implementing CP_SM */
	if (!dpn_prop->simple_ch_prep_sm) {
		addr = SDW_DPN_PREPARECTRL(p_rt->num);

		if (prep)
			ret = sdw_update(s_rt->slave, addr,
					0xFF, p_rt->ch_mask);
		else
			ret = sdw_update(s_rt->slave, addr, 0xFF, 0x0);

		if (ret < 0) {
			dev_err(&s_rt->slave->dev,
				"Slave prep_ctrl reg write failed");
			return ret;
		}

		/* Wait for completion on port ready */
		port_ready = &s_rt->slave->port_ready[prep_ch.num];
		time_left = wait_for_completion_timeout(port_ready,
				msecs_to_jiffies(dpn_prop->ch_prep_timeout));

		if (!time_left) {
			dev_err(&s_rt->slave->dev,
				"Chn prep failed for port:%d", prep_ch.num);
			return -ETIMEDOUT;
		}
	}

	/* Inform slaves about ports being prepared*/
	if (ops->port_prep) {
		ret = ops->port_prep(s_rt->slave, &prep_ch,
					SDW_OPS_PORT_POST_PREP);
		if (ret < 0) {
			dev_err(&s_rt->slave->dev,
				"Slave Port Post-Prepare failed: %d", ret);
			return ret;
		}
	}

	/* Disable interrupt after Port de-prepare */
	if ((!prep) && (intr))
		ret = sdw_configure_dpn_intr(s_rt->slave, p_rt->num, prep,
						dpn_prop->device_interrupts);

	return ret;
}

static int sdw_prep_deprep_mstr_ports(struct sdw_master_runtime *m_rt,
				struct sdw_port_runtime *p_rt, bool prep)
{
	struct sdw_transport_params *t_params = &p_rt->transport_params;
	struct sdw_bus *bus = m_rt->bus;
	const struct sdw_master_port_ops *ops = bus->port_ops;
	struct sdw_prepare_ch prep_ch;
	int ret = 0;

	prep_ch.num = p_rt->num;
	prep_ch.ch_mask = p_rt->ch_mask;
	prep_ch.prepare = prep; /* Prepare/De-prepare */
	prep_ch.bank = bus->params.next_bank;

	/* Pre-prepare/Pre-deprepare port(s) */
	if (ops->dpn_port_prep) {
		ret = ops->dpn_port_prep(bus, &prep_ch);
		if (ret < 0) {
			dev_err(bus->dev, "Port prepare failed for port:%d",
					t_params->port_num);
			return ret;
		}
	}

	return ret;
}

/**
 * sdw_prep_deprep_ports: Prepare/De-prepare port(s) for Master(s) and
 * Slave(s)
 *
 * @m_rt: Master runtime handle
 * @prep: Prepare or De-prepare
 */
static int sdw_prep_deprep_ports(struct sdw_master_runtime *m_rt, bool prep)
{
	struct sdw_slave_runtime *s_rt = NULL;
	struct sdw_port_runtime *p_rt;
	int ret = 0;

	/* Prepare/De-prepare Slave port(s) */
	list_for_each_entry(s_rt, &m_rt->slave_list, m_rt_node) {
		list_for_each_entry(p_rt, &s_rt->port_list, port_node) {
			ret = sdw_prep_deprep_slave_ports(m_rt->bus, s_rt,
							p_rt, prep);
			if (ret < 0)
				return ret;
		}
	}

	/* Prepare/De-prepare Master port(s) */
	list_for_each_entry(p_rt, &m_rt->port_list, port_node) {
		ret = sdw_prep_deprep_mstr_ports(m_rt, p_rt, prep);
		if (ret < 0)
			return ret;
	}

	return ret;
}


/**
 * sdw_notify_config: Notify bus configuration
 *
 * @m_rt: Master runtime handle
 */
static int sdw_notify_config(struct sdw_master_runtime *m_rt)
{
	struct sdw_slave_runtime *s_rt;
	struct sdw_bus *bus = m_rt->bus;
	struct sdw_slave *slave;
	int ret = 0;

	if (bus->ops->set_bus_conf) {
		ret = bus->ops->set_bus_conf(bus, &bus->params);
		if (ret < 0)
			return ret;
	}

	list_for_each_entry(s_rt, &m_rt->slave_list, m_rt_node) {
		slave = s_rt->slave;

		if (slave->ops->bus_config) {
			ret = slave->ops->bus_config(slave, &bus->params);
			if (ret < 0)
				dev_err(bus->dev, "Notify Slave: %d failed",
								slave->dev_num);
			return ret;
		}
	}

	return ret;
}

/**
 * sdw_program_params: Program transport and port parameters for Master(s)
 * and Slave(s)
 *
 * @bus: SDW bus instance
 */
static int sdw_program_params(struct sdw_bus *bus)
{
	struct sdw_master_runtime *m_rt = NULL;
	int ret = 0;

	list_for_each_entry(m_rt, &bus->m_rt_list, bus_node) {
		ret = sdw_program_port_params(m_rt);
		if (ret < 0) {
			dev_err(bus->dev,
				"Program transport params failed: %d", ret);
			return ret;
		}

		ret = sdw_notify_config(m_rt);
		if (ret < 0) {
			dev_err(bus->dev, "Notify bus config failed: %d", ret);
			return ret;
		}

		/* Enable port(s) on alternate bank for all active streams */
		if (m_rt->stream->state != SDW_STREAM_ENABLE)
			continue;

		ret = sdw_enable_disable_ports(m_rt, true);
		if (ret < 0) {
			dev_err(bus->dev, "Enable channel failed: %d", ret);
			return ret;
		}
	}

	return ret;
}

static int _sdw_bank_switch(struct sdw_bus *bus)
{
	int col_index, row_index;
	struct sdw_msg *wr_msg;
	u8 *wbuf = NULL;
	int ret = 0;
	u16 addr;

	wr_msg = kzalloc(sizeof(*wr_msg), GFP_KERNEL);
	if (!wr_msg)
		return -ENOMEM;

	bus->defer_msg.msg = wr_msg;
	wbuf = kzalloc(sizeof(*wbuf), GFP_KERNEL);
	if (!wbuf) {
		ret = -ENOMEM;
		goto error;
	}

	/* Get row and column index to program register */
	col_index = sdw_find_col_index(bus->params.col);
	row_index = sdw_find_row_index(bus->params.row);
	wbuf[0] = col_index | (row_index << 3);

	if (bus->params.next_bank)
		addr = SDW_SCP_FRAMECTRL_B1;
	else
		addr = SDW_SCP_FRAMECTRL_B0;

	sdw_fill_msg(wr_msg, NULL, addr, 1, SDW_BROADCAST_DEV_NUM,
					SDW_MSG_FLAG_WRITE, wbuf);
	wr_msg->ssp_sync = true;

	if (bus->multi_link)
		ret = sdw_transfer_defer(bus, wr_msg, &bus->defer_msg);
	else
		ret = sdw_transfer(bus, wr_msg);

	if (ret < 0) {
		dev_err(bus->dev, "Slave frame_ctrl reg write failed");
		goto error;
	}

	if (!bus->multi_link) {
		kfree(wr_msg);
		kfree(wbuf);
		bus->defer_msg.msg = NULL;
		bus->params.curr_bank = !bus->params.curr_bank;
		bus->params.next_bank = !bus->params.next_bank;
	}

	return 0;

error:
	kfree(wr_msg);
	kfree(wbuf);
	return ret;
}

static int sdw_bank_switch(struct sdw_bus *bus)
{
	const struct sdw_master_ops *ops = bus->ops;
	int ret = 0;

	/* Pre-bank switch */
	if (ops->pre_bank_switch) {
		ret = ops->pre_bank_switch(bus);
		if (ret < 0) {
			dev_err(bus->dev, "Pre bank switch op failed: %d", ret);
			return ret;
		}
	}

	/* Bank-switch */
	ret = _sdw_bank_switch(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Bank switch op failed: %d", ret);
		return ret;
	}

	return ret;
}

static int sdw_post_bank_switch(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	const struct sdw_master_ops *ops;
	struct sdw_bus *bus = m_rt->bus;
	int ret = 0;

	ops = bus->ops;

	/* Post-bank switch */
	if (ops->post_bank_switch) {
		ret = ops->post_bank_switch(bus);
		if (ret < 0) {
			dev_err(bus->dev,
					"Post bank switch op failed: %d", ret);
			return ret;
		}
	}

	return ret;
}

static int do_bank_switch(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_bus *bus = m_rt->bus;
	int ret = 0;

		/* Bank switch */
		ret = sdw_bank_switch(bus);
		if (ret < 0) {
			dev_err(bus->dev, "Bank switch failed: %d", ret);
			goto err;
		}

	ret = sdw_post_bank_switch(stream);
	if (ret < 0)
		dev_err(bus->dev, "Post Bank switch failed: %d", ret);

err:
	return ret;
}

/**
 * _sdw_deprepare_stream: De-prepare stream
 *
 * @stream: Stream runtime handle
 */
int _sdw_deprepare_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_bus *bus = m_rt->bus;
	int ret = 0;

	/* De-prepare port(s) */
	ret = sdw_prep_deprep_ports(m_rt, false);
	if (ret < 0) {
		dev_err(bus->dev, "De-prepare port(s) failed: %d", ret);
		return ret;
	}

	bus->params.bandwidth -= m_rt->stream->params.rate *
		m_rt->ch_count * m_rt->stream->params.bps;

	if (!bus->params.bandwidth) {
		bus->params.row = 0;
		bus->params.col = 0;
		goto exit;

	}

	/* Compute params */
	if (bus->compute_params) {
		ret = bus->compute_params(bus);
		if (ret < 0) {
			dev_err(bus->dev,
					"Compute params failed: %d", ret);
			return ret;
		}
	}

	/* Program params */
	ret = sdw_program_params(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Program params failed: %d", ret);
		return ret;
	}

	return do_bank_switch(stream);

	trace_sdw_bus_params(bus);

exit:
	stream->state = SDW_STREAM_DEPREPARE;

	return ret;
}

/**
 * _sdw_disable_stream: Disable stream
 *
 * @stream: Stream runtime handle
 */
int _sdw_disable_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_bus *bus = m_rt->bus;
	int ret;

	/* Disable port(s) */
	ret = sdw_enable_disable_ports(m_rt, false);
	if (ret < 0) {
		dev_err(bus->dev, "Disable port(s) failed: %d", ret);
		return ret;
	}

	stream->state = SDW_STREAM_DISABLE;

	/* Program params */
	ret = sdw_program_params(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Program params failed: %d", ret);
		return ret;
	}

	trace_sdw_bus_params(bus);

	return do_bank_switch(stream);
}

/**
 * _sdw_enable_stream: Enable stream
 *
 * @stream: Stream runtime handle
 */
int _sdw_enable_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_bus *bus = m_rt->bus;
	int ret;

	/* Program params */
	ret = sdw_program_params(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Program params failed: %d", ret);
		return ret;
	}

	/* Enable port(s) */
	ret = sdw_enable_disable_ports(m_rt, true);
	if (ret < 0) {
		dev_err(bus->dev, "Enable port(s) failed ret: %d", ret);
		return ret;
	}

	trace_sdw_bus_params(bus);

	ret = do_bank_switch(stream);
	if (ret < 0) {
		dev_err(bus->dev, "Bank switch failed: %d", ret);
		return ret;
	}

	stream->state = SDW_STREAM_ENABLE;
	return 0;
}

/**
 * sdw_prepare_stream: Prepare stream
 *
 * @stream: Stream runtime handle
 */
int _sdw_prepare_stream(struct sdw_stream_runtime *stream)
{
	struct sdw_master_runtime *m_rt = stream->m_rt;
	struct sdw_bus *bus = m_rt->bus;
	struct sdw_master_prop *prop = NULL;
	struct sdw_bus_params params;
	int ret;

	prop = &bus->prop;
	memcpy(&params, &bus->params, sizeof(params));

	/* TODO: Support Asynchronous mode */
	if ((prop->max_freq % stream->params.rate) != 0) {
		dev_err(bus->dev, "Async mode not supported");
		return -EINVAL;
	}

	/* Increment cumulative bus bandwidth */
	bus->params.bandwidth += m_rt->stream->params.rate *
		m_rt->ch_count * m_rt->stream->params.bps;

	/* Compute params */
	if (bus->compute_params) {
		ret = bus->compute_params(bus);
		if (ret < 0) {
			dev_err(bus->dev, "Compute params failed: %d", ret);
			return ret;
		}
	}

	/* Program params */
	ret = sdw_program_params(bus);
	if (ret < 0) {
		dev_err(bus->dev, "Program params failed: %d", ret);
		goto restore_params;
	}

	ret = do_bank_switch(stream);
	if (ret < 0) {
		dev_err(bus->dev, "Bank switch failed: %d", ret);
		goto restore_params;
	}

	/* Prepare port(s) on the new clock configuration */
	ret = sdw_prep_deprep_ports(m_rt, true);
	if (ret < 0) {
		dev_err(bus->dev, "Prepare port(s) failed ret = %d",
				ret);
		return ret;
	trace_sdw_bus_params(bus);
	}

	stream->state = SDW_STREAM_PREPARE;

	return ret;

restore_params:
	memcpy(&bus->params, &params, sizeof(params));
	return ret;
}

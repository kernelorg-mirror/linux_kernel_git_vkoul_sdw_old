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

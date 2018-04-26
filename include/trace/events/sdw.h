/*
 *  This file is provided under a dual BSD/GPLv2 license.  When using or
 *  redistributing this file, you may do so under either license.
 *
 *  GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2015-17 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2015-17 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * SoundWire tracepoints
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM sdw

#if !defined(_TRACE_SDW_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_SDW_H

#include <linux/soundwire/sdw.h>
#include <linux/tracepoint.h>

struct sdw_stream_runtime;

/*
 * __sdw_transfer() write request
 */
TRACE_EVENT(sdw_rw,
	       TP_PROTO(const struct sdw_bus *bus, const struct sdw_msg *msg, int ret),
	       TP_ARGS(bus, msg, ret),
	       TP_STRUCT__entry(
		       __field(int,	master_nr)
		       __field(__u16,	device)
		       __field(__u8,	addr_page1)
		       __field(__u8,	addr_page2)
		       __field(__u16,	addr)
		       __field(__u16,	flag)
		       __field(bool,	ssp_sync)
		       __field(__u16,	len)
		       __dynamic_array(__u8, buf, msg->len)
		       __field(__s16,	ret)),
	       TP_fast_assign(
		       __entry->master_nr = bus->link_id;
		       __entry->device = msg->dev_num;
		       __entry->addr = msg->addr;
		       __entry->flag = msg->flags;
		       __entry->len = msg->len;
		       __entry->addr_page1 = msg->addr_page1;
		       __entry->addr_page2 = msg->addr_page2;
		       __entry->ssp_sync = msg->ssp_sync;
		       memcpy(__get_dynamic_array(buf), msg->buf, msg->len);
		       __entry->ret = ret;
			      ),
	       TP_printk("sdw-RW%d slv_id:%d addr=%03x page1=%04x page2=%04x flag=%04x ssp_sync=%d len=%u [%*phD] ret: %d",
			 __entry->master_nr,
			 __entry->device,
			 __entry->addr,
			 __entry->addr_page1,
			 __entry->addr_page2,
			 __entry->flag,
			 __entry->ssp_sync,
			 __entry->len,
			 __entry->len, __get_dynamic_array(buf),
			 __entry->ret
			 )
		);

/*
 * sdw_stream_config() configuration
 */
TRACE_EVENT(sdw_config_stream,
	       TP_PROTO(const struct sdw_bus *bus, const struct sdw_slave *slv, const struct sdw_stream_config *str_cfg, const char *stream),
	       TP_ARGS(bus, slv, str_cfg, stream),
	       TP_STRUCT__entry(
		       __field(unsigned int,	frame_rate)
		       __field(unsigned int,	ch_cnt)
		       __field(unsigned int,	bps)
		       __field(unsigned int,	direction)
		       __field(const char*,	stream)
		       __field(unsigned int,	stream_type)
		       __array(char,		name,	32)
				),
	       TP_fast_assign(
		       __entry->frame_rate = str_cfg->frame_rate;
		       __entry->ch_cnt = str_cfg->ch_count;
		       __entry->bps = str_cfg->bps;
		       __entry->direction = str_cfg->direction;
		       __entry->stream_type = str_cfg->type;
		       __entry->stream = stream;
		       slv ? strncpy(entry->name, dev_name(&slv->dev), 32) : strncpy(entry->name, dev_name(bus->dev), 32);
			      ),
	       TP_printk("dev = %s stream = %s, type = %d rate = %d, chn = %d bps = %d dir = %d",
			__entry->name,
			__entry->stream,
			__entry->stream_type,
			 __entry->frame_rate,
			 __entry->ch_cnt,
			 __entry->bps,
			 __entry->direction
			 )
		);

/*
 * sdw_port_config() configuration
 */
TRACE_EVENT(sdw_config_ports,
	       TP_PROTO(const struct sdw_bus *bus, const struct sdw_slave *slv, const struct sdw_port_config *port_cfg, const char *stream),
	       TP_ARGS(bus, slv, port_cfg, stream),
	       TP_STRUCT__entry(
		       __field(unsigned int,	port_num)
		       __field(unsigned int,	ch_mask)
		       __field(const char*,	stream)
		       __array(char,		name,	32)
				),
	       TP_fast_assign(
		       __entry->port_num = port_cfg->num;
		       __entry->ch_mask = port_cfg->ch_mask;
		       __entry->stream = stream;
		       slv ? strncpy(entry->name, dev_name(&slv->dev), 32) : strncpy(entry->name, dev_name(bus->dev), 32);
			      ),
	       TP_printk("dev = %s stream = %s, port = %d, ch_mask = %d",
			__entry->name,
			__entry->stream,
			 __entry->port_num,
			 __entry->ch_mask
			 )
		);

/*
 * sdw_xport_params() configuration
 */
TRACE_EVENT(sdw_xport_params,
		TP_PROTO(const struct sdw_transport_params *params),
		TP_ARGS(params),
		TP_STRUCT__entry(
			__field(bool,		blk_grp_ctrl_valid)
			__field(unsigned int,    port_num)
			__field(unsigned int,    blk_grp_ctrl)
			__field(unsigned int,    sample_interval)
			__field(unsigned int,    offset1)
			__field(unsigned int,    offset2)
			__field(unsigned int,    hstart)
			__field(unsigned int,    hstop)
			__field(unsigned int,    blk_pkg_mode)
			__field(unsigned int,    lane_ctrl)
			),
		TP_fast_assign(
			__entry->blk_grp_ctrl_valid = params->blk_grp_ctrl_valid;
			__entry->port_num = params->port_num;
			__entry->blk_grp_ctrl = params->blk_grp_ctrl;
			__entry->sample_interval = params->sample_interval;
			__entry->offset1 = params->offset1;
			__entry->offset2 = params->offset2;
			__entry->hstart = params->hstart;
			__entry->hstop = params->hstop;
			__entry->blk_pkg_mode = params->blk_pkg_mode;
			__entry->lane_ctrl = params->lane_ctrl;
			),
		TP_printk("port_number = %d, bgcv = %d, bgc = %d, si = %d, off1 = %d, off2 = %d, hstt = %d, hstp = %d, bpm = %d, lc = %d",
				__entry->port_num,
				__entry->blk_grp_ctrl_valid,
				__entry->blk_grp_ctrl,
				__entry->sample_interval,
				__entry->offset1,
				__entry->offset2,
				__entry->hstart,
				__entry->hstop,
				__entry->blk_pkg_mode,
				__entry->lane_ctrl
			 )
		);

/*
 * sdw_port_params() configuration
 */
TRACE_EVENT(sdw_port_params,
		TP_PROTO(const struct sdw_port_params *params),
		TP_ARGS(params),
		TP_STRUCT__entry(
			__field(unsigned int,    port_num)
			__field(unsigned int,    bps)
			__field(unsigned int,    flow_mode)
			__field(unsigned int,    data_mode)
			),
		TP_fast_assign(
			__entry->port_num = params->num;
			__entry->bps = params->bps;
			__entry->flow_mode = params->flow_mode;
			__entry->data_mode = params->data_mode;
			),
		TP_printk("port_number = %d, bps = %d, flow_mode = %d, data_mode = %d",
				__entry->port_num,
				__entry->bps,
				__entry->flow_mode,
				__entry->data_mode
			 )
		);

/*
 * sdw_bus_params() configuration
 */
TRACE_EVENT(sdw_bus_params,
		TP_PROTO(const struct sdw_bus *bus),
		TP_ARGS(bus),
		TP_STRUCT__entry(
			__field(unsigned int,	 link_id)
			__field(unsigned int,    curr_bank)
			__field(unsigned int,    next_bank)
			__field(unsigned int,    max_dr_freq)
			__field(unsigned int,    curr_dr_freq)
			__field(unsigned int,    bandwidth)
			__field(unsigned int,    row)
			__field(unsigned int,    col)
			),
		TP_fast_assign(
			__entry->link_id = bus->link_id;
			__entry->curr_bank = bus->params.curr_bank;
			__entry->next_bank = bus->params.next_bank;
			__entry->max_dr_freq = bus->params.max_dr_freq;
			__entry->curr_dr_freq = bus->params.curr_dr_freq;
			__entry->bandwidth = bus->params.bandwidth;
			__entry->row = bus->params.row;
			__entry->col = bus->params.col;
			),
		TP_printk("link_id = %d, curr_bank = %d next_bank = %d mdfreq = %d cdfreq = %d, bw = %d, row = %d, col = %d",
				__entry->link_id,
				__entry->curr_bank,
				__entry->next_bank,
				__entry->max_dr_freq,
				__entry->curr_dr_freq,
				__entry->bandwidth,
				__entry->row,
				__entry->col
			)
		);

#endif /* _TRACE_SDW_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

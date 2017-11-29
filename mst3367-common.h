/*
 *  Driver for the MSTAR 3367 HDMI Receiver
 *
 *  Copyright (c) 2017 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef MST3367_COMMON_H
#define MST3367_COMMON_H

#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ctrls.h>

#include "mst3367-drv.h"

#define dprintk(level, fmt, arg...)\
	do { if (debug >= level)\
		printk(KERN_DEBUG KBUILD_MODNAME ": " fmt, ## arg);\
	} while (0)

struct mst3367_video_standards_s {
	struct v4l2_dv_timings timings;
	u32 htotal_min, htotal_max;
	u32 vtotal_min, vtotal_max;
	u32 hperiod_min, hperiod_max;
	u32 vperiod_min, vperiod_max;
	u32 interleaved;
	u32 encoded_fps;
	u32 hdmi_fpsX100;
};

struct mst3367_state {
	struct mst3367_platform_data pdata;

	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;

	/* Is the mst3367 powered on? */
	bool power_on;
	bool haveSource;

	/* controls */
	struct v4l2_ctrl *hotplug_ctrl;
	struct v4l2_ctrl *rx_sense_ctrl;

	/* i2c */
	struct i2c_adapter *i2c;
	u8 i2c_addr;
	u8 current_bank;

	/* Detection */
	const struct mst3367_video_standards_s *detectedStandard;
	int detectedSignal;
	struct {
		u32 htotal, vtotal, hperiod, vperiod, detectdelay, hactive, interleaved;
	} currentTimings;

	/* Shadow regs for monitoring writes. */
	u8 regs[4][256];
	u8 regs_updated[4][256];

	u8 regb1r01_cached;
	u8 regb2r48_cached;
};

#endif /* MST3367_COMMON_H */

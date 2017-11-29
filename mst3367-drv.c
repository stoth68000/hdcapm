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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <linux/v4l2-dv-timings.h>
#include <media/v4l2-device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-ctrls.h>
//#include <media/i2c/mst3367.h>
#include "mst3367-drv.h"
#include "mst3367-common.h"

static int debug;
module_param_named(debug, debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level [def: 0]");

#define dprintk(level, fmt, arg...)\
        do { if (debug >= level)\
                printk(KERN_DEBUG KBUILD_MODNAME ": " fmt, ## arg);\
        } while (0)

MODULE_DESCRIPTION("Driver for MST3367 HDMI receiver");
MODULE_AUTHOR("Steven Toth <stoth@kernellabs.com>");
MODULE_LICENSE("GPL");

#define BANK0  0x00
#define BANK1  0x01
#define BANK2  0x02
#define BANK3  0x03
#define DUMP_SHADOWS 0
#define DUMP_REGISTERS 0

/*
This is how the register map was modified by the windows
driver during the i2c-trace-driver-init-with-1080-signal.csv
trace.

BANK0 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
     -----------------------------------------------
00 : 00                                              
10 :                                           11 01 
20 :                                                 
30 :                                                 
40 :    6F                                           
50 :    89       20                                  
60 :                                                 
70 :          90                                     
80 :                                                 
90 : 15 15 62 10 00 00 00 00 00 00 00 10 00 00 00 00 
A0 : 00 00 00 10 00 20 00 00 01 20 01 15 95 05 04    
B0 : 20 E0 08 00 54 0C    00 00                      
C0 :                                                 
D0 :                                                 
E0 :       00                                        
F0 : 
                                                
BANK1 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
     -----------------------------------------------
00 : 01                                           02 
10 :                   30 00 00 00 50                
20 :             40                07                
30 : 80 00 00                                        
40 :                                                 
50 :                                                 
60 :                                                 
70 :                                                 
80 :                                                 
90 :                                                 
A0 :                                                 
B0 :                                                 
C0 :                                                 
D0 :                                                 
E0 :                                                 
F0 :                                                 

BANK2 0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
     -----------------------------------------------
00 : 02 61 F5 02 01 00 08 04 03 28                   
10 :                      C0    FF FF FC 1A 00 00 00 
20 : 00 00 26       A2    00                   A1    
30 :                                                 
40 :                                                 
50 :                                                 
60 :                                                 
70 :                                                 
80 :                                                 
90 :                                                 
A0 :                                                 
B0 :                                                 
C0 :                                                 
D0 :                                                 
E0 :                                                 
F0 :                                                 

*/

// http://www.3dexpress.de/displayconfigx/timings.html
static const struct mst3367_video_standards_s mst3367_video_standards[] = {
/*                                                                                                   encoded      HDMI */
/*        timing                       nhtotal --   nvtotal--   nhperiod-   nvperiod-   interleaved      fps   fps*100 */

	{ V4L2_DV_BT_CEA_720X480P59_94, 845,  865,  520,  525,  310,  320,  595,  605,            0,      60,     5994, },

// 720p30 - frontend doesn't reliably lock.
	{ V4L2_DV_BT_CEA_1280X720P30,  2300, 2500,  745,  755,  215,  235,  290,  310,            0,      30,     3000, },
	{ V4L2_DV_BT_CEA_1280X720P50,  2965, 2985,  745,  755,  360,  380,  480,  520,            0,      50,     5000, },
	{ V4L2_DV_BT_CEA_1280X720P60,  2470, 2480,  745,  755,  445,  455,  595,  605,            0,      60,     6000, },

	// Tivo
	{ V4L2_DV_BT_CEA_1280X720P60,  1645, 1655,  745,  755,  445,  455,  595,  605,            0,      60,     6000, },

	{ V4L2_DV_BT_CEA_1920X1080P24, 4080, 4105, 1120, 1130,  260,  280,  230,  250,            0,      24,     2400, },
	{ V4L2_DV_BT_CEA_1920X1080P25, 3950, 3970, 1120, 1130,  270,  290,  240,  254,            0,      25,     2500, },
	{ V4L2_DV_BT_CEA_1920X1080P30, 2295, 3305, 1120, 1130,  330,  345,  290,  310,            0,      30,     3000, },
	{ V4L2_DV_BT_CEA_1920X1080P50, 3950, 3970, 1120, 1130,  550,  570,  480,  520,            0,      25,     5000, },
	{ V4L2_DV_BT_CEA_1920X1080P60, 3290, 3310, 1120, 1130,  665,  685,  595,  605,            0,      30,     6000, },
};

static const struct mst3367_video_standards_s *findVideoStandard(u32 htotal, u32 vtotal, u32 hperiod, u32 vperiod, u32 interleaved)
{
	const struct mst3367_video_standards_s *e, *r = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(mst3367_video_standards); i++) {
		e = &mst3367_video_standards[i];

		if ((htotal < e->htotal_min) || (htotal > e->htotal_max))
			continue;
		if ((vtotal < e->vtotal_min) || (vtotal > e->vtotal_max))
			continue;
		if ((hperiod < e->hperiod_min) || (hperiod > e->hperiod_max))
			continue;
		if ((vperiod < e->vperiod_min) || (vperiod > e->vperiod_max))
			continue;
		if (interleaved != e->interleaved)
			continue;

		r = e;
		break;
	}

	return r;
}

static inline struct mst3367_state *get_mst3367_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mst3367_state, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct mst3367_state, hdl)->sd;
}

static void mst3367_notify_source_detect(struct v4l2_subdev *sd, int haveSource)
{
	struct mst3367_source_detect msd;
	msd.present = haveSource;

	v4l2_subdev_notify(sd, MST3367_SOURCE_DETECT, (void *)&msd);
}

/* The MST 3367 has multiple I2C register maps, banks 0-3, if the
 * current bank doesn't match the requested bank, switch banks.
 */
static void mst3367_switch_bank(struct v4l2_subdev *sd, u8 bank)
{
	struct mst3367_state *state = get_mst3367_state(sd);
	u8 buf[] = { 0x00, bank };

        struct i2c_msg msg = { .addr = state->i2c_addr >> 1,
		.flags = 0, .buf = buf, .len = 2 };

	if (state->current_bank != bank) {
		state->current_bank = bank;
		if (i2c_transfer(state->i2c, &msg, 1) != 1)
			printk(KERN_ERR "%s: switch bank error\n", __func__);
	}
}

static u8 mst3367_rd(struct v4l2_subdev *sd, u8 bank, u8 reg)
{
	struct mst3367_state *state = get_mst3367_state(sd);
        u8 b0 = reg;
        u8 b1 = 0;

        struct i2c_msg msg[] = {
                { .addr = state->i2c_addr >> 1, .flags = 0, .buf = &b0, .len = 1 },
                { .addr = state->i2c_addr >> 1, .flags = I2C_M_RD, .buf = &b1, .len = 1 } };

	dprintk(2, "%s(bank=%d, reg=0x%02x)\n", __func__, bank, reg);

	mst3367_switch_bank(sd, bank);

        if (i2c_transfer(state->i2c, msg, 2) != 2)
                printk(KERN_ERR "%s: readreg error\n", __func__);

	dprintk(2, "%s(bank=%d, reg=0x%02x) = 0x%02x\n", __func__, bank, reg, b1);
        return b1;
}

static void mst3367_wr(struct v4l2_subdev *sd, u8 bank, u8 reg, u8 val)
{
	struct mst3367_state *state = get_mst3367_state(sd);
        u8 buf[] = { reg, val };
        
        struct i2c_msg msg = { .addr = state->i2c_addr >> 1, .flags = 0,
		.buf = buf, .len = 2 };
        
	dprintk(2, "%s(bank=%d, reg=0x%02x, value=0x%02x)\n", __func__, bank, reg, val);
	mst3367_switch_bank(sd, bank);

	state->regs[state->current_bank][reg] = val;
	state->regs_updated[state->current_bank][reg] = 1;

	if (i2c_transfer(state->i2c, &msg, 1) != 1)
		printk(KERN_ERR "%s: writereg error\n", __func__);
}

static inline void mst3367_set(struct v4l2_subdev *sd, u8 bank, u8 reg, u8 mask)
{
	u8 val = mst3367_rd(sd, bank, reg);
	val |= mask;
	mst3367_wr(sd, bank, reg, val);
}

static inline void mst3367_clr(struct v4l2_subdev *sd, u8 bank, u8 reg, u8 mask)
{
	u8 val = mst3367_rd(sd, bank, reg);
	val &= ~mask;
	mst3367_wr(sd, bank, reg, val);
}

enum hpt_e {
	RX_TMDS_HPD_OFF   = 0x00,
	RX_TMDS_A_HPD_ON  = 0x01,
	RX_TMDS_A_LINK_ON = 0x02,
	RX_TMDS_B_HPD_ON  = 0x10,
	RX_TMDS_B_LINK_ON = 0x20,
};

static inline void MST3367_TMDS_HOT_PLUG(struct v4l2_subdev *sd, enum hpt_e e)
{
	u8 v = mst3367_rd(sd, BANK0, 0xB7);
	v |= 0x02;

	if (e & RX_TMDS_A_LINK_ON)
		v &= ~0x02;
	if (e & RX_TMDS_B_LINK_ON)
		v &= ~0x02;

	mst3367_wr(sd, BANK0, 0xB7, v);
	msleep(20);
}

static inline void MST3367_HDMI_INIT(struct v4l2_subdev *sd)
{
	/* RxHdmiInit */
	mst3367_clr(sd, BANK2, 0x01, 0xf0);
	mst3367_set(sd, BANK2, 0x01, 0x40 | 0x20);
	mst3367_set(sd, BANK2, 0x04, 0x01);
	mst3367_wr(sd, BANK2, 0x06, 0x08);
	mst3367_set(sd, BANK2, 0x09, 0x20);
	mst3367_clr(sd, BANK0, 0x54, 0x10);
	mst3367_set(sd, BANK0, 0xac, 0x80);

	mst3367_set(sd, BANK0, 0x00, 0x80);
	mst3367_set(sd, BANK0, 0xce, 0x80);
	mst3367_clr(sd, BANK0, 0xcf, 0x07);
	mst3367_set(sd, BANK0, 0xcf, 0x02);
	mst3367_clr(sd, BANK0, 0x00, 0x80);
#if 0
	mst3367_set(sd, BANK0, 0x00, 0x80);
	mst3367_wr(sd, BANK0, 0xb5, 0x0c);
	mst3367_clr(sd, BANK0, 0x00, 0x80);

	mst3367_wr(sd, BANK2, 0x27, 0x00);
#endif
}

static inline void MST3367_HDCP_RESET(struct v4l2_subdev *sd)
{
	mst3367_wr(sd, BANK0, 0xb8, 0x10); /* HDCP RESET */
	mst3367_wr(sd, BANK0, 0xb8, 0x00);
	msleep(20);
}

static inline void MST3367_HDMI_RESET(struct v4l2_subdev *sd)
{
	mst3367_wr(sd, BANK2, 0x07, 0xf4);
	mst3367_wr(sd, BANK2, 0x07, 0x04);
	msleep(20);
}

#if DUMP_SHADOWS
static void dump_shadows(struct v4l2_subdev *sd, int bank)
{
	struct mst3367_state *state = get_mst3367_state(sd);
	int i, j;
	u8 line[80];

	printk("B%d    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n", bank);
	printk("     -----------------------------------------------\n");
	for (i = 0; i < 256; i += 16) {
		sprintf(line, "%02X : ", i);
		for (j = 0; j < 16; j++) {

			if (state->regs_updated[bank][i + j]) {
				sprintf(line + strlen(line), "%02X ", state->regs[bank][i + j]);
			} else {
				sprintf(line + strlen(line), "   ");
			}

		}
		sprintf(line + strlen(line), "\n");
		printk(line);
	}
}
#endif

#if DUMP_REGISTERS
static void dump_registers(struct v4l2_subdev *sd, int bank)
{
	//struct mst3367_state *state = get_mst3367_state(sd);
	int i, j;
	u8 line[80];
	u8 vals[256];

	for (i = 0; i < sizeof(vals); i++)
		vals[i] = mst3367_rd(sd, bank, i);

	printk("B%d    0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n", bank);
	printk("     -----------------------------------------------\n");
	for (i = 0; i < 256; i += 16) {
		sprintf(line, "%02X : ", i);
		for (j = 0; j < 16; j++) {
			sprintf(line + strlen(line), "%02X ", vals[i + j]);
		}
		sprintf(line + strlen(line), "\n");
		printk(line);
	}
}
#endif

static int MST3367_HDMI_MODE_DETECT(struct v4l2_subdev *sd, int *locked)
{
	struct mst3367_state *state = get_mst3367_state(sd);
	int ret = -ENOLINK; /* No link detected */
	u8 r[0xff];
	u16 t;

	*locked = 0;

	/* Do we have a signal detect / lock? */
	if (mst3367_rd(sd, BANK0, 0x55) & 0x3c) {

		/* We have a signal, extract timing data. */
		state->currentTimings.htotal = (mst3367_rd(sd, BANK0, 0x6a) << 8 | mst3367_rd(sd, BANK0, 0x6b)) & 0xfff;
		state->currentTimings.vtotal = (mst3367_rd(sd, BANK0, 0x5b) << 8 | mst3367_rd(sd, BANK0, 0x5c)) & 0x7ff;
		state->currentTimings.hactive = (mst3367_rd(sd, BANK2, 0x29) << 8 | mst3367_rd(sd, BANK2, 0x28)) & 0x1fff;

		r[0x57] = mst3367_rd(sd, BANK0, 0x57) & 0x3f;
		r[0x58] = mst3367_rd(sd, BANK0, 0x58);
		r[0x59] = mst3367_rd(sd, BANK0, 0x59) & 0x3f;
		r[0x5a] = mst3367_rd(sd, BANK0, 0x5a);
		r[0x5f] = mst3367_rd(sd, BANK0, 0x5f) & 0x02;

		t = ((r[0x57] << 8) | r[0x58]);
		if (t > 0)
			state->currentTimings.hperiod = ((1600000) / ((r[0x57] << 8) | (r[0x58] << 0)));

		t = ((r[0x59] << 8) | r[0x5a]);
		if (t > 0)
			state->currentTimings.vperiod = ((1250000) / ((r[0x59] << 8) | (r[0x5a] << 0)));

		state->currentTimings.interleaved = r[0x5f] >> 1;

		state->currentTimings.detectdelay = ((r[0x59] << 8) | r[0x5a]);
		state->currentTimings.detectdelay = ((state->currentTimings.detectdelay + 63) * 2) / 125; /* Unit ms */

		dprintk(2, "%s() htotal = %d, vtotal = %d, hperiod = %d, vperiod = %d, detectdelay = %d, "
			"hactive = %d, interleaved = %d\n",
			__func__,
			state->currentTimings.htotal,
			state->currentTimings.vtotal,
			state->currentTimings.hperiod,
			state->currentTimings.vperiod,
			state->currentTimings.detectdelay,
			state->currentTimings.hactive,
			state->currentTimings.interleaved);

		/* Looking the signal format. If its something we support then return lock, else no lock. */
		state->detectedStandard = findVideoStandard(state->currentTimings.htotal,
			state->currentTimings.vtotal,
			state->currentTimings.hperiod,
			state->currentTimings.vperiod,
			state->currentTimings.interleaved);
		if (state->detectedStandard)
			*locked = 1;
		else {
			/* Detected a signal on the wire, but we have no standard defined for it. */
			dprintk(2, "%s() No detected standard for htotal = %d, vtotal = %d, hperiod = %d, vperiod = %d, detectdelay = %d, "
				"hactive = %d, interleaved = %d\n",
				__func__,
				state->currentTimings.htotal,
				state->currentTimings.vtotal,
				state->currentTimings.hperiod,
				state->currentTimings.vperiod,
				state->currentTimings.detectdelay,
				state->currentTimings.hactive,
				state->currentTimings.interleaved);
		}

		ret = 0;
/*
		printk(KERN_ERR "%s() r01 = 0x%x\n", __func__, r[0x01]);
HDMI_MD 2
HDCP_OP_STS 1
HDCP_MD 0
0x0: DVI, without HDCP.
001: DVI OESS* + HDCP, without advance cipher.
011: DVI EESS** + HDCP, with advance cipher.
1x0: HDMI EESS, without HDCP.
101: HDMI EESS + HDCP, without advance cipher.
111: HDMI + HDCP EESS, with advance cipher.
*OESS: Original Encryption Status Signaling.
**EESS: Enhanced Encryption Status Signaling
*/

	}

	state->regb1r01_cached = mst3367_rd(sd, BANK1, 0x01);
	state->regb2r48_cached = mst3367_rd(sd, BANK2, 0x48);

	if (*locked) {
		state->detectedSignal = 1;
		if (!state->haveSource) {
			state->haveSource = 1;
			mst3367_notify_source_detect(sd, state->haveSource);
		}
	} else {
		state->detectedSignal = 0;
		if (state->haveSource) {
			state->haveSource = 0;
			mst3367_notify_source_detect(sd, state->haveSource);
		}
	}

	return ret;
}

#if 0
static void MST3367_TMDS_GET_TYPE(struct v4l2_subdev *sd)
{
        /* RxTmdsGetType() [1.35] */
	u8 v;

	v = mst3367_rd(sd, BANK1, 0x01);
	if (v & 0x04) {
		/* RX_TMDS_HDMI */
	}
	if (v & 0x01) {
		/* DVI / HDMI WITH HDCP */
	}

	v = mst3367_rd(sd, BANK1, 0x34);
	if (v & 0x80) {
		/* RX_TMDS_HDMI_HDCP / RX_TMDS_DVI_HDCP */
	}
#if 0
        ULONG type = 0x00000000; // RX_TMDS_DVI
        BYTE R0101 = MST3367_GetRegister( pDevice, 0x01, 0x01 );
        BYTE R0134 = MST3367_GetRegister( pDevice, 0x01, 0x34 );
        if( R0101 & 0x04 ) { type |= 0x00000001; } // RX_TMDS_HDMI
        if( R0101 & 0x01 ) { type |= 0x00000002; } // DVI / HDMI WITH HDCP
        if( R0134 & 0x80 ) { type |= 0x00000002; } // RX_TMDS_HDMI_HDCP / RX_TMDS_DVI_HDCP
        //LINUXV4L2_DEBUG( KERN_INFO, "MST3367_TMDS_GET_TYPE( %08X )\n", type );
        return type;
#endif
}
#endif

static inline u32 MST3367_HdmiGetPacketStatus(struct v4l2_subdev *sd)
{
	u32 status = 0;
	u8 r0b, r0c, r0e;

	r0b = mst3367_rd(sd, BANK2, 0x0b) & 0xff;
	r0c = mst3367_rd(sd, BANK2, 0x0c) & 0x3f;
	r0e = mst3367_rd(sd, BANK2, 0x0e) & 0x08;

	status = (r0c << 8) | r0b;
	if (r0e & 0x08)
		status |= 0x8000;

	printk(KERN_ERR "%s() status = 0x%08x\n", __func__, status);

	return status;
}

static inline u32 MST3367_HdmiGetPacketColor(struct v4l2_subdev *sd)
{
	u32 color = 0;

	u8 r48 = mst3367_rd(sd, BANK2, 0x48) & 0x60;

	if (r48 == 0x00)
		color = 0; /* RX_INPUT_RGB */
	else
	if (r48 == 0x20)
		color = 1; /* RX_INPUT_YUV422 */
	else
	if (r48 == 0x40)
		color = 2; /* RX_INPUT_YUV444 */

	return color;
}

static int mst3367_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);

	dprintk(0, "%s(val = 0x%x)\n", __func__, ctrl->val);

	v4l2_dbg(1, debug, sd, "%s: ctrl id: %d, ctrl->val %d\n", __func__, ctrl->id, ctrl->val);

	return -EINVAL;
}

static const struct v4l2_ctrl_ops mst3367_ctrl_ops = {
	.s_ctrl = mst3367_s_ctrl,
};

#ifdef CONFIG_VIDEO_ADV_DEBUG
/* Register bits 15-8 represent bank, bits 7-0 register. */
static int mst3367_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	reg->val = mst3367_rd(sd, (reg->reg >> 8) & 0xff, reg->reg & 0xff);
	reg->size = 1;
	return 0;
}
static int mst3367_s_register(struct v4l2_subdev *sd, const struct v4l2_dbg_register *reg)
{
	mst3367_wr(sd, (reg->reg >> 8) & 0xff, reg->reg & 0xff, reg->val & 0xff);
	return 0;
}
#endif

static int mst3367_log_status(struct v4l2_subdev *sd)
{
	struct mst3367_state *state = get_mst3367_state(sd);

	v4l2_info(sd, "source_connected:  %s\n", state->haveSource ? "yes" : "no");
	v4l2_info(sd, "signal_detected:   %s\n", state->detectedSignal ? "yes" : "no");
	v4l2_info(sd, "power:             %s\n", state->power_on ? "on" : "off");

	if (state->detectedSignal) {
		v4l2_info(sd, "standard:          %dx%dx%d%s\n",
			state->detectedStandard->timings.bt.width,
			state->detectedStandard->timings.bt.height,
			state->detectedStandard->hdmi_fpsX100 / 100,
			state->detectedStandard->timings.bt.interlaced ? "i" : "p");
	} else {
		v4l2_info(sd, "standard:          n/a\n");
	}

	v4l2_info(sd, "htotal:            %d (horizontal front porch, sync, back porch + active pixels)\n",
		state->currentTimings.htotal);
	v4l2_info(sd, "vtotal:            %d (vertical front porch, sync, back porch + active pixels)\n",
		state->currentTimings.vtotal);
	v4l2_info(sd, "hperiod:           %d (%d.%d KHz)\n",
		state->currentTimings.hperiod,
		state->currentTimings.hperiod / 10,
		state->currentTimings.hperiod % 10);

	v4l2_info(sd, "vperiod:           %d (%d.%d Hz)\n",
		state->currentTimings.vperiod,
		state->currentTimings.vperiod / 10,
		state->currentTimings.vperiod % 10);

	v4l2_info(sd, "detectdelay:       %d\n", state->currentTimings.detectdelay);
	v4l2_info(sd, "hactive:           %d\n", state->currentTimings.hactive);
	v4l2_info(sd, "scanline:          %s\n", state->currentTimings.interleaved ? "interleaved" : "progressive");

	v4l2_info(sd, "input colorspace:  %s\n",
		(state->regb2r48_cached & 0x60) == 0x00 ? "RX_INPUT_RGB" :
		(state->regb2r48_cached & 0x60) == 0x20 ? "RX_INPUT_YUV422" :
		(state->regb2r48_cached & 0x60) == 0x40 ? "RX_INPUT_YUV444" : "UNDEFINED");

	v4l2_info(sd, "1.01:              0x%02x\n", state->regb1r01_cached);
	v4l2_info(sd, "1.01.b2:           %s\n", state->regb1r01_cached & 0x04 ? "HDMI" : "DVI");
	v4l2_info(sd, "1.01.b0:           %s\n", state->regb1r01_cached & 0x01 ? "HDCP active" : "HDCP not present");

	return 0;
}

static int mst3367_s_power(struct v4l2_subdev *sd, int on)
{
	struct mst3367_state *state = get_mst3367_state(sd);

	v4l2_dbg(1, debug, sd, "%s: power %s\n", __func__, on ? "on" : "off");

	/* TODO: Turn on/off the TMDS clocks. */

	state->power_on = on;
	if (on) {
		/* Power up */
	} else {
		/* Power down */
	}

	return true;
}

/* Interrupt handler */
static int mst3367_isr(struct v4l2_subdev *sd, u32 status, bool *handled)
{
	dprintk(0, "%s()\n", __func__);
	*handled = true;
	return 0;
}

static const struct v4l2_subdev_core_ops mst3367_core_ops = {
	.log_status = mst3367_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = mst3367_g_register,
	.s_register = mst3367_s_register,
#endif
	.s_power = mst3367_s_power,
	.interrupt_service_routine = mst3367_isr,
};

/* Enable/disable mst3367 bus output */
static int mst3367_s_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, (enable ? "en" : "dis"));

	if (enable) {
		//mst3367_check_monitor_present_status(sd);
	} else {
		mst3367_s_power(sd, 0);
	}
	return 0;
}

static const struct v4l2_dv_timings_cap mst3367_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(0, 1920, 0, 1200, 25000000, 170000000,
		V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT |
			V4L2_DV_BT_STD_GTF | V4L2_DV_BT_STD_CVT,
		V4L2_DV_BT_CAP_PROGRESSIVE | V4L2_DV_BT_CAP_REDUCED_BLANKING |
		V4L2_DV_BT_CAP_CUSTOM)
};

static int mst3367_g_dv_timings(struct v4l2_subdev *sd, struct v4l2_dv_timings *timings)
{
	struct mst3367_state *state = get_mst3367_state(sd);

	v4l2_dbg(1, debug, sd, "%s:\n", __func__);

	if (!state->detectedSignal) {
		return -ENODATA;
	}

	*timings = state->detectedStandard->timings;

	return 0;
}

static int mst3367_enum_dv_timings(struct v4l2_subdev *sd, struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &mst3367_timings_cap, NULL, NULL);
}

static int mst3367_dv_timings_cap(struct v4l2_subdev *sd, struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = mst3367_timings_cap;
	return 0;
}

static int mst3367_video_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	dprintk(1, "%s(input=%d, output=%d, config=0x%x)\n", __func__, input, output, config);

	return 0;
}

static int mst3367_query_dv_timings(struct v4l2_subdev *sd, struct v4l2_dv_timings *timings)
{
	struct mst3367_state *state = get_mst3367_state(sd);
	int locked;
	int ret;
#if DUMP_SHADOWS || DUMP_REGISTERS
	static int count = 0;
#endif

	dprintk(2, "%s()\n", __func__);

	memset(timings, 0, sizeof(struct v4l2_dv_timings));

	/* Perform video standard detection. */
	ret = MST3367_HDMI_MODE_DETECT(sd, &locked);
	if (ret < 0 || !locked) {
		/* No timings could be detected because no signal was found. */
		return ret;
	}

	/* We're detected a signal, return formal timing. */
	*timings = state->detectedStandard->timings;

	if (debug > 1) {
		v4l2_print_dv_timings(sd->name, "timings: ", timings, true);
		MST3367_HdmiGetPacketColor(sd);
	}

#if DUMP_SHADOWS
	if (count++ > 6) {
		count = 0;
		dump_shadows(sd, 0);
		dump_shadows(sd, 1);
		dump_shadows(sd, 2);
	}
#endif

#if DUMP_REGISTERS
	if (count++ > 10) {
		count = 0;
		dump_registers(sd, BANK0);
		dump_registers(sd, BANK1);
		dump_registers(sd, BANK2);
	}
#endif

	return 0; /* Success  - Signal locked */
}

static int mst3367_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct mst3367_state *state = get_mst3367_state(sd);

	if (state->detectedSignal) {
		/* Clear these failed bits, we have a signal. */
		*status &= ~V4L2_IN_ST_NO_POWER;
		*status &= ~V4L2_IN_ST_NO_SIGNAL;
	} else {
		/* Establish failed bits. */
		*status |=  V4L2_IN_ST_NO_POWER;
		*status |=  V4L2_IN_ST_NO_SIGNAL;
	}

	return 0;
}

static int mst3367_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *sp)
{
	struct mst3367_state *state = get_mst3367_state(sd);

	if (state->detectedSignal && state->detectedStandard) {
		sp->parm.capture.timeperframe.numerator = 1;
		sp->parm.capture.timeperframe.denominator = state->detectedStandard->encoded_fps;
	} else {
		sp->parm.capture.timeperframe.numerator = 0;
		sp->parm.capture.timeperframe.denominator = 0;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops mst3367_video_ops = {
	.s_stream = mst3367_s_stream,
	.g_dv_timings = mst3367_g_dv_timings,
	.s_routing = mst3367_video_s_routing,
	.query_dv_timings = mst3367_query_dv_timings,
	.g_input_status = mst3367_g_input_status,
	.g_parm = mst3367_g_parm,
};

static const struct v4l2_subdev_pad_ops mst3367_pad_ops = {
	.enum_dv_timings = mst3367_enum_dv_timings,
	.dv_timings_cap = mst3367_dv_timings_cap,
};

static int mst3367_s_audio_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd, "%s: %sable\n", __func__, (enable ? "en" : "dis"));
	return 0;
}

static int mst3367_s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 N;

	switch (freq) {
	case 32000:  N = 4096;  break;
	case 44100:  N = 6272;  break;
	case 48000:  N = 6144;  break;
	case 88200:  N = 12544; break;
	case 96000:  N = 12288; break;
	case 176400: N = 25088; break;
	case 192000: N = 24576; break;
	default:
	     return -EINVAL;
	}

	/* Set N (used with CTS to regenerate the audio clock) */
	//mst3367_wr(sd, 0x01, (N >> 16) & 0xf);
	//mst3367_wr(sd, 0x02, (N >> 8) & 0xff);
	//mst3367_wr(sd, 0x03, N & 0xff);

	return 0;
}

static int mst3367_s_i2s_clock_freq(struct v4l2_subdev *sd, u32 freq)
{
	u32 i2s_sf;

	switch (freq) {
	case 32000:  i2s_sf = 0x30; break;
	case 44100:  i2s_sf = 0x00; break;
	case 48000:  i2s_sf = 0x20; break;
	case 88200:  i2s_sf = 0x80; break;
	case 96000:  i2s_sf = 0xa0; break;
	case 176400: i2s_sf = 0xc0; break;
	case 192000: i2s_sf = 0xe0; break;
	default:
	     return -EINVAL;
	}

	return 0;
}

static int mst3367_audio_s_routing(struct v4l2_subdev *sd, u32 input, u32 output, u32 config)
{
	dprintk(1, "%s(input=%d, output=%d, config=0x%x)\n", __func__, input, output, config);
	return 0;
}

static const struct v4l2_subdev_audio_ops mst3367_audio_ops = {
	.s_stream = mst3367_s_audio_stream,
	.s_clock_freq = mst3367_s_clock_freq,
	.s_i2s_clock_freq = mst3367_s_i2s_clock_freq,
	.s_routing = mst3367_audio_s_routing,
};

static const struct v4l2_subdev_ops mst3367_ops = {
	.core  = &mst3367_core_ops,
	.video = &mst3367_video_ops,
	.audio = &mst3367_audio_ops,
	.pad = &mst3367_pad_ops,
};

static void mst3367_audio_setup(struct v4l2_subdev *sd)
{
	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	mst3367_s_i2s_clock_freq(sd, 48000);
	mst3367_s_clock_freq(sd, 48000);
	mst3367_audio_s_routing(sd, 0, 0, 0);
}

static void mst3367_init_setup(struct v4l2_subdev *sd)
{
	int i;
	u8 csctbl[] = {
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x10, 0x00, 0x20, 0x00, 0x00, 0x01, 0x20, 0x01,
	};

	v4l2_dbg(1, debug, sd, "%s\n", __func__);

	MST3367_TMDS_HOT_PLUG(sd, RX_TMDS_HPD_OFF);

	/* RxGeneralInit */
	mst3367_wr(sd, BANK0, 0x41, 0x6f);
	mst3367_wr(sd, BANK0, 0xb8, 0x00);

	/* RxTmdsInit */
	mst3367_wr(sd, BANK1, 0x0f, 0x02);
	mst3367_wr(sd, BANK1, 0x16, 0x30);
	mst3367_wr(sd, BANK1, 0x17, 0x00);
	mst3367_wr(sd, BANK1, 0x18, 0x00);
	mst3367_wr(sd, BANK1, 0x19, 0x00);
	mst3367_wr(sd, BANK1, 0x1a, 0x50);
	mst3367_clr(sd, BANK1, 0x2a, 0x07);
	mst3367_set(sd, BANK1, 0x2a, 0x07);
#if 0
	/* This isn't issued in the windows driver traces. */
	mst3367_wr(sd, BANK1, 0x2a, 0x02);
#endif
	mst3367_wr(sd, BANK2, 0x08, 0x03);

	/* RxHdcpInit */
#if 0
	/* disable HDCP */
	mst3367_wr(sd, BANK1, 0x24, 0x41);
#else
	/* receive HDCP */
	mst3367_wr(sd, BANK1, 0x24, 0x40);
#if 0
	/* HDCP reinit */
	mst3367_wr(sd, BANK1, 0x25, 0x08);
	mst3367_wr(sd, BANK1, 0x26, 0x14);
	val = mst3367_rd(sd, BANK1, 0x27);
	mst3367_wr(sd, BANK1, 0x25, 0x00);
	mst3367_wr(sd, BANK1, 0x26, 0x14);
	mst3367_wr(sd, BANK1, 0x27, val);
#endif
#endif

	mst3367_wr(sd, BANK1, 0x30, 0x80);
	mst3367_wr(sd, BANK1, 0x31, 0x00);
	mst3367_wr(sd, BANK1, 0x32, 0x00);

	/* RxVideoInit */
	mst3367_wr(sd, BANK0, 0xb0, 0x14);
	mst3367_set(sd, BANK0, 0xae, 0x04);
	mst3367_wr(sd, BANK0, 0xad, 0x05); /* ENABLE LOW.PASS FILTER */
	mst3367_wr(sd, BANK0, 0xb1, 0xe0); /* From windows i2c trace. */
	mst3367_wr(sd, BANK0, 0xb2, 0x08); /* From windows i2c trace. */
	mst3367_wr(sd, BANK0, 0xb3, 0x00);
	mst3367_wr(sd, BANK0, 0xb4, 0x55);

	/* RxAudioInit */
	mst3367_clr(sd, BANK0, 0xb4, 0x03);
	mst3367_wr(sd, BANK2, 0x01, 0x61); /* 0x51 -> 0x61 */
	mst3367_wr(sd, BANK2, 0x02, 0xf5);
	mst3367_set(sd, BANK2, 0x03, 0x02);
	mst3367_wr(sd, BANK2, 0x04, 0x01);
	mst3367_wr(sd, BANK2, 0x05, 0x00);
	mst3367_wr(sd, BANK2, 0x06, 0x08);
	mst3367_wr(sd, BANK2, 0x1c, 0x1a);
	mst3367_wr(sd, BANK2, 0x1d, 0x00);
	mst3367_wr(sd, BANK2, 0x1e, 0x00);
	mst3367_wr(sd, BANK2, 0x1f, 0x00);
	mst3367_clr(sd, BANK2, 0x25, 0xa2);
	mst3367_set(sd, BANK2, 0x25, 0xa2);

	/* */
	mst3367_set(sd, BANK2, 0x02, 0x80);
	mst3367_set(sd, BANK2, 0x07, 0x04);
	mst3367_wr(sd, BANK2, 0x17, 0xc0);
	mst3367_wr(sd, BANK2, 0x19, 0xff);
	mst3367_wr(sd, BANK2, 0x1a, 0xff);
	mst3367_wr(sd, BANK2, 0x1b, 0xfc);
	mst3367_wr(sd, BANK2, 0x20, 0x00);
	mst3367_clr(sd, BANK2, 0x21, 0x03);
	mst3367_wr(sd, BANK2, 0x22, 0x26);
	mst3367_wr(sd, BANK2, 0x27, 0x00);
	mst3367_set(sd, BANK2, 0x2e, 0xa1);

	/* */
	mst3367_wr(sd, BANK0, 0xab, 0x15); // // [2012.05.08] [COLOR.RANGE] 0x15
	mst3367_clr(sd, BANK0, 0xac, 0x3f);
	mst3367_set(sd, BANK0, 0xac, 0x15);

	/* RxSwitchSource - HDMI */
	MST3367_TMDS_HOT_PLUG(sd, RX_TMDS_HPD_OFF);
	MST3367_HDCP_RESET(sd);
	MST3367_HDMI_RESET(sd);
	mst3367_wr(sd, BANK0, 0x51, 0x89);
	MST3367_TMDS_HOT_PLUG(sd, RX_TMDS_A_HPD_ON | RX_TMDS_A_LINK_ON);
	mst3367_wr(sd, BANK0, 0xB7, 0x00);

	/* Patches */
	mst3367_wr(sd, BANK0, 0xE2, 0x00); /* DISABLE AUTO POSITION */
	mst3367_wr(sd, BANK0, 0x1e, 0x11); /* UNKNOWN */
	mst3367_wr(sd, BANK0, 0x1f, 0x01); /* UNKNOWN */
	mst3367_wr(sd, BANK0, 0x73, 0x90); /* UNKNOWN */
	mst3367_wr(sd, BANK0, 0xb5, 0x0c); /* UNKNOWN */

	/* CSC */
	mst3367_wr(sd, BANK0, 0x90, 0x15); /* Color Range */
	mst3367_wr(sd, BANK0, 0x91, 0x15);
	mst3367_wr(sd, BANK0, 0x92, 0x62);

	for (i = 0; i < sizeof(csctbl); i++)
		mst3367_wr(sd, BANK0, 0x93 + i, csctbl[i]);

	//mst3367_set(sd, BANK0, 0xB0, 0x25 ); /* RX_OUTPUT_YUV422 / 10.BITS / EXTERNAL SYNC */
	//mst3367_set(sd, BANK0, 0xB0, 0x21 ); /* RX_OUTPUT_YUV422 / 08.BITS / EMBEDDED SYNC */
	//mst3367_set(sd, BANK0, 0xB0, 0x24 ); /* RX_OUTPUT_YUV422 / 10.BITS / EXTERNAL SYNC */
	mst3367_wr(sd, BANK0, 0xB0, 0x20 ); /* RX_OUTPUT_YUV422 / 08.BITS / EXTERNAL SYNC */

	MST3367_HDMI_INIT(sd);
}

static int mst3367_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct mst3367_platform_data *pdata = client->dev.platform_data;
	struct mst3367_state *state;
	struct v4l2_ctrl_handler *hdl;
	struct v4l2_subdev *sd;
	int err = -EIO;

	dprintk(1, "%s()\n", __func__);

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		printk(KERN_ERR "%s() no dice!\n", __func__);
		return -EIO;
	}

	v4l_dbg(1, debug, client, "detecting mst3367 client on address 0x%x\n",
		client->addr << 1);

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->current_bank = 0xff;
	state->i2c = client->adapter;
	state->i2c_addr = 0x9c;

	/* Platform data */
	if (pdata == NULL) {
		v4l_err(client, "No platform data!\n");
		return -ENODEV;
	}
	memcpy(&state->pdata, pdata, sizeof(state->pdata));

	sd = &state->sd;
	v4l2_i2c_subdev_init(sd, client, &mst3367_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	hdl = &state->hdl;
	v4l2_ctrl_handler_init(hdl, 2);

	state->hotplug_ctrl = v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_DV_TX_HOTPLUG, 0, 1, 0, 0);
	state->rx_sense_ctrl = v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_DV_TX_RXSENSE, 0, 1, 0, 0);
	sd->ctrl_handler = hdl;
	if (hdl->error) {
		err = hdl->error;
		goto err_hdl;
	}

	if (mst3367_rd(sd, BANK0, 0x50) != 1) {
		v4l2_err(sd, "chip_revision != 1\n");
		err = -EIO;
		goto err_hdl;
	}

	state->detectedSignal = 0;

	mst3367_init_setup(sd);
	mst3367_audio_setup(sd);
	v4l2_ctrl_handler_setup(&state->hdl);

	v4l2_info(sd, "%s found and initialized @ addr 0x%x (%s)\n", client->name,
		  client->addr << 1, client->adapter->name);

	if (debug)
		pr_info(KBUILD_MODNAME ": Debugging is enabled\n");

	pr_info(KBUILD_MODNAME ": driver loaded\n");

	return 0;

err_hdl:
	v4l2_ctrl_handler_free(&state->hdl);
	return err;
}

static int mst3367_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	//struct mst3367_state *state = get_mst3367_state(sd);

	dprintk(1, "%s()\n", __func__);

	v4l2_dbg(1, debug, sd, "%s removed @ 0x%x (%s)\n", client->name,
		 client->addr << 1, client->adapter->name);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pr_info(KBUILD_MODNAME ": driver unloaded\n");

	return 0;
}

static struct i2c_device_id mst3367_id[] = {
	{ "mst3367", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mst3367_id);

static struct i2c_driver mst3367_driver = {
	.driver = {
		.name = "mst3367",
	},
	.probe    = mst3367_probe,
	.remove   = mst3367_remove,
	.id_table = mst3367_id,
};

module_i2c_driver(mst3367_driver);

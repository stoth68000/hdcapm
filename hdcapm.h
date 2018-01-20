/*
 *  Driver for the Startech USB2HDCAPM USB capture device
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

#ifndef _HDCAPM_H
#define _HDCAPM_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/usb.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/firmware.h>
#include <linux/timer.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>

#include "hdcapm-reg.h"
#include "kl-histogram.h"

extern int hdcapm_i2c_scan;
extern int hdcapm_debug;
#define dprintk(level, fmt, arg...)\
	do { if (hdcapm_debug >= level)\
		printk(KERN_DEBUG KBUILD_MODNAME ": " fmt, ## arg);\
	} while (0)

#define HDCAPM_CARD_REV1 1

#define PIPE_EP1 0x01
#define PIPE_EP2 0x02
#define PIPE_EP3 0x83
#define PIPE_EP4 0x04

#define TIMER_EVAL 0

extern struct usb_device_id hdcapm_usb_id_table[];

struct hdcapm_dev;
struct hdcapm_statistics;

enum transition_state_e {
        STATE_UNDEFINED = 0,
        STATE_START,	/* V4L2 read() or poll() advanced to _START state. */
        STATE_STARTED,	/* kernel thread notices _START state, starts the firmware and moves state to _STARTED. */
        STATE_STOP,     /* V4L2 close advances from _STARTED to _STOP. */
        STATE_STOPPED,  /* kernel thread notices _STOPPING, stops firmware and moves to STOPPED state. */
};

struct hdcapm_encoder_parameters {

	/* TODO: Mostly all todo items. */
	u32 audio_mute;
	u32 brightness;
	u32 bitrate_bps;
	u32 bitrate_peak_bps;
	u32 bitrate_mode;
	u32 gop_size;

	u32 h264_profile; /* H264 profile BASELINE etc */
	u32 h264_level; /* H264 profile 4.1 etc */
	u32 h264_entropy_mode; /* CABAC = 1 / CAVLC = 0 */
	u32 h264_mode; /* VBR = 1, CBR = 0 */
};

struct hdcapm_fh {
	struct v4l2_fh fh;
	struct hdcapm_dev *dev;
	atomic_t v4l_reading;
};

struct hdcapm_i2c_bus {
	struct hdcapm_dev *dev;
	int nr;
	struct i2c_adapter i2c_adap;
	struct i2c_client  i2c_client;
	struct i2c_algo_bit_data i2c_algo;
};

struct hdcapm_dev {
	struct list_head devlist;

	char name[32];

	enum transition_state_e state;
	int thread_active;
        struct task_struct *kthread;

	struct hdcapm_statistics *stats;

	/* Held by the follow driver features.
	 * 1. During probe and disconnect.
	 * 2. When writing commands to the firmware.
	 */
	struct mutex lock;

	struct usb_device *udev;

	/* We need to xfer USB buffers off the stack, put them here. */
	u8  *xferbuf;
	u32  xferbuf_len;

	/* I2C.
	 * Bus0 - MST3367.
	 * Bus1 - Sonix chip.
	 */
	struct hdcapm_i2c_bus i2cbus[2];
	//struct i2c_client *i2c_client_hdmi;
	struct v4l2_subdev *sd;

	/* V4L2 */
	struct v4l2_device v4l2_dev;
	struct video_device *v4l_device;
	struct v4l2_ctrl_handler ctrl_handler;
	atomic_t v4l_reader_count;
	struct hdcapm_encoder_parameters encoder_parameters;
#if TIMER_EVAL
	struct timer_list ktimer;
	struct hrtimer hrtimer;
#endif

	/* User buffering */
	struct mutex dmaqueue_lock;
	struct list_head list_buf_free;
	struct list_head list_buf_used;
	wait_queue_head_t wait_read;
};

struct hdcapm_buffer {
	struct list_head   list;

	int                nr;
	struct hdcapm_dev *dev;
	struct urb        *urb;

	u8  *ptr;
	u32  maxsize;
	u32  actual_size;
	u32  readpos;
};

struct hdcapm_statistics {

	/* Number of times the driver stole a used buffer to satisfy a free buffer streaming request. */
	u64 buffer_overrun;

	/* The amount of data we've received from the firmware (video/audio codec data). */
	u64 codec_bytes_received;

	/* The number of buffers we're received full of codec data (video/audio codec data). */
	u64 codec_buffers_received;

	/* Any time we call the codec to check for a TS buffer, and it replies that it doesn't yet have one. */
	u64 codec_ts_not_yet_ready;

	struct kl_histogram usb_read_call_interval;
	struct kl_histogram usb_read_sleeping;
	struct kl_histogram usb_codec_transfer;
	struct kl_histogram usb_codec_status;
	struct kl_histogram usb_buffer_handoff;
	struct kl_histogram usb_buffer_acquire;
	struct kl_histogram timer_callbacks;
	struct kl_histogram hrtimer_callbacks;
	struct kl_histogram v4l2_read_call_interval;
};
static __inline__ void hdcapm_core_statistics_reset(struct hdcapm_dev *dev)
{
	struct hdcapm_statistics *s = dev->stats;

	memset(s, 0, sizeof(*s));
	kl_histogram_reset(&s->usb_read_call_interval, "usb_read call interval", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->usb_read_sleeping, "usb read sleeping", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->usb_buffer_handoff, "usb buffer full handoff", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->usb_buffer_acquire, "usb buffer free acquire", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->usb_codec_transfer, "usb codec transfer", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->usb_codec_status, "usb codec status read", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->v4l2_read_call_interval, "v4l2 read() call interval", KL_BUCKET_VIDEO);
#if TIMER_EVAL
	kl_histogram_reset(&s->timer_callbacks, "timer cb intervals (1ms)", KL_BUCKET_VIDEO);
	kl_histogram_reset(&s->hrtimer_callbacks, "hrtimer cb intervals (4ms)", KL_BUCKET_VIDEO);
#endif
}

/* -core.c */
int hdcapm_write32(struct hdcapm_dev *dev, u32 addr, u32 val);
int hdcapm_read32(struct hdcapm_dev *dev, u32 addr, u32 *val);

/* Read N DWORDS from the firmware and optionally convert the LE firmware dwords to platform CPU DWORDS. */
int hdcapm_read32_array(struct hdcapm_dev *dev, u32 addr, u32 wordcount, u32 *arr, int le_to_cpu);

void hdcapm_set32(struct hdcapm_dev *dev, u32 addr, u32 mask);
void hdcapm_clr32(struct hdcapm_dev *dev, u32 addr, u32 mask);

int hdcapm_dmawrite32(struct hdcapm_dev *dev, u32 addr, const u32 *arr, u32 entries);
int hdcapm_dmaread32(struct hdcapm_dev *dev, u32 addr, u32 *arr, u32 entries);
int hdcapm_mem_write32(struct hdcapm_dev *dev, u32 addr, u32 val);
int hdcapm_mem_read32(struct hdcapm_dev *dev, u32 addr, u32 *val);

int hdcapm_core_ep_send(struct hdcapm_dev *dev, int endpoint, u8 *buf, u32 len, u32 timeout);
int hdcapm_core_ep_recv(struct hdcapm_dev *dev, int endpoint, u8 *buf, u32 len, u32 *actual, u32 timeout);

int hdcapm_core_stop_streaming(struct hdcapm_dev *dev);
int hdcapm_core_start_streaming(struct hdcapm_dev *dev);
void hdcapm_core_statistics_reset(struct hdcapm_dev *dev);

/* -i2c.c */
int hdcapm_i2c_register(struct hdcapm_dev *dev, struct hdcapm_i2c_bus *bus, int nr);
void hdcapm_i2c_unregister(struct hdcapm_dev *dev, struct hdcapm_i2c_bus *bus);

/* -buffer.c */
struct hdcapm_buffer *hdcapm_buffer_alloc(struct hdcapm_dev *dev, u32 nr, u32 maxsize);
void hdcapm_buffer_free(struct hdcapm_buffer *buf);
void hdcapm_buffers_move_all(struct hdcapm_dev *dev, struct list_head *to, struct list_head *from);
void hdcapm_buffers_free_all(struct hdcapm_dev *dev, struct list_head *head);
struct hdcapm_buffer *hdcapm_buffer_next_free(struct hdcapm_dev *dev);
struct hdcapm_buffer *hdcapm_buffer_peek_used(struct hdcapm_dev *dev);
void hdcapm_buffer_move_to_free(struct hdcapm_dev *dev, struct hdcapm_buffer *buf);
void hdcapm_buffer_move_to_used(struct hdcapm_dev *dev, struct hdcapm_buffer *buf);
void hdcapm_buffer_add_to_free(struct hdcapm_dev *dev, struct hdcapm_buffer *buf);
void hdcapm_buffer_add_to_used(struct hdcapm_dev *dev, struct hdcapm_buffer *buf);

/* -compressor.c */
int  hdcapm_compressor_register(struct hdcapm_dev *dev);
void hdcapm_compressor_unregister(struct hdcapm_dev *dev);
void hdcapm_compressor_run(struct hdcapm_dev *dev);

/* -video.c */
int  hdcapm_video_register(struct hdcapm_dev *dev);
void hdcapm_video_unregister(struct hdcapm_dev *dev);

#endif /* _HDCAPM_H */

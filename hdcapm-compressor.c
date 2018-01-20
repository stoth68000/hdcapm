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

#include "hdcapm.h"

#define CMD_ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(u32))

static int hdcapm_compressor_enable_firmware(struct hdcapm_dev *dev, int val);

static char *cmd_name(u32 id)
{
	switch(id) {
	case 0x01: return "Start Compressor";
	case 0x02: return "Stop Compressor";
	case 0x10: return "Configure Compressor Interface";
	default:   return "Undefined";
	}
}

/* Wait up to 500ms for the firmware to be ready, or return a timeout.
 * On idle, value 1 is return else < 0 indicates an error.
 */
static int fw_check_idle(struct hdcapm_dev *dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	int ret = -ETIMEDOUT;
	u32 val;

	while (!time_after(jiffies, timeout)) {

		if (hdcapm_read32(dev, REG_FW_CMD_BUSY, &val) != 0) {
			ret = -EINVAL; /* Error trying to read register. */
			break;
		}

		if (val == 0) {
			ret = 1; /* Success - Firmware is idle. */
			break;
		}

		msleep(10);
	}

	return ret;
}

/* Send a command to the firmware.
 *
 * Firmware commands and arguments are passed to this function for
 * transmission to the hardware.
 * An array of u32s, with the first u32 being the command type, followed
 * by N arguments that are written to ARGS[0-n].
 * Return 0 on success else < 0.
 */
static int execute_cmd(struct hdcapm_dev *dev, const u32 *cmdarr, u32 entries)
{
	int ret;
	int i;

	/* Check hardware is ready */
	mutex_lock(&dev->lock);

	if (fw_check_idle(dev)) {

		dprintk(1, "FIRMWARE CMD = 0x%08x [%s]\n", *cmdarr, cmd_name(*cmdarr));

		/* Send a new command to the hardware/firmware. */
		/* Write all args into the FW arg registers */
		for (i = 1; i < entries; i++) {
			dprintk(1, "           %2d: 0x%08x\n", i - 1, *(cmdarr + i));
		}
		for (i = 1; i < entries; i++) {
			hdcapm_write32(dev, REG_FW_CMD_ARG(i - 1), *(cmdarr + i));
		}

		/* Prepare the firmware to execute a command. */
		hdcapm_write32(dev, REG_FW_CMD_BUSY, 1);

		/* Trigger the command execution. */
		hdcapm_write32(dev, REG_FW_CMD_EXECUTE, *cmdarr);

		ret = 0; /* Success */
	} else
		ret = -EINVAL;
 
	mutex_unlock(&dev->lock);

	return ret;
}

#if 0
/* 29298 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_01[] = {
	0x00000001,
	0x2101b219, // 6f8
	0x02d00500, // 6f4
	0x1e3c0609, // 6f0
	//0x1e3c0649, // 6f0
	0x80504e20, // 6ec
	0x4e204e20, // 6e8
	0x48002000, // 6e4
	0xe0010001, // 6e0
	0x02d00500, // 6dc ???
	0xc00000d0, // 6d8
	0x21121080, // 6d4
	0x465001f2, // 6d0
};
#endif

/* 79416 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_02[] = {
	0x00000002,
	0x00000000,
};

/* 29513 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_0a[] = {
	0x0000000a,
	0x00000008,
};

/* 27813 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_f1[] = {
	0x000000f1,
	0x80000011,
};

/* 27873 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_f2[] = {
	0x000000f2,
	0x01000100,
};

/* 80184 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_f3[] = {
	0x000000f3,
	0x00000000,
};

/* 28548 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_0f[] = {
	0x00000010,
	0x0000000f,
	0x00000000,
	0x00000000,
};

/* 28643 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_10[] = {
	0x00000010,
	0x00000010,
	0x00000000,
	0x00000000,
	0x00000000,
};

/* 28743 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_12[] = {
	0x00000010,
	0x00000012,
	0x00000000,
};

/* 28823 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_13[] = {
	0x00000010,
	0x00000013,
	0x00000050,
	0x00000000,
	0x0000000a,
};

/* 29028 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_16[] = {
	0x00000010,
	0x00000016,
	0x00000000,
	0x00000000,
};

/* 29123 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_17[] = {
	0x00000010,
	0x00000017,
	0x00000000,
};

/* 29203 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_10_02[] = {
	0x00000010,
	0x00000002,
	0xf1f1f1da,
	0xb6f1f1b6,
};

/* LGPEncoder/complete-trace.tdc
 * Line 29683: EP4 -> 01 00 07 00 B0 06 00 00 (query buffer availablility, read 7 words from address 6b0)
 * Line 29688: EP3 <- 40 00 00 00 83 00 00 00 00 1F 3E 00 00 00 00 00 3F 5B 00 00 AA AA AA AA 01 00 00 00
 * Line 29718  EP4 -> 09 00 08 00 00 00 00 00 00 1F 3E 00 3F 5B 00 00
 * The buffer is transferred via EP1 IN, note that teh ISO13818 DWORDS are byte reversed..... then this message
 * is sent to the firmware to acknowledge the buffer was read?
 */
/* 30739 in LGPEncoder/complete-trace.tdc */
static const u32 cmd_30[] = {
	0x00000030, /* Fixed value */
	0x00000083, /* Fixed value */
	0x00005b3f, /* Number of DWORDS we previously read. */
	0x00010007, /* Fixed value */
	0x2aaaaaaa, /* Fixed value */
	0x00000000, /* Fixed value */
	0x00000000, /* Fixed value */
};

static int firmware_transition(struct hdcapm_dev *dev, int run, struct v4l2_dv_timings *timings)
{
	/* 29298 in LGPEncoder/complete-trace.tdc */
	u32 cfg[12];

	u32 i_width, i_height, i_fps;
	u32 o_width, o_height, o_fps;
	u32 min_bitrate_kbps = dev->encoder_parameters.bitrate_bps / 1000;
	u32 max_bitrate_kbps = dev->encoder_parameters.bitrate_peak_bps / 1000;
	u32 htotal, vtotal;
	u32 timing_fpsx100;

	dprintk(1, "%s(%p, %s)\n", __func__, dev, run == 1 ? "START" : "STOP");
	if (run) {
		if (!timings) {
			pr_err(KBUILD_MODNAME ": no timing during firmware transition\n");
			return -EINVAL;
		}

		/* Prepare the video/audio compression settings. */
		i_width  = timings->bt.width;
		i_height = timings->bt.height;
		htotal   = V4L2_DV_BT_FRAME_WIDTH(&timings->bt);
		vtotal   = V4L2_DV_BT_FRAME_HEIGHT(&timings->bt);
		if (htotal * vtotal) {
			timing_fpsx100 = div_u64((100 * (u64)timings->bt.pixelclock), (htotal * vtotal));
		} else {
			pr_err(KBUILD_MODNAME ": no fps calulated\n");
			return -EINVAL;
		}

		i_fps = timing_fpsx100 / 100;

		o_width  = i_width;
		o_height = i_height;
		o_fps    = i_fps;

		/* Scaling. Adjust width, height and output fps. */
		/* Hardware can't handle anything above p30, drop frames from p60 to 30, p50 to 25. */
		if ((timings->bt.width == 1920) && (timings->bt.height == 1080) &&
			(!timings->bt.interlaced) && (i_fps > 30)) {
			o_fps /= 2;
		}

		cfg[ 0] = 0x00000001;
//MMM
		cfg[ 1] = 0x21010019 |
			(dev->encoder_parameters.h264_level << 12) |
			(dev->encoder_parameters.h264_entropy_mode << 26) |
			(dev->encoder_parameters.h264_profile << 8);

		cfg[ 2] = i_height << 16 | i_width;
		cfg[ 3] = o_fps << 23 | i_fps << 16 | 0x0609;
		cfg[ 4] = 0x0050 << 16 | min_bitrate_kbps | (dev->encoder_parameters.h264_mode << 31);
		cfg[ 5] = max_bitrate_kbps << 16 | min_bitrate_kbps;
		cfg[ 6] = 0x48002000;
		cfg[ 7] = 0xe0010000 | dev->encoder_parameters.gop_size;
		cfg[ 8] = o_height << 16 | o_width;
		cfg[ 9] = 0xc00000d0;
		cfg[10] = 0x21121080;
		cfg[11] = 0x465001f2;

		hdcapm_compressor_enable_firmware(dev, 1);

		/* From LGP device dump line 27788 */
		execute_cmd(dev, cmd_f1, CMD_ARRAY_SIZE(cmd_f1));
		execute_cmd(dev, cmd_f2, CMD_ARRAY_SIZE(cmd_f2));

		/* Configure the video / audio compressors. */
		execute_cmd(dev, cmd_10_0f, CMD_ARRAY_SIZE(cmd_10_0f)); /* */
		execute_cmd(dev, cmd_10_10, CMD_ARRAY_SIZE(cmd_10_10)); /* */
		execute_cmd(dev, cmd_10_12, CMD_ARRAY_SIZE(cmd_10_12)); /* */
		execute_cmd(dev, cmd_10_13, CMD_ARRAY_SIZE(cmd_10_13)); /* */
		execute_cmd(dev, cmd_10_16, CMD_ARRAY_SIZE(cmd_10_16)); /* */
		execute_cmd(dev, cmd_10_17, CMD_ARRAY_SIZE(cmd_10_17)); /* */
		execute_cmd(dev, cmd_10_02, CMD_ARRAY_SIZE(cmd_10_02)); /* */

		/* Configure and start encoder. */
		execute_cmd(dev, cfg, CMD_ARRAY_SIZE(cfg)); /* Start */
		execute_cmd(dev, cmd_0a, CMD_ARRAY_SIZE(cmd_0a)); /* */
#if 0
		msleep(500);

printk("Stopping(1)\n");
		/* Stop and disable encoder. */
		execute_cmd(dev, cmd_02, CMD_ARRAY_SIZE(cmd_02)); /* Stop */
		execute_cmd(dev, cmd_f3, CMD_ARRAY_SIZE(cmd_f3));

printk("Starting(2)\n");
		/* Now start it again. */
		execute_cmd(dev, cmd_f1, CMD_ARRAY_SIZE(cmd_f1));
		execute_cmd(dev, cmd_f2, CMD_ARRAY_SIZE(cmd_f2));
		execute_cmd(dev, cfg, CMD_ARRAY_SIZE(cfg)); /* Start */
		execute_cmd(dev, cmd_0a, CMD_ARRAY_SIZE(cmd_0a)); /* */
#endif
	} else {
		/* Stop and disable encoder. */
		execute_cmd(dev, cmd_02, CMD_ARRAY_SIZE(cmd_02)); /* Stop */
		execute_cmd(dev, cmd_f3, CMD_ARRAY_SIZE(cmd_f3));

		//hdcapm_compressor_enable_firmware(dev, 0);
		//hdcapm_write32(dev, REG_FW_CMD_BUSY, 0);
	}

	return 0;
}

/* Perform a status read of the compressor. If TS data is available then
 * query that and push the buffer into a user queue for later processing.
 */
static int usb_read(struct hdcapm_dev *dev)
{
	struct hdcapm_buffer *buf;
	u32 val;
	u32 arr[7];
	u8 r[4];
	int ret, i;
	u32 bytes_to_read;

	/* Query the Compressor regs 0x6b0-0x6c8. Determine whether a buffer is ready for transfer.
	 * Reg 6b0 (0): Status indicator?
	 * Reg 6b4 (1): F1 always 0x83
	 * Reg 6b8 (2): Transport buffer address (on host h/w)
	 * Reg 6bc (3):
	 * Reg 6c0 (4): Number of dwords
	 * Reg 6c4 (5):
	 * Reg 6c8 (6):
	 * Line 55674 - LGPEncoder/complete-trace.tdc
	 */

	kl_histogram_sample_begin(&dev->stats->usb_codec_status);
	ret = hdcapm_read32_array(dev, REG_06B0, ARRAY_SIZE(arr), &arr[0], 1);
	if (ret < 0) {
		/* Failure to read from the device. */
		return -EINVAL;
	}
	kl_histogram_sample_complete(&dev->stats->usb_codec_status);

	dprintk(3, "tsb reply: %08x %08x %08x %08x %08x %08x %08x\n",
		arr[0], arr[1], arr[2], arr[3], arr[4], arr[5], arr[6]);

	/* Check the reply */
	if (arr[6] == 0) {
		/* Buffer not yet ready. */
		dev->stats->codec_ts_not_yet_ready++;
		return -ETIMEDOUT;
	}

	/* Check this is a TS buffer */
	if ((arr[0] & 0xff) != 0x40) {
		/* Unexpected, debug this. */
		printk(KERN_ERR "tsb reply: %08x %08x %08x %08x %08x %08x %08x (No 0x40?)\n",
			arr[0], arr[1], arr[2], arr[3], arr[4], arr[5], arr[6]);
		//BUG();
	}

	/* Check this other fixed value. */
	if ((arr[1] & 0xff) != 0x83) {
		/* Unexpected, debug this. */
		printk(KERN_ERR "tsb reply: %08x %08x %08x %08x %08x %08x %08x (No 0x83?)\n",
			arr[0], arr[1], arr[2], arr[3], arr[4], arr[5], arr[6]);
		BUG();
	}

	bytes_to_read = arr[4] * sizeof(u32);
	if (bytes_to_read > 256000) {
		/* Unexpected, debug this. */
		printk(KERN_ERR "tsb reply: %08x %08x %08x %08x %08x %08x %08x (Too many dwords?)\n",
			arr[0], arr[1], arr[2], arr[3], arr[4], arr[5], arr[6]);
		BUG();
	}

	/* We need a buffer to transfer the TS into. */
	kl_histogram_sample_begin(&dev->stats->usb_buffer_acquire);
	buf = hdcapm_buffer_next_free(dev);
	if (!buf)
		return -EINVAL;

	kl_histogram_sample_complete(&dev->stats->usb_buffer_acquire);

	kl_histogram_update(&dev->stats->usb_read_call_interval);

	/* Transfer buffer from the USB device (address arr[2]), length arr[4]). */
	kl_histogram_sample_begin(&dev->stats->usb_codec_transfer);
	ret = hdcapm_dmaread32(dev, arr[2], (u32 *)buf->ptr, arr[4]);
	if (ret < 0) {
		/* Throw the buffer back in the free list. */
		hdcapm_buffer_add_to_free(dev, buf);
		return -EINVAL;
	}
	kl_histogram_sample_complete(&dev->stats->usb_codec_transfer);

#if 1
	/* The buffer comes back in DWORD ordering, we need to fixup the payload to
	 * put the TS packet bytes back into the right order.
	 */
	for (i = 0; i < bytes_to_read; i += 4) {
		r[0] = *(buf->ptr + i + 3);
		r[1] = *(buf->ptr + i + 2);
		r[2] = *(buf->ptr + i + 1);
		r[3] = *(buf->ptr + i + 0);

		*(buf->ptr + i + 0) = r[0];
		*(buf->ptr + i + 1) = r[1];
		*(buf->ptr + i + 2) = r[2];
		*(buf->ptr + i + 3) = r[3];
	}
#endif

	dev->stats->codec_bytes_received += bytes_to_read; 
	dev->stats->codec_buffers_received++;

	/* Put the buffer on the used list, the caller will read/dequeue it later. */
	kl_histogram_sample_begin(&dev->stats->usb_buffer_handoff);
	buf->actual_size = bytes_to_read;
	buf->readpos = 0;
	hdcapm_buffer_add_to_used(dev, buf);
	kl_histogram_sample_complete(&dev->stats->usb_buffer_handoff);

	/* Signal to any userland waiters, new buffer available. */
	wake_up_interruptible(&dev->wait_read);

	/* Acknowledge the buffer back to the firmware. */
	hdcapm_read32(dev, 0x800, &val);
	hdcapm_write32(dev, 0x800, val);

	hdcapm_write32(dev, REG_FW_CMD_ARG(0), 0x83);
	hdcapm_write32(dev, REG_FW_CMD_ARG(1), arr[4]);
	hdcapm_write32(dev, REG_FW_CMD_ARG(2), 0x2aaaaaaa);
	hdcapm_write32(dev, REG_FW_CMD_ARG(3), 0);
	hdcapm_write32(dev, REG_FW_CMD_ARG(5), 0);
	hdcapm_write32(dev, REG_FW_CMD_BUSY, 1);
	hdcapm_write32(dev, REG_FW_CMD_EXECUTE, 0x30);

	hdcapm_write32(dev, 0x6c8, 0);
	
	return 0;
}

static void hdcapm_compressor_init_gpios(struct hdcapm_dev *dev)
{
	// 38045 - bit toggling, gpios

	/* Available GPIO's 15-0. */

	/* Configure GPIO's 3-1, 8, 11, 12 as outputs. */
	hdcapm_set32(dev, REG_GPIO_OE, 0x2);
	hdcapm_set32(dev, REG_GPIO_OE, 0x4);
	hdcapm_set32(dev, REG_GPIO_OE, 0x8);
	hdcapm_set32(dev, REG_GPIO_OE, 0x100);
	hdcapm_set32(dev, REG_GPIO_OE, 0x800);
	hdcapm_set32(dev, REG_GPIO_OE, 0x1000);
	/* Reg should end up at 190E */

	/* Pull all GPIO's high. */
	hdcapm_clr32(dev, REG_GPIO_DATA_WR, 0xFFFFFFFF);

	/* GPIO #2 is the MST3367 reset, active high, */
	/* TODO: is this register inverted, meaning writes high resuilt in low? */
	hdcapm_set32(dev, REG_GPIO_DATA_WR, 0x00000004);
}

static int hdcapm_compressor_enable_firmware(struct hdcapm_dev *dev, int val)
{
	// 32527
	/* EP4 Host -> 07 01 00 00 00 00 00 00 */
	u8 tx[] = {
		0x07,
		 val, /* 0 = stop, 1 = start. */
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
	};

	dprintk(2, "%s()\n", __func__);

	/* Flush this to EP4 via a bulk write. */
	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	return 0; /* Success */
}

int hdcapm_compressor_register(struct hdcapm_dev *dev)
{
	const struct firmware *fw = NULL;
	const char *fw_video = "v4l-hdcapm-vidfw-01.fw";
	size_t fw_video_len = 453684;
	const char *fw_audio = "v4l-hdcapm-audfw-01.fw";
	size_t fw_audio_len = 363832;
	u32 val;
	u32 *dwords;
	u32 addr;
	u32 chunk;
	u32 cpy;
	u32 offset;
	int ret;

	hdcapm_write32(dev, REG_081C, 0x00004000);
	hdcapm_write32(dev, REG_0820, 0x00103FFF);
	hdcapm_write32(dev, REG_0824, 0x00000000);
	hdcapm_write32(dev, REG_0828, 0x00104000);
	hdcapm_write32(dev, REG_082C, 0x00203FFF);
	hdcapm_write32(dev, REG_0830, 0x00100000);
	hdcapm_write32(dev, REG_0834, 0x00204000);
	hdcapm_write32(dev, REG_0838, 0x00303FFF);
	hdcapm_write32(dev, REG_083C, 0x00200000);
	hdcapm_write32(dev, REG_0840, 0x70003124);
	hdcapm_write32(dev, REG_0840, 0x90003124);

#if 0
	hdcapm_read32(dev, REG_0038, &val);
	dprintk(1, "%08x = %08x\n", REG_0038, val);
//	BUG_ON(val != 0x00010020);
#endif

	hdcapm_write32(dev, REG_GPIO_OE, 0x00000000);
	hdcapm_write32(dev, REG_GPIO_DATA_WR, 0x00000000);

#if 0
	hdcapm_read32(dev, REG_0050, &val);
	dprintk(1, "%08x = %08x\n", REG_0050, val);
//	BUG_ON(val != 0x00200406);
#endif

	hdcapm_write32(dev, REG_0050, 0x00200406);
	hdcapm_read32(dev, REG_0050, &val);
#if 0
	dprintk(1, "%08x = %08x\n", REG_0050, val);
//	BUG_ON(val != 0x00200406);
#endif

	/* Give the device enough time to boot its initial microcode. */
	msleep(1000);

	hdcapm_compressor_enable_firmware(dev, 0);

	// 12570
	hdcapm_write32(dev, REG_FW_CMD_BUSY, 0x00000000);
	hdcapm_write32(dev, REG_081C, 0x00004000);

	hdcapm_write32(dev, REG_0820, 0x00103FFF);
	hdcapm_write32(dev, REG_0824, 0x00000000);
	hdcapm_write32(dev, REG_0828, 0x00104000);
	hdcapm_write32(dev, REG_082C, 0x00203FFF);
	hdcapm_write32(dev, REG_0830, 0x00100000);
	hdcapm_write32(dev, REG_0834, 0x00204000);
	hdcapm_write32(dev, REG_0838, 0x00303FFF);
	hdcapm_write32(dev, REG_083C, 0x00200000);
	hdcapm_write32(dev, REG_0840, 0x70003124);
	hdcapm_write32(dev, REG_0840, 0x90003124);

	// 17568
	hdcapm_write32(dev, REG_0050, 0x00200406);
#if 0
	dprintk(1, "%08x = %08x\n", REG_0050, val);
//	BUG_ON(val != 0x00200406);
#endif

	hdcapm_write32(dev, REG_0050, 0x00200406);
	hdcapm_read32(dev, REG_0050, &val);
#if 0
	dprintk(1, "%08x = %08x\n", REG_0050, val);
//	BUG_ON(val != 0x00200406);
#endif

	// 17588
	hdcapm_write32(dev, REG_0050, 0x00200406);
	hdcapm_write32(dev, REG_GPIO_OE, 0x00000000);
	hdcapm_write32(dev, REG_GPIO_DATA_WR, 0x00000000);

	hdcapm_read32(dev, REG_0000, &val);
#if 0
	dprintk(1, "%08x = %08x\n", REG_0000, val);
//	BUG_ON(val != 0x03FF0300);
#endif
	hdcapm_write32(dev, REG_0000, 0x03FF0300);

	/* Wipe memory at various addresses */
	dwords = kzalloc(0x2000 * sizeof(u32), GFP_KERNEL);
	if (!dwords)
		return -ENOMEM;

	if (hdcapm_dmawrite32(dev, 0x0005634E, dwords, 0x2000) < 0) {
		pr_err(KBUILD_MODNAME ": wipe of addr1 failed\n");
		return -EINVAL;
	}
	if (hdcapm_dmawrite32(dev, 0x0005834E, dwords, 0x2000) < 0) {
		pr_err(KBUILD_MODNAME ": wipe of addr2 failed\n");
		return -EINVAL;
	}
	if (hdcapm_dmawrite32(dev, 0x0005A34E, dwords, 0x1E3B) < 0) {
		pr_err(KBUILD_MODNAME ": wipe of addr3 failed\n");
		return -EINVAL;
	}
	kfree(dwords);

	/* Upload the audio firmware. */
	ret = request_firmware(&fw, fw_audio, &dev->udev->dev);
	if (ret) {
		pr_err(KBUILD_MODNAME
			": failed to find firmware file %s"
			", aborting upload.\n", fw_video);
		return -EINVAL;
	}
	if (fw->size != fw_audio_len) {
		pr_err(KBUILD_MODNAME ": failed video firmware length check\n");
		release_firmware(fw);
		return -EINVAL;
	}
	pr_info(KBUILD_MODNAME ": loading audio firmware size %zu bytes.\n", fw->size);

	offset = 0;
	addr = 0x00040000;
	val = fw_audio_len;
	chunk = 0x2000 * sizeof(u32);
	while (val > 0) {
		if (val > chunk)
			cpy = chunk;
		else
			cpy = val;

		hdcapm_dmawrite32(dev, addr, (const u32 *)fw->data + offset, cpy / sizeof(u32));

		val -= cpy;
		addr += (cpy / sizeof(u32));
		offset += (cpy / sizeof(u32));
	}
	release_firmware(fw);

	// 24757
	hdcapm_mem_write32(dev, 0x000BC425, 1);
	hdcapm_mem_write32(dev, 0x000BC424, 0);
	hdcapm_mem_write32(dev, 0x000BC801, 0);

	// 24757
	hdcapm_write32(dev, REG_0B78, 0x00150000);
	hdcapm_write32(dev, REG_FW_CMD_BUSY, 0x00000000);

	/* Upload the video firmware. */
	ret = request_firmware(&fw, fw_video, &dev->udev->dev);
	if (ret) {
		pr_err(KBUILD_MODNAME
			": failed to find firmware file %s"
			", aborting upload.\n", fw_video);
		return -EINVAL;
	}
	if (fw->size != fw_video_len) {
		pr_err(KBUILD_MODNAME ": failed video firmware length check\n");
		release_firmware(fw);
		return -EINVAL;
	}
	pr_info(KBUILD_MODNAME ": loading video firmware size %zu bytes.\n", fw->size);

	/* Load the video firmware */
	// 24778
	offset = 0;
	addr = 0x00000000;
	val = fw_video_len;
	chunk = 0x2000 * sizeof(u32);
	while (val > 0) {
		if (val > chunk)
			cpy = chunk;
		else
			cpy = val;

		hdcapm_dmawrite32(dev, addr, (const u32 *)fw->data + offset, cpy / sizeof(u32));

		val -= cpy;
		addr += (cpy / sizeof(u32));
		offset += (cpy / sizeof(u32));
	}
	release_firmware(fw);

	hdcapm_compressor_enable_firmware(dev, 1);
	hdcapm_write32(dev, REG_FW_CMD_BUSY, 0x00000000);

	// 38021
	hdcapm_mem_read32(dev, 0x00000040, &val);
#if 0
	dprintk(1, "%s() val = 0x%x\n", __func__, val);
	WARN_ON(val != 0x534f5351); /* QSOS */
#endif

	hdcapm_mem_read32(dev, 0x00000041, &val);
#if 0
	dprintk(1, "%s() val = 0x%x\n", __func__, val);
	WARN_ON(val != 0x0002001e); /* ???? */
#endif

	// 38037
	hdcapm_mem_read32(dev, 0x000Bc804, &val);
#if 0
	dprintk(1, "%s() val = 0x%x\n", __func__, val);
	WARN_ON(val != 0); /* ???? */
#endif

	msleep(100);

	hdcapm_compressor_init_gpios(dev);

	pr_info(KBUILD_MODNAME ": Registered compressor\n");
	return 0;
}

void hdcapm_compressor_unregister(struct hdcapm_dev *dev)
{
	dprintk(1, "%s() Unregistered compressor\n", __func__);
}

void hdcapm_compressor_run(struct hdcapm_dev *dev)
{
	struct v4l2_dv_timings timings;
	int ret;
	int val;

	printk("%s()\n", __func__);

	if (v4l2_subdev_call(dev->sd, video, g_dv_timings, &timings) < 0) {
		return;
	}

	/* Reset the internal counters, bps, buffers processed etc. */
	hdcapm_core_statistics_reset(dev);

	/* Make sure all of our buffers are available again. */
	hdcapm_buffers_move_all(dev, &dev->list_buf_free, &dev->list_buf_used);

#if !(ONETIME_FW_LOAD)
	/* Register the compression codec (it does both audio and video). */
	if (hdcapm_compressor_register(dev) < 0) {
		pr_err(KBUILD_MODNAME ": failed to register compressor\n");
		return;
	}
#endif

	hdcapm_read32(dev, REG_0050, &val);
	val &= ~0x04;
	val &= ~0x02;
	hdcapm_write32(dev, REG_0050, val);

	ret = firmware_transition(dev, 1, &timings);

	dev->state = STATE_STARTED;
	while (dev->state == STATE_STARTED) {
		ret = usb_read(dev);
		kl_histogram_sample_begin(&dev->stats->usb_read_sleeping);
		usleep_range(500, 4000);
		kl_histogram_sample_complete(&dev->stats->usb_read_sleeping);
	}

	ret = firmware_transition(dev, 0, NULL);

#if !(ONETIME_FW_LOAD)
	hdcapm_compressor_unregister(dev);
#endif

	dev->state = STATE_STOPPED;

	hdcapm_buffers_move_all(dev, &dev->list_buf_free, &dev->list_buf_used);
}

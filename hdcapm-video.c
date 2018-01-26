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

#define ENCODER_MIN_BITRATE  2000000
#define ENCODER_MAX_BITRATE 20000000
#define ENCODER_DEF_BITRATE ENCODER_MAX_BITRATE

#define ENCODER_MIN_GOP_SIZE  1
#define ENCODER_MAX_GOP_SIZE 60
#define ENCODER_DEF_GOP_SIZE ENCODER_MAX_GOP_SIZE

static int s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hdcapm_dev *dev = container_of(ctrl->handler, struct hdcapm_dev, ctrl_handler);
	struct hdcapm_encoder_parameters *p = &dev->encoder_parameters;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_AUDIO_MUTE:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_AUDIO_MUTE) = %d\n", __func__, ctrl->val);
		p->audio_mute = ctrl->val;
		break;
	case V4L2_CID_BRIGHTNESS:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_BRIGHTNESS) = %d\n", __func__, ctrl->val);
		p->brightness = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_BITRATE) = %d\n", __func__, ctrl->val);
		p->bitrate_bps = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_PEAK:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_BITRATE_PEAK) = %d\n", __func__, ctrl->val);
		p->bitrate_peak_bps = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_BITRATE_MODE_VBR: p->h264_mode = 1; break;
		case V4L2_MPEG_VIDEO_BITRATE_MODE_CBR: p->h264_mode = 0; break;
		default:
			pr_err(KBUILD_MODNAME ": failed to handle ctrl->id 0x%x, value = %d\n", ctrl->id, ctrl->val);
			ret = -EINVAL;
		}
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_BITRATE_MODE) = %d\n", __func__, ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_GOP_SIZE) = %d\n", __func__, ctrl->val);
		p->gop_size = ctrl->val;

		/* If we're in VBR mode GOP 1 looks bad, force a change to CBR. */
		if (p->gop_size == 1 && p->h264_mode == 1) {
			pr_info(KBUILD_MODNAME ": GOP size 1 produces poor quality, switching from VBR to CBR\n");
			p->h264_mode = 0;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:  p->h264_level =  0; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1B:   p->h264_level =  1; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:  p->h264_level =  2; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:  p->h264_level =  3; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:  p->h264_level =  4; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:  p->h264_level =  5; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:  p->h264_level =  6; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:  p->h264_level =  7; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:  p->h264_level =  8; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:  p->h264_level =  9; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:  p->h264_level = 10; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:  p->h264_level = 11; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:  p->h264_level = 12; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:  p->h264_level = 13; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:  p->h264_level = 14; break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:  p->h264_level = 15; break;
		default:
			pr_err(KBUILD_MODNAME ": failed to handle ctrl->id 0x%x, value = %d\n", ctrl->id, ctrl->val);
			ret = -EINVAL;
		}
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_H264_LEVEL) = %d\n", __func__, ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC: p->h264_entropy_mode = 1; break;
		case V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC: p->h264_entropy_mode = 0; break;
		default:
			pr_err(KBUILD_MODNAME ": failed to handle ctrl->id 0x%x, value = %d\n", ctrl->id, ctrl->val);
			ret = -EINVAL;
		}
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE) = %d\n", __func__, ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE: p->h264_profile = 0; break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN: p->h264_profile = 1; break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH: p->h264_profile = 2; break;
		default:
			pr_err(KBUILD_MODNAME ": failed to handle ctrl->id 0x%x, value = %d\n", ctrl->id, ctrl->val);
			ret = -EINVAL;
		}
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_VIDEO_H264_PROFILE) = %d\n", __func__, ctrl->val);
		break;
	case V4L2_CID_MPEG_STREAM_TYPE:
		dprintk(1, KBUILD_MODNAME ": %s(V4L2_CID_MPEG_STREAM_TYPE) = %d\n", __func__, ctrl->val);
		break;
	default:
		pr_err(KBUILD_MODNAME ": failed to handle ctrl->id 0x%x, value = %d\n", ctrl->id, ctrl->val);
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ctrl_ops ctrl_ops =
{
	.s_ctrl = s_ctrl,
};

static int vidioc_enum_input(struct file *file, void *priv_fh, struct v4l2_input *i)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	if (i->index > 0)
		return -EINVAL;

	snprintf(i->name, sizeof(i->name), "HDMI / DVI");
	i->type = V4L2_INPUT_TYPE_CAMERA;
	i->capabilities = V4L2_IN_CAP_DV_TIMINGS;
#if 0
	i->audioset = 1;
#endif

	return v4l2_subdev_call(dev->sd, video, g_input_status, &i->status);
}

static int vidioc_querycap(struct file *file, void  *priv, struct v4l2_capability *cap)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	strcpy(cap->driver, KBUILD_MODNAME);
	strlcpy(cap->card, dev->name, sizeof(cap->card));
	usb_make_path(dev->udev, cap->bus_info, sizeof(cap->bus_info));

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_READWRITE; //| V4L2_CAP_AUDIO;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	sp->parm.capture.readbuffers = 4;
	return v4l2_subdev_call(dev->sd, video, g_parm, sp);
}

static int vidioc_log_status(struct file *file, void *priv)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	struct hdcapm_statistics *s = dev->stats;
	u64 q_used_bytes, q_used_items;
	struct hdcapm_encoder_parameters *p = &dev->encoder_parameters;

	v4l2_info(&dev->v4l2_dev, "device_state:           %s\n",
		dev->state == STATE_START ? "START" :
		dev->state == STATE_STARTED ? "STARTED" :
		dev->state == STATE_STOP ? "STOP" :
		dev->state == STATE_STOPPED ? "STOPPED" : "UNDEFINED");

	v4l2_info(&dev->v4l2_dev, "device_context:         0x%p\n", dev);
	v4l2_info(&dev->v4l2_dev, "codec_buffers_received: %llu\n", s->codec_buffers_received);
	v4l2_info(&dev->v4l2_dev, "codec_bytes_received:   %llu\n", s->codec_bytes_received);
	v4l2_info(&dev->v4l2_dev, "codec_ts_not_yet_ready: %llu\n", s->codec_ts_not_yet_ready);
	v4l2_info(&dev->v4l2_dev, "buffer_overrun:         %llu\n", s->buffer_overrun);

	if (p->output_width && p->output_height) {
		v4l2_info(&dev->v4l2_dev, "video_scaler_output:    %dx%d\n",
			p->output_width, p->output_height);
	} else {
		v4l2_info(&dev->v4l2_dev, "video_scaler_output:    [native 1:1]\n");
	}

	if (hdcapm_buffer_used_queue_stats(dev, &q_used_bytes, &q_used_items) == 0) {
		v4l2_info(&dev->v4l2_dev, "q_used_bytes:           %llu\n", q_used_bytes);
		v4l2_info(&dev->v4l2_dev, "q_used_items:           %llu\n", q_used_items);
	}

	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_read_call_interval);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_read_sleeping);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_codec_transfer);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_buffer_handoff);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_buffer_acquire);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->usb_codec_status);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->v4l2_read_call_interval);
#if TIMER_EVAL
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->timer_callbacks);
	kl_histogram_print_v4l2_device(&dev->v4l2_dev, &s->hrtimer_callbacks);
#endif

	return v4l2_subdev_call(dev->sd, core, log_status);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
	//struct hdcapm_fh *fh = file->private_data;
	//struct hdcapm_dev *dev = fh->dev;

	*i = 0;

	return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
	//struct hdcapm_fh *fh = file->private_data;
	//struct hdcapm_dev *dev = fh->dev;

	if (i > 0)
		return -EINVAL;

	return 0;
}

#if 0
static int vidioc_g_audio(struct file *file, void *priv, struct v4l2_audio *i)
{
	//struct hdcapm_fh *fh = file->private_data;
	//struct hdcapm_dev *dev = fh->dev;

	i->index = 0;

	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv, const struct v4l2_audio *i)
{
	//struct hdcapm_fh *fh = file->private_data;
	//struct hdcapm_dev *dev = fh->dev;

	if (i > 0)
		return -EINVAL;

	return 0;
}

static int vidioc_enumaudio(struct file *file, void *priv, struct v4l2_audio *i)
{
	//struct hdcapm_fh *fh = file->private_data;
	//struct hdcapm_dev *dev = fh->dev;

	if (i->index > 0)
		return -EINVAL;

	snprintf(i->name, sizeof(i->name), "HDMI");
	i->index = 0;
	i->capability = V4L2_AUDCAP_STEREO;

	return 0;
}
#endif

static int vidioc_g_dv_timings(struct file *file, void *priv, struct v4l2_dv_timings *timings)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	return v4l2_subdev_call(dev->sd, video, g_dv_timings, timings);
}

static int vidioc_s_dv_timings(struct file *file, void *priv, struct v4l2_dv_timings *timings)
{
	return -EINVAL;
}

static int vidioc_enum_dv_timings(struct file *file, void *priv, struct v4l2_enum_dv_timings *timings)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	timings->pad = 0;
	timings->reserved[0] = timings->reserved[1] = 0;
	return v4l2_subdev_call(dev->sd, pad, enum_dv_timings, timings);
}

static int vidioc_dv_timings_cap(struct file *file, void *priv, struct v4l2_dv_timings_cap *cap)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	cap->pad = 0;
	return v4l2_subdev_call(dev->sd, pad, dv_timings_cap, cap);
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc *f)
{
	if (f->index != 0)
		return -EINVAL;

	strlcpy(f->description, "MPEG", sizeof(f->description));
	f->pixelformat = V4L2_PIX_FMT_MPEG;
	f->flags = V4L2_FMT_FLAG_COMPRESSED;
	return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	struct hdcapm_encoder_parameters *p = &dev->encoder_parameters;
	struct v4l2_dv_timings timings;

	if (v4l2_subdev_call(dev->sd, video, g_dv_timings, &timings) < 0)
		return -EINVAL;

	f->fmt.pix.pixelformat    = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline   = 0;
	f->fmt.pix.sizeimage      = 188 * 312;
	f->fmt.pix.colorspace     = V4L2_COLORSPACE_SMPTE170M;

	if (p->output_width)
		f->fmt.pix.width  = p->output_width;
	else
		f->fmt.pix.width  = timings.bt.width;

	if (p->output_height)
		f->fmt.pix.height = p->output_height;
	else
		f->fmt.pix.height = timings.bt.width;

	f->fmt.pix.height         = timings.bt.height;
	f->fmt.pix.field          = V4L2_FIELD_NONE;

	return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	struct hdcapm_encoder_parameters *p = &dev->encoder_parameters;
	struct v4l2_dv_timings timings;

	if (v4l2_subdev_call(dev->sd, video, g_dv_timings, &timings) < 0)
		return -EINVAL;

	/* Its not clear to me if the input resolution changes, if we're required
	 * to preserve the users requested width and height, or default it back
	 * to 1:1 with the input signal.
	 */
	p->output_width = f->fmt.pix.width;
	p->output_height = f->fmt.pix.height;

	f->fmt.pix.pixelformat  = V4L2_PIX_FMT_MPEG;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage    = 188 * 312;
	f->fmt.pix.colorspace   = V4L2_COLORSPACE_SMPTE170M;
	f->fmt.pix.width        = timings.bt.width;
	f->fmt.pix.height       = timings.bt.height;
	f->fmt.pix.field        = V4L2_FIELD_NONE;

	return 0;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format *f)
{
	return vidioc_s_fmt_vid_cap(file, priv, f);
}

static int vidioc_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_event_subscribe(fh, sub, 16, NULL);
	default:
		pr_warn(KBUILD_MODNAME ": event sub->type = 0x%x (UNKNOWN)\n", sub->type);
	}
	return v4l2_ctrl_subscribe_event(fh, sub);
}

static int vidioc_query_dv_timings(struct file *file, void *priv_fh, struct v4l2_dv_timings *timings)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	return v4l2_subdev_call(dev->sd, video, query_dv_timings, timings);
}

static const struct v4l2_ioctl_ops mpeg_ioctl_ops =
{
	.vidioc_enum_input        = vidioc_enum_input,
	.vidioc_querycap          = vidioc_querycap,
	.vidioc_g_parm            = vidioc_g_parm,
	.vidioc_log_status        = vidioc_log_status,
	.vidioc_g_input           = vidioc_g_input,
	.vidioc_s_input           = vidioc_s_input,
#if 0
	.vidioc_g_audio           = vidioc_g_audio,
	.vidioc_s_audio           = vidioc_s_audio,
	.vidioc_enumaudio         = vidioc_enumaudio,
#endif
	.vidioc_g_dv_timings      = vidioc_g_dv_timings,
	.vidioc_s_dv_timings      = vidioc_s_dv_timings,
	.vidioc_query_dv_timings  = vidioc_query_dv_timings,
	.vidioc_enum_dv_timings   = vidioc_enum_dv_timings,
	.vidioc_dv_timings_cap    = vidioc_dv_timings_cap,
	.vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
	.vidioc_subscribe_event   = vidioc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int fops_open(struct file *file)
{
	struct hdcapm_dev *dev;
	struct hdcapm_fh *fh;

	dev = (struct hdcapm_dev *)video_get_drvdata(video_devdata(file));
	if (!dev)
		return -ENODEV;

	dprintk(2, "%s()\n", __func__);

	/* allocate + initialize per filehandle data */
	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (NULL == fh)
		return -ENOMEM;

	fh->dev = dev;
	v4l2_fh_init(&fh->fh, video_devdata(file));
	file->private_data = &fh->fh;
	v4l2_fh_add(&fh->fh);

	return 0;
}

static int fops_release(struct file *file)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;

	dprintk(2, "%s()\n", __func__);

	/* Shut device down on last close */
	if (atomic_cmpxchg(&fh->v4l_reading, 1, 0) == 1) {
		if (atomic_dec_return(&dev->v4l_reader_count) == 0) {
			/* stop mpeg capture then cancel buffers */
			hdcapm_core_stop_streaming(dev);
		}
	}

	v4l2_fh_del(&fh->fh);
	v4l2_fh_exit(&fh->fh);
	kfree(fh);

	return 0;
}

static ssize_t fops_read(struct file *file, char __user *buffer,
	size_t count, loff_t *pos)
{
	struct hdcapm_fh *fh = file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	struct hdcapm_buffer *ubuf = NULL;
	int ret = 0;
	int rem, cnt;
	u8 *p;

	kl_histogram_update(&dev->stats->v4l2_read_call_interval);

	if (*pos) {
		printk(KERN_ERR "%s() ESPIPE\n", __func__);
		return -ESPIPE;
	}

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&dev->v4l_reader_count) == 1) {
			hdcapm_core_start_streaming(dev);
		}
	}

	/* blocking wait for buffer */
	if ((file->f_flags & O_NONBLOCK) == 0) {
		if (wait_event_interruptible(dev->wait_read, hdcapm_buffer_peek_used(dev))) {
				printk(KERN_ERR "%s() ERESTARTSYS\n", __func__);
				//return -ERESTARTSYS;
				return -EINVAL;
		}
	}

	/* Pull the first buffer from the used list */
	ubuf = hdcapm_buffer_peek_used(dev);

	while ((count > 0) && ubuf) {

		/* set remaining bytes to copy */
		rem = ubuf->actual_size - ubuf->readpos;
		cnt = rem > count ? count : rem;

		p = ubuf->ptr + ubuf->readpos;

		dprintk(3, "%s() nr=%d count=%d cnt=%d rem=%d buf=%p buf->readpos=%d\n",
			__func__, ubuf->nr, (int)count, cnt, rem, ubuf, ubuf->readpos);

		if (copy_to_user(buffer, p, cnt)) {
			printk(KERN_ERR "%s() copy_to_user failed\n", __func__);
			if (!ret) {
				printk(KERN_ERR "%s() EFAULT\n", __func__);
				ret = -EFAULT;
			}
			goto err;
		}

		ubuf->readpos += cnt;
		count -= cnt;
		buffer += cnt;
		ret += cnt;

		if (ubuf->readpos > ubuf->actual_size)
			printk(KERN_ERR "read() pos > actual, huh?\n");

		if (ubuf->readpos == ubuf->actual_size) {

			/* finished with current buffer, take next buffer */

			/* Requeue the buffer on the free list */
			ubuf->readpos = 0;

			hdcapm_buffer_move_to_free(dev, ubuf);

			/* Dequeue next */
			if ((file->f_flags & O_NONBLOCK) == 0) {
				if (wait_event_interruptible(dev->wait_read, hdcapm_buffer_peek_used(dev))) {
					break;
				}
			}
			ubuf = hdcapm_buffer_peek_used(dev);
		}
	}
err:
	if (!ret && !ubuf)
		ret = -EAGAIN;

	return ret;
}

static unsigned int fops_poll(struct file *file, poll_table *wait)
{
	unsigned long req_events = poll_requested_events(wait);
	struct hdcapm_fh *fh = (struct hdcapm_fh *)file->private_data;
	struct hdcapm_dev *dev = fh->dev;
	unsigned int mask = v4l2_ctrl_poll(file, wait);

#if 0
	port->last_poll_msecs_diff = port->last_poll_msecs;
	port->last_poll_msecs = jiffies_to_msecs(jiffies);
	port->last_poll_msecs_diff = port->last_poll_msecs -
		port->last_poll_msecs_diff;

	saa7164_histogram_update(&port->poll_interval,
		port->last_poll_msecs_diff);
#endif

	if (!(req_events & (POLLIN | POLLRDNORM)))
		return mask;

	if (atomic_cmpxchg(&fh->v4l_reading, 0, 1) == 0) {
		if (atomic_inc_return(&dev->v4l_reader_count) == 1) {
			hdcapm_core_start_streaming(dev);
		}
	}

	/* Pull the first buffer from the used list */
	if (!list_empty(&dev->list_buf_used))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct v4l2_file_operations mpeg_fops =
{
	.owner          = THIS_MODULE,
	.open           = fops_open,
	.release        = fops_release,
	.read           = fops_read,
	.poll           = fops_poll,
	.unlocked_ioctl = video_ioctl2,
};

static struct video_device mpeg_template =
{
	.name          = "hdcapm",
	.fops          = &mpeg_fops,
	.ioctl_ops     = &mpeg_ioctl_ops,
	.minor         = -1,
};

int hdcapm_video_register(struct hdcapm_dev *dev)
{
	struct v4l2_ctrl_handler *hdl = &dev->ctrl_handler;
	int ret;

	dprintk(1, "%s()\n", __func__);

	/* Any video controls. */

	dev->v4l_device = video_device_alloc();
        if (dev->v4l_device == NULL)
                return -EINVAL;

	/* Configure the V4L2 device properties */
	*dev->v4l_device = mpeg_template;
	snprintf(dev->v4l_device->name, sizeof(dev->v4l_device->name), "%s %s (%s)", dev->name, "mpeg", dev->name);
	dev->v4l_device->v4l2_dev = &dev->v4l2_dev;
	dev->v4l_device->release = video_device_release;

	v4l2_ctrl_handler_init(hdl, 14);
	dev->v4l_device->ctrl_handler = hdl;

	v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_MPEG_AUDIO_MUTE, 0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_BRIGHTNESS, 0, 255, 1, 127);
        v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE, ENCODER_MIN_GOP_SIZE, ENCODER_MAX_GOP_SIZE, 1, ENCODER_DEF_GOP_SIZE);
        v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_MPEG_VIDEO_BITRATE, ENCODER_MIN_BITRATE, ENCODER_MAX_BITRATE, 100000, ENCODER_DEF_BITRATE);
        v4l2_ctrl_new_std(hdl, &ctrl_ops, V4L2_CID_MPEG_VIDEO_BITRATE_PEAK, ENCODER_MIN_BITRATE, ENCODER_MAX_BITRATE, 100000, ENCODER_DEF_BITRATE);

	v4l2_ctrl_new_std_menu(hdl, &ctrl_ops,
		V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
		V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
		V4L2_MPEG_VIDEO_BITRATE_MODE_VBR);

	v4l2_ctrl_new_std_menu(hdl, &ctrl_ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_5_1,
		0, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);

	v4l2_ctrl_new_std_menu(hdl, &ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
		V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		0, V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC);

	v4l2_ctrl_new_std_menu(hdl, &ctrl_ops,
		V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			~((1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
			  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
			  (1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)),
		V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);

	v4l2_ctrl_new_std_menu(hdl, &ctrl_ops,
		V4L2_CID_MPEG_STREAM_TYPE,
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS,
		~(1 << V4L2_MPEG_STREAM_TYPE_MPEG2_TS),
		V4L2_MPEG_STREAM_TYPE_MPEG2_TS);

	/* Establish all default control values. */
	v4l2_ctrl_handler_setup(hdl);

	video_set_drvdata(dev->v4l_device, dev);
	ret = video_register_device(dev->v4l_device, VFL_TYPE_GRABBER, -1);
	if (ret < 0) {
		goto fail1;
	}
	printk(KERN_INFO KBUILD_MODNAME ": registered device video%d [mpeg]\n", dev->v4l_device->num);

	ret = 0; /* Success */

fail1:
	return ret;
}

void hdcapm_video_unregister(struct hdcapm_dev *dev)
{
	dprintk(1, "%s()\n", __func__);

	if (dev->v4l_device) {
		if (dev->v4l_device->minor != -1)
			video_unregister_device(dev->v4l_device);
		else
			video_device_release(dev->v4l_device);

		dev->v4l_device = NULL;
	}
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
}


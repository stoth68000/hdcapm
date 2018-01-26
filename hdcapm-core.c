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
#include "mst3367-drv.h"

int hdcapm_debug = 0;
module_param_named(debug, hdcapm_debug, int, 0644);
MODULE_PARM_DESC(debug, "debug bitmask: 1) module");

int hdcapm_i2c_scan = 0;
module_param_named(i2c_scan, hdcapm_i2c_scan, int, 0644);
MODULE_PARM_DESC(i2c_scan, "Probe i2c bus for devices");

unsigned int thread_poll_interval = 500;
module_param(thread_poll_interval, int, 0644);
MODULE_PARM_DESC(thread_poll_interval, "have the kernel thread poll every N ms (def:500)");

unsigned int buffer_count = 128;
module_param(buffer_count, int, 0644);
MODULE_PARM_DESC(buffer_count, "# of buffers the driver should queue");

#define XFERBUF_SIZE (65536 * 4)
unsigned int buffer_size = XFERBUF_SIZE;
module_param(buffer_size, int, 0644);
MODULE_PARM_DESC(buffer_size, "size of each buffer in bytes");

static DEFINE_MUTEX(devlist);
LIST_HEAD(hdcapm_devlist);
static unsigned int devlist_count;

/* Copy an on-stack transfer buffer into a device context.
 * Do this before we pass it to the USB subsystem, else ARM complains (once) in the
 * USB controller about the location of the transfer.
 * TODO: Review usage and optimize of the calls so that not all transfers need to be on stack.
 */
int hdcapm_core_ep_send(struct hdcapm_dev *dev, int endpoint, u8 *buf, u32 len, u32 timeout)
{
	int writelength;

	if (len > XFERBUF_SIZE) {
		printk(KERN_ERR "%s() buffer of %d bytes too large for transfer\n", __func__, len);
		return -1;
	}

	memcpy(dev->xferbuf, buf, len);
	dev->xferbuf_len = len;

	/* Flush this to EP4 via a bulk write. */
	return usb_bulk_msg(dev->udev, usb_sndbulkpipe(dev->udev, endpoint), dev->xferbuf, dev->xferbuf_len, &writelength, timeout);
}

/* Copy a transfer buffer from the device context back to an onstack location.
 * TODO: Review usage and optimize of the calls so that not all transfers need to be on stack.
 */
int hdcapm_core_ep_recv(struct hdcapm_dev *dev, int endpoint, u8 *buf, u32 len, u32 *actual, u32 timeout)
{
	int ret;

	BUG_ON(len > XFERBUF_SIZE);

	/* Bulk read */
	ret = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, endpoint), dev->xferbuf, len, &dev->xferbuf_len, timeout);

	memcpy(buf, dev->xferbuf, dev->xferbuf_len);
	*actual = dev->xferbuf_len;

	return ret;
}

int hdcapm_mem_write32(struct hdcapm_dev *dev, u32 addr, u32 val)
{
	/* EP4 Host -> 02 01 04 00 01 C8 0B 00 01 C8 0B 00 00 00 00 00 */
	u8 tx[] = {
		0x02,
		0x01, /* Write */
		0x04,
		0x00,
		addr, /* This is really a fill function? */
		addr >>  8,
		addr >> 16,
		addr >> 24,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
		val,
		val >>  8,
		val >> 16,
		val >> 24,
	};

	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, val);

	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	return 0;
}

/* Read a single DWORD from the USB device memory. */
int hdcapm_mem_read32(struct hdcapm_dev *dev, u32 addr, u32 *val)
{
	int len;
	u8 rx[4];

	/* Read bytes between to addresses
	 * EP4 Host -> 02 00 04 00 01 C8 0B 00 01 C8 0B 00
	 * EP3 Host <- 00 00 00 00
	 */
	u8 tx[] = {
		0x02,
		0x00, /* Read */
		0x04,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
	};

	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	/* Read 4 bytes from EP 3. */
	/* TODO: shouldn;t the buffer length be 4? */
	if (hdcapm_core_ep_recv(dev, PIPE_EP3, &rx[0], sizeof(rx), &len, 500) < 0) {
		return -1;
	}

	*val = rx[0] | (rx[1] << 8) | (rx[2] << 16) | (rx[3] << 24);
	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, *val);

	return 0;
}


/* Write a series of DMA DWORDS from the USB device memory. */
int hdcapm_dmawrite32(struct hdcapm_dev *dev, u32 addr, const u32 *arr, u32 entries)
{
	int len;
	u8 rx;

	/* EP4 Host -> 09 01 08 00 00 00 00 00 4E 63 05 00 00 20 00 00 */
	u8 tx[] = {
		0x09,
		0x01, /* Write */
		0x08,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
		entries,
		entries >>  8,
		entries >> 16,
		entries >> 24,
	};

	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, entries);

	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	/* Read 1 byte1 from EP 3. */
	if (hdcapm_core_ep_recv(dev, PIPE_EP3, &rx, sizeof(rx), &len, 500) < 0) {
		return -1;
	}

	if (rx != 0) {
		return -1;
	}

	/* Flush the buffer to device */
	if (hdcapm_core_ep_send(dev, PIPE_EP2, (u8 *)arr, entries * sizeof(u32), 5000) < 0) {
		return -1;
	}

	return 0;
}

/* Read a series of DMA DWORDS from the USB device memory. */
int hdcapm_dmaread32(struct hdcapm_dev *dev, u32 addr, u32 *arr, u32 entries)
{
	int len;
	u8 rx;

	/* EP4 Host -> 09 00 08 00 00 00 00 00 00 C8 05 00 00 04 00 00 */
	u8 tx[] = {
		0x09,
		0x00, /* Read */
		0x08,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
		entries,
		entries >>  8,
		entries >> 16,
		entries >> 24,
	};

	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, entries);

	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	/* Read 1 byte1 from EP 3. */
	if (hdcapm_core_ep_recv(dev, PIPE_EP3, &rx, sizeof(rx), &len, 500) < 0) {
		return -1;
	}

	if (rx != 0) {
		return -1;
	}

	/* Flush the buffer to device */
	if (hdcapm_core_ep_recv(dev, PIPE_EP1, (u8 *)arr, entries * sizeof(u32), &len, 5000) < 0) {
		return -1;
	}

	return 0;
}

/* Write a DWORD to a USB device register. */
int hdcapm_write32(struct hdcapm_dev *dev, u32 addr, u32 val)
{
	/* EP4 Host -> 01 01 01 00 04 05 00 00 55 00 00 00 */
	u8 tx[] = {
		0x01,
		0x01, /* Write */
		0x01,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
		val,
		val >>  8,
		val >> 16,
		val >> 24,
	};

	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, val);

	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	return 0;
}

/* Read a DWORD from a USB device register. */
int hdcapm_read32(struct hdcapm_dev *dev, u32 addr, u32 *val)
{
	int len;
	u8 rx[4];

	/* EP4 Host -> 01 00 01 00 00 05 00 00 */
	u8 tx[] = {
		0x01,
		0x00, /* Read */
		0x01,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
	};

	/* Flush this to EP4 via a write. */
	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		return -1;
	}

	/* Read 4 bytes from EP 3. */
	if (hdcapm_core_ep_recv(dev, PIPE_EP3, &rx[0], sizeof(rx), &len, 500) < 0) {
		return -1;
	}

//	dprintk(1, "%02x %02x %02x %02x\n", rx[0], rx[1], rx[2], rx[3]);
	*val = rx[0] | (rx[1] << 8) | (rx[2] << 16) | (rx[3] << 24);

	dprintk(2, "%s(0x%08x, 0x%08x)\n", __func__, addr, *val);

	return 0;
}

/* Read (bulk) a number of DWORDS from device registers and endian convert if requested. */
int hdcapm_read32_array(struct hdcapm_dev *dev, u32 addr, u32 wordcount, u32 *arr, int le_to_cpu)
{
	int len, i, j;
	int readlenbytes = wordcount * sizeof(u32);
	u8 *rx;

	/* EP4 Host -> 01 00 07 00 B0 06 00 00 */
	u8 tx[] = {
		0x01,
		0x00, /* Read */
		wordcount,
		0x00,
		addr,
		addr >>  8,
		addr >> 16,
		addr >> 24,
	};

	rx = kzalloc(readlenbytes, GFP_KERNEL);
	if (!rx)
		return -ENOMEM;

	/* Flush this to EP4 via a write. */
	if (hdcapm_core_ep_send(dev, PIPE_EP4, &tx[0], sizeof(tx), 500) < 0) {
		kfree(rx);
		return -1;
	}

	/* Read 4 bytes from EP 3. */
	if (hdcapm_core_ep_recv(dev, PIPE_EP3, rx, readlenbytes, &len, 500) < 0) {
		kfree(rx);
		return -1;
	}

	dprintk(2, "%s(0x%08x) =\n", __func__, addr);
	for (i = 0, j = 0; i < len; i += 4, j++) {

		*(arr + j) = rx[i + 0] | (rx[i + 1] << 8) | (rx[i + 2] << 16) | (rx[i + 3] << 24);

		if (le_to_cpu)
			*(arr + j) = le32_to_cpu(*(arr + j));
	}

	kfree(rx);
	return 0;
}

/* Set one or more bits high int a USB device register. */
void hdcapm_set32(struct hdcapm_dev *dev, u32 addr, u32 mask)
{
	u32 val;
	hdcapm_read32(dev, addr, &val);
	val |= mask;
	hdcapm_write32(dev, addr, val);
}

/* Set one or more bits low int a USB device register. */
void hdcapm_clr32(struct hdcapm_dev *dev, u32 addr, u32 mask)
{
	u32 val;
	hdcapm_read32(dev, addr, &val);
	val &= ~mask;
	hdcapm_write32(dev, addr, val);
}

int hdcapm_core_stop_streaming(struct hdcapm_dev *dev)
{
	dev->state = STATE_STOP;

	return 0; /* Success */
}

int hdcapm_core_start_streaming(struct hdcapm_dev *dev)
{
	dev->state = STATE_START;

	return 0; /* Success */
}

/* Worker thread to poll the HDMI receiver, and run the USB
 * transfer mechanism when the encoder starts.
 */
static int hdcapm_thread_function(void *data)
{
	struct hdcapm_dev *dev = data;
	struct v4l2_dv_timings timings;
	int ret;

	dev->thread_active = 1;
	dprintk(1, "%s() Started\n", __func__);

	set_freezable();

	while (1) {
		msleep_interruptible(thread_poll_interval);

		if (kthread_should_stop())
			break;

		try_to_freeze();

		if (dev->state == STATE_STOPPED) {
			ret = v4l2_subdev_call(dev->sd, video, query_dv_timings, &timings);
			if (ret == 0) {
			}
		}

		if (dev->state == STATE_START) {
			/* This is a blocking func. */
			hdcapm_compressor_run(dev);
		}
	}

	dev->thread_active = 0;
	return 0;
}

static void hdcapm_usb_v4l2_release(struct v4l2_device *v4l2_dev)
{
	struct hdcapm_dev *dev =
		container_of(v4l2_dev, struct hdcapm_dev, v4l2_dev);

	v4l2_device_unregister_subdev(dev->sd);

	// TODO: Do I need this?
	//v4l2_ctrl_handler_free(&dev->v4l2_ctrl_hdl);

	v4l2_device_unregister(&dev->v4l2_dev);
}

#if TIMER_EVAL
static void _ktimer_event(unsigned long ptr)
{
	struct hdcapm_dev *dev = (struct hdcapm_dev *)ptr;

	kl_histogram_update(&dev->stats->timer_callbacks);

	mod_timer(&dev->ktimer, jiffies + (HZ / 1000));
}

static enum hrtimer_restart _hrtimer_event(struct hrtimer *timer)
{
	struct hdcapm_dev *dev = container_of(timer, struct hdcapm_dev, hrtimer);
	unsigned long missed;

	kl_histogram_update(&dev->stats->hrtimer_callbacks);
#if 0
	missed = hrtimer_forward_now(&dev->hrtimer, 4000000);
#endif

	return HRTIMER_RESTART;
}
#endif

/* sub-device events are pushed with v4l2_subdev_notify() and v4l2_subdev_notify_enent().
 * They eventually make their way here.
 * The bridge then forwards those events via v4l2_event_queue() to the v4l2_device,
 * and so eventually they end up in userspace.
 */
static void hdcapm_notify(struct v4l2_subdev *sd, unsigned int notification, void *arg)
{
	struct hdcapm_dev *dev = container_of(sd->v4l2_dev, struct hdcapm_dev, v4l2_dev);
	struct mst3367_source_detect *mst3367;

	switch (notification) {
	case MST3367_SOURCE_DETECT:
		mst3367 = (struct mst3367_source_detect *)arg;
#if 0
		pr_info(KBUILD_MODNAME ": has signal = %d\n", mst3367->present);
#endif
		break;
	case V4L2_DEVICE_NOTIFY_EVENT:
		/*
		 * Userspace can monitor for these with:
		 * v4l2-ctl -d /dev/video2 --wait-for-event=source_change=0
		 */
		v4l2_event_queue(dev->v4l_device, arg);
		break;
	default:
		pr_err(KBUILD_MODNAME ": unhandled notification = 0x%x\n", notification);
		break;
	}
}

static int hdcapm_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct hdcapm_dev *dev;
	struct hdcapm_buffer *buf;
	struct usb_device *udev;
	struct i2c_board_info mst3367_info;
	struct mst3367_platform_data mst3367_pdata;
	int ret, i;

	udev = interface_to_usbdev(interface);

	if (interface->altsetting->desc.bInterfaceNumber != 0) {
		ret = -ENODEV;
		goto fail0;
	}

	dprintk(1, "%s() vendor id 0x%x device id 0x%x\n", __func__,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct));

	/* Ensure the bus speed is 480Mbps. */
	if (udev->speed != USB_SPEED_HIGH) {
		pr_err(KBUILD_MODNAME ": Device initialization failed.\n");
		pr_err(KBUILD_MODNAME ": Device must be connected to a USB 2.0 port (480Mbps).\n");
		ret = -ENODEV;
		goto fail0;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		pr_err(KBUILD_MODNAME ": failed to allocate memory\n");
		ret = -ENOMEM;
		goto fail1;
	}

	dev->xferbuf = kzalloc(XFERBUF_SIZE, GFP_KERNEL);
	if (dev->xferbuf == NULL) {
		pr_err(KBUILD_MODNAME ": failed to allocate memory for usb transfer buffer\n");
		ret = -ENOMEM;
		goto fail2;
	}

	dev->stats = kzalloc(sizeof(struct hdcapm_statistics), GFP_KERNEL);
	if (dev->stats == NULL) {
		pr_err(KBUILD_MODNAME ": failed to allocate memory for stats\n");
		ret = -ENOMEM;
		goto fail2;
	}
	hdcapm_core_statistics_reset(dev);

	strlcpy(dev->name, "Startech HDCAPM Encoder", sizeof(dev->name));
	dev->state = STATE_STOPPED;
	dev->udev = udev;

	mutex_init(&dev->lock);
	mutex_init(&dev->dmaqueue_lock);
	INIT_LIST_HEAD(&dev->list_buf_free);
	INIT_LIST_HEAD(&dev->list_buf_used);
	init_waitqueue_head(&dev->wait_read);
	usb_set_intfdata(interface, dev);

	/* Register the I2C buses. */
	if (hdcapm_i2c_register(dev, &dev->i2cbus[0], 0) < 0) {
		pr_err(KBUILD_MODNAME ": failed to register i2cbus 0\n");
		ret = -EINVAL;
		goto fail2_1;
	}

	/* We're not using bus#1, it has the eeprom on it. Remove this or leave
	 * for future developers with future products?
	 */
	if (hdcapm_i2c_register(dev, &dev->i2cbus[1], 1) < 0) {
		pr_err(KBUILD_MODNAME ": failed to register i2cbus 1\n");
		ret = -EINVAL;
		goto fail3;
	}

#if ONETIME_FW_LOAD
	/* Register the compression codec (it does both audio and video). */
	if (hdcapm_compressor_register(dev) < 0) {
		pr_err(KBUILD_MODNAME ": failed to register compressor\n");
		ret = -EINVAL;
		goto fail4;
	}
#else
	hdcapm_compressor_init_gpios(dev);
#endif

	/* Attach HDMI receiver */
	ret = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (ret < 0) {
		pr_err(KBUILD_MODNAME ": v4l2_device_register failed\n");
		ret = -EINVAL;
		goto fail5;
	}

	dev->v4l2_dev.release = hdcapm_usb_v4l2_release;
	dev->v4l2_dev.notify = hdcapm_notify;

	/* Configure a sub-device attachment for the HDMI receiver. */
	memset(&mst3367_pdata, 0, sizeof(mst3367_pdata));
	memset(&mst3367_info, 0, sizeof(struct i2c_board_info));
	strlcpy(mst3367_info.type, "mst3367", I2C_NAME_SIZE);

	mst3367_pdata.some_value = 0x0;
	mst3367_info.addr = 0x9c >> 1;
	mst3367_info.platform_data = &mst3367_pdata;

	dev->sd = v4l2_i2c_new_subdev_board(&dev->v4l2_dev, &dev->i2cbus[0].i2c_adap, &mst3367_info, NULL);
        if (!dev->sd) {
		pr_err(KBUILD_MODNAME ": failed to find or load a driver for the MST3367\n");
		ret = -EINVAL;
		goto fail6;
	}

	/* Power on the HDMI receiver, assuming it needs it. */
	v4l2_subdev_call(dev->sd, core, s_power, 1);

	/* We need some buffers to hold user payload. */
	for (i = 0; i < buffer_count; i++) {
		buf = hdcapm_buffer_alloc(dev, i, buffer_size);
		if (!buf) {
			pr_err(KBUILD_MODNAME ": failed to allocate a user buffer\n");
			ret = -ENOMEM;
			goto fail8;
		}

		mutex_lock(&dev->dmaqueue_lock);
		list_add_tail(&buf->list, &dev->list_buf_free);
		mutex_unlock(&dev->dmaqueue_lock);
	}

	/* Formally register the V4L2 interfaces. */
	if (hdcapm_video_register(dev) < 0) {
		pr_err(KBUILD_MODNAME ": failed to register video device\n");
		ret = -EINVAL;
		goto fail8;
	}

	/* Bring up a kernel thread to manage the HDMI frontend and run the data pump. */
	dev->kthread = kthread_run(hdcapm_thread_function, dev, "hdcapm hdmi");
	if (!dev->kthread) {
		pr_err(KBUILD_MODNAME ": failed to create hdmi kernel thread\n");
		ret = -EINVAL;
		goto fail9;
        }

	/* Finish the rest of the hardware configuration. */
	mutex_lock(&devlist);
	list_add_tail(&dev->devlist, &hdcapm_devlist);
	devlist_count++;
	mutex_unlock(&devlist);

	pr_info(KBUILD_MODNAME ": Registered device '%s'\n", dev->name);

#if TIMER_EVAL
	setup_timer(&dev->ktimer, _ktimer_event, (unsigned long)dev);
	mod_timer(&dev->ktimer, jiffies + (HZ / 1000));

	hrtimer_init(&dev->hrtimer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	dev->hrtimer.function = _hrtimer_event;
#if 0
	hrtimer_start(&dev->hrtimer, 4000000, HRTIMER_MODE_ABS);
#endif
#endif /* TIMER_EVAL */

	return 0; /* Success */

fail9:
	hdcapm_video_unregister(dev);
fail8:
	/* Put all the buffers back on the free list, then dealloc them. */
	hdcapm_buffers_move_all(dev, &dev->list_buf_free, &dev->list_buf_used);
	hdcapm_buffers_free_all(dev, &dev->list_buf_free);
fail6:
	v4l2_device_unregister(&dev->v4l2_dev);
fail5:
#if ONETIME_FW_LOAD
	hdcapm_compressor_unregister(dev);
fail4:
#endif
	hdcapm_i2c_unregister(dev, &dev->i2cbus[1]);
fail3:
	hdcapm_i2c_unregister(dev, &dev->i2cbus[0]);
fail2_1:
	kfree(dev->stats);
fail2:
	kfree(dev->xferbuf);
fail1:
	kfree(dev);
fail0:
	return ret;
}

static void hdcapm_usb_disconnect(struct usb_interface *interface)
{
	struct hdcapm_dev *dev = usb_get_intfdata(interface);
	int i;

	dprintk(1, "%s()\n", __func__);

#if TIMER_EVAL
	del_timer_sync(&dev->ktimer);
	hrtimer_cancel(&dev->hrtimer);
#endif

	if (dev->kthread) {
		kthread_stop(dev->kthread);
		dev->kthread = NULL;

		i = 0;
		while (dev->thread_active) {
			msleep(500);
			if (i++ > 24)
				break;
		}
	}

	hdcapm_video_unregister(dev);

#if ONETIME_FW_LOAD
	/* Unregister the compression codec. */
	hdcapm_compressor_unregister(dev);
#endif

	/* Unregister any I2C buses. */
	hdcapm_i2c_unregister(dev, &dev->i2cbus[1]);
	hdcapm_i2c_unregister(dev, &dev->i2cbus[0]);

	/* Put all the buffers back on the free list, the dealloc them. */
	hdcapm_buffers_move_all(dev, &dev->list_buf_free, &dev->list_buf_used);
	hdcapm_buffers_free_all(dev, &dev->list_buf_free);

	kfree(dev->xferbuf);
	kfree(dev->stats);

	mutex_lock(&devlist);
	list_del(&dev->devlist);
	mutex_unlock(&devlist);
}

static int hdcapm_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct hdcapm_dev *dev = usb_get_intfdata(interface);
	if (!dev)
		return 0;

	/* TODO: Power off the HDMI receiver? */

	pr_info(KBUILD_MODNAME ": USB is suspend\n");

	return 0;
}

static int hdcapm_resume(struct usb_interface *interface)
{
	struct hdcapm_dev *dev = usb_get_intfdata(interface);
	if (!dev)
		return 0;

	/* TODO: Power on the HDMI receiver? */

	return 0;
}

struct usb_device_id hdcapm_usb_id_table[] = {
	{ USB_DEVICE(0x1164, 0x75a7), .driver_info = HDCAPM_CARD_REV1 },
	{ /* -- end -- */ },
};
MODULE_DEVICE_TABLE(usb, hdcapm_usb_id_table);

static struct usb_driver hdcapm_usb_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= hdcapm_usb_probe,
	.disconnect	= hdcapm_usb_disconnect,
	.id_table	= hdcapm_usb_id_table,
	.suspend	= hdcapm_suspend,
	.resume		= hdcapm_resume,
	.reset_resume	= hdcapm_resume,
};

static int __init hdcapm_init(void)
{
	int ret;

	if (hdcapm_debug & 1)
		pr_info(KBUILD_MODNAME ": Debugging is enabled\n");

	pr_info(KBUILD_MODNAME ": driver loaded\n");

	ret = usb_register(&hdcapm_usb_driver);
	if (ret)
		pr_err(KBUILD_MODNAME ": usb_register failed, error = %d\n", ret);

	return ret;
}

static void __exit hdcapm_exit(void)
{
	usb_deregister(&hdcapm_usb_driver);

	pr_info(KBUILD_MODNAME ": driver unloaded\n");
}

module_init(hdcapm_init);
module_exit(hdcapm_exit);

MODULE_DESCRIPTION("Driver for StarTech USB2HDCAPM USB capture product");
MODULE_AUTHOR("Steven Toth <stoth@kernellabs.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.1");

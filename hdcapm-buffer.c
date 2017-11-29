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

struct hdcapm_buffer *hdcapm_buffer_alloc(struct hdcapm_dev *dev, u32 nr, u32 maxsize)
{
	struct hdcapm_buffer *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->nr = nr;
	buf->dev = dev;
	buf->maxsize = maxsize;
	buf->ptr = kzalloc(maxsize, GFP_KERNEL);
	if (!buf->ptr) {
		kfree(buf);
		return NULL;
	}

	return buf;
}

void hdcapm_buffer_free(struct hdcapm_buffer *buf)
{
	if (buf->ptr) {
		kfree(buf->ptr);
		buf->ptr = NULL;
	}

	if (buf->urb) {
		usb_free_urb(buf->urb);
		buf->urb = NULL;
	}

	kfree(buf);
}

/* Helper macros for managing the device lists.
 * Caller is responsible for holding the mutex.
 */
void hdcapm_buffers_move_all(struct hdcapm_dev *dev, struct list_head *to, struct list_head *from)
{
	struct hdcapm_buffer *buf;

	mutex_lock(&dev->dmaqueue_lock);
	while (!list_empty(from)) {
                buf = list_first_entry(from, struct hdcapm_buffer, list);
		if (buf)
			list_move_tail(&buf->list, to);
        }
	mutex_unlock(&dev->dmaqueue_lock);
}

/* Helper macros for managing the device lists.
 * Caller is responsible for holding the mutex.
 */
void hdcapm_buffers_free_all(struct hdcapm_dev *dev, struct list_head *head)
{
	struct hdcapm_buffer *buf;

	mutex_lock(&dev->dmaqueue_lock);
	while (!list_empty(head)) {
                buf = list_first_entry(head, struct hdcapm_buffer, list);
		if (buf) {
			list_del(&buf->list);
			hdcapm_buffer_free(buf);
		}
        }
	mutex_unlock(&dev->dmaqueue_lock);
}

/* Helper macros for managing the device lists.
 * We WILL take the mutex in this func.
 * Return a reference to the top most used buffer, we're going to
 * read some or all of it (probably). Don't delete it from the list.
 */
struct hdcapm_buffer *hdcapm_buffer_peek_used(struct hdcapm_dev *dev)
{
	struct hdcapm_buffer *buf = NULL;

	mutex_lock(&dev->dmaqueue_lock);
	if (!list_empty(&dev->list_buf_used)) {
		buf = list_first_entry(&dev->list_buf_used, struct hdcapm_buffer, list);
	}
	mutex_unlock(&dev->dmaqueue_lock);

	dprintk(3, "%s() returns %p\n", __func__, buf);

	return buf;
}

static struct hdcapm_buffer *hdcapm_buffer_next_used(struct hdcapm_dev *dev)
{
	struct hdcapm_buffer *buf = NULL;

	mutex_lock(&dev->dmaqueue_lock);
	if (!list_empty(&dev->list_buf_used)) {
		buf = list_first_entry(&dev->list_buf_used, struct hdcapm_buffer, list);
		list_del(&buf->list);
	}
	mutex_unlock(&dev->dmaqueue_lock);

	dprintk(3, "%s() returns %p\n", __func__, buf);

	return buf;
}

/* Pull the top buffer from the free list, but don't specifically remove it from the
 * list. If no buffer exists, steal one from the used list.
 * We WILL take the mutex in this func.
 * Return the buffer at the top of the free list, delete the list node.
 * We're probably going to fill it and move it to the used list.
 * IF no free buffers exist, steal one from the used list and flag an internal
 * data loss statistic.
 */
struct hdcapm_buffer *hdcapm_buffer_next_free(struct hdcapm_dev *dev)
{
	struct hdcapm_buffer *buf = NULL;

	mutex_lock(&dev->dmaqueue_lock);
	if (!list_empty(&dev->list_buf_free)) {
		buf = list_first_entry(&dev->list_buf_free, struct hdcapm_buffer, list);
		list_del(&buf->list);
	}
	mutex_unlock(&dev->dmaqueue_lock);

	if (!buf) {
		printk(KERN_WARNING "%s() No empty buffers, data loss will occur. Increase param buffer_count.\n", __func__);
		buf = hdcapm_buffer_next_used(dev);
		if (!buf) {
			printk(KERN_ERR "%s() Driver madness, no free or empty buffers.\n", __func__);
		}
		dev->stats->buffer_overrun++;
	}

	dprintk(3, "%s() returns %p\n", __func__, buf);

	return buf;
}

static void hdcapm_buffer_add_to_list(struct hdcapm_dev *dev, struct hdcapm_buffer *buf, struct list_head *list)
{
	mutex_lock(&dev->dmaqueue_lock);
	list_add_tail(&buf->list, list);
	mutex_unlock(&dev->dmaqueue_lock);
}

__inline__ void hdcapm_buffer_add_to_free(struct hdcapm_dev *dev, struct hdcapm_buffer *buf)
{
	hdcapm_buffer_add_to_list(dev, buf, &dev->list_buf_free);
}

__inline__ void hdcapm_buffer_add_to_used(struct hdcapm_dev *dev, struct hdcapm_buffer *buf)
{
	hdcapm_buffer_add_to_list(dev, buf, &dev->list_buf_used);
}

static __inline__ void hdcapm_buffer_move(struct hdcapm_dev *dev, struct hdcapm_buffer *buf, struct list_head *list)
{
	mutex_lock(&dev->dmaqueue_lock);
	list_move_tail(&buf->list, list);
	mutex_unlock(&dev->dmaqueue_lock);
}

/* Helper macros for moving a buffer to the free list.
 * We WILL take the mutex in this func.
 */
void hdcapm_buffer_move_to_free(struct hdcapm_dev *dev, struct hdcapm_buffer *buf)
{
	hdcapm_buffer_move(dev, buf, &dev->list_buf_free);
}

/* Helper macros for moving a buffer to the used list.
 * We WILL take the mutex in this func.
 */
void hdcapm_buffer_move_to_used(struct hdcapm_dev *dev, struct hdcapm_buffer *buf)
{
	hdcapm_buffer_move(dev, buf, &dev->list_buf_used);
}

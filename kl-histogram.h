/**
 * @file	kl-histogram.h
 * @author	Steven Toth <stoth@kernellabs.com>
 * @copyright	Copyright (c) 2010-2017 Kernel Labs Inc. All Rights Reserved.
 * @brief	TODO - Brief description goes here.
 */

#ifndef KL_HISTOGRAM_H
#define KL_HISTOGRAM_H

#ifdef KL_USERSPACE
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#else
/* Unix Kernel */
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif

#include <media/v4l2-device.h>

#define HAVE_RRD_H 0
#endif

#if HAVE_RRD_H
#include <rrd.h>
#include <rrd_client.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Example Usage within your driver ...
 */
/*
struct driver_dev {
	struct kl_histogram irq_interval;
}

int driver_init(...) {
	kl_histogram_reset(&dev->irq_interval, "irq intervals", KL_BUCKET_VIDEO);
}

void some_func() {
	kl_histogram_update(&dev->irq_interval);
}
*/

/**
 * @brief	TODO - Brief description goes here.
 */
struct kl_histogram_bucket
{
	u32 val;
	u32 count;
#ifdef KL_USERSPACE
	struct timeval update_time;
#else
	u64 update_time;
#endif
};

/**
 * @brief	TODO - Brief description goes here.
 */
#define KL_HISTOGRAM_LOOKUP_MAX 50

/**
 * @brief	TODO - Brief description goes here.
 */
struct kl_histogram
{
	char name[32];
#define KL_HISTOGRAM_COUNTERS (KL_HISTOGRAM_LOOKUP_MAX + 14)
	struct kl_histogram_bucket counter[KL_HISTOGRAM_COUNTERS];

#ifdef KL_USERSPACE
	struct timeval time_prev, time_curr;
#else
	u64 msecs_prev, msecs_curr;
#endif

	u64 cumulative_msecs;
#if HAVE_RRD_H
	int rrd_required;
	int rrd_initialized;
	char *rrd_filename_db;
	char *rrd_chartTitle;
#endif
};

/**
 * @brief	TODO - Brief discription goes here. 
 */
enum kl_histogram_bucket_t {
	KL_BUCKET_VIDEO,
};

/**
 * @brief	Call reset during your device init section to prepare the structure.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 * @param[in]	const char *name - Brief description goes here.
 * @param[in]	enum kl_histogram_bucket_t - Brief description goes here.
 */
void kl_histogram_reset(struct kl_histogram *hg, const char *name, enum kl_histogram_bucket_t);

/**
 * @brief	Call this every time your important timing event occurs.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_update(struct kl_histogram *hg);

/**
 * @brief	Call this every time your important timing event occurs.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 * @param[in]	u32 val - Brief description goes here.
 */
void kl_histogram_update_with_value(struct kl_histogram *hg, u32 val);

/**
 * @brief	Call this every time your sample window begins, for measuring time windows.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_sample_begin(struct kl_histogram *hg);

/**
 * @brief	Call this every time your sample window ends, for measuring time windows.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_sample_complete(struct kl_histogram *hg);

/**
 * @brief	Zeroize and start a new cumulative measurement, where we intent to measure multiple
 *              small pieces of processing, over a larger amount of time, but report the total
 *              processing once into the histogram. A perfect use case is measuring slice decode
 *              time for a 60 frame gop, where frame arrival is sporadic, but we only care about
 *              the decompression time, and not the latency between frames.
 * @param[in]	struct kl_histogram *hg - object.
 */
void kl_histogram_cumulative_initialize(struct kl_histogram *hg);

/**
 * @brief	Begin measurement.
 * @param[in]	struct kl_histogram *hg - object.
 */
void kl_histogram_cumulative_begin(struct kl_histogram *hg);

/**
 * @brief	End measurement time. Call kl_histogram_cumulative_begin() again at a later time
 *              to add additional processing time to the cumulative measurement. When you've finished
 *              collecting all cumulative measurements for you use case, call kl_histogram_cumulative_finalize()
 *              to flush the cumulative measurement into the histogram.
 *              Don't forget to call kl_histogram_cumulative_initialize() if you plan to reuse the object for a
 *              new cumulative measurement, else old data will not be flushed.
 * @param[in]	struct kl_histogram *hg - object.
 */
void kl_histogram_cumulative_complete(struct kl_histogram *hg);

/**
 * @brief	Once all measurements are complete, call this to flush the final measurement into the histogram.
 * @param[in]	struct kl_histogram *hg - object.
 */
void kl_histogram_cumulative_finalize(struct kl_histogram *hg);

/**
 * @brief	Allow the caller to erase the statistics.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_zeroize(struct kl_histogram *hg);

#ifdef KL_USERSPACE
/**
 * @brief	Call this to have your counters dumped for dynamic inspection, to a file descriptor.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 * @param[in]	int fd - Brief description goes here.
 */
void kl_histogram_dprintf(struct kl_histogram *hg, int fd);

/**
 * @brief	Call this to have your counters dumped for dynamic inspection, to a file descriptor.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_printf(struct kl_histogram *hg);

#if HAVE_RRD_H
/**
 * @brief		Ensure values are flushed to a rrdchart.
 *              This is only really useful for histograms that
 *              Measure some kind of guage on a regular basis,
 *              such as GOP compression time, and want starts for it.
 *              Also useful for plotting repetitive single values over time.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 * @param[in]	const char * - Output RRD database name.
 */
int kl_histogram_rrd_gauge_enable(struct kl_histogram *hg, const char *rrd_filename, const char *chartTitle);
#endif /* HAVE_RRD_H */

#else
/**
 * @brief	From procfs, call this to have your counters dumped for dynamic inspection.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 * @param[in]	struct seq_file *m - Brief description goes here.
 */
void kl_histogram_print_procfs(struct kl_histogram *hg, struct seq_file *m);

/**
 * @brief	Dump the counters to syslog.
 * @param[in]	struct kl_histogram *hg - Brief description goes here.
 */
void kl_histogram_print_syslog(struct kl_histogram *hg);

/**
 * @brief	Call this to have your counters dumped for dynamic inspection, via a v4l2_device v4l2_info log.
 *              This is useful when you want to expose histogram stats via v4l2-ctl --log-status.
 * @param[in]	kl_histogram_print_subdev *sd - A V4L2 subdevice.
 * @param[in]	struct kl_histogram *hg - A previously initialized histogram.
 */
void kl_histogram_print_v4l2_device(struct v4l2_device *dev, struct kl_histogram *hg);

#endif /* KL_USERSPACE */

#ifdef __cplusplus
};
#endif

#endif // KL_HISTOGRAM_H

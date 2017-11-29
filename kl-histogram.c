/*
 * Copyright 2010-2017 - Kernel Labs Inc. www.kernellabs.com.
 */

#ifdef KL_USERSPACE
#include <libklmonitoring/kl-histogram.h>
#else

#include "kl-histogram.h"
#include <media/v4l2-device.h>
#endif

#ifdef KL_USERSPACE
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#include <getopt.h>

extern int dprintf (int __fd, const char *__restrict __fmt, ...)
     __attribute__ ((__format__ (__printf__, 2, 3)));

static int timeval_to_ms(struct timeval *tv)
{
	return (tv->tv_sec * 1000) + (tv->tv_usec / 1000);
}

static int timeval_subtract(struct timeval *result, struct timeval *x, struct timeval *y)
{
     /* Perform the carry for the later subtraction by updating y. */
     if (x->tv_usec < y->tv_usec)
     {
         int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
         y->tv_usec -= 1000000 * nsec;
         y->tv_sec += nsec;
     }
     if (x->tv_usec - y->tv_usec > 1000000)
     {
         int nsec = (x->tv_usec - y->tv_usec) / 1000000;
         y->tv_usec += 1000000 * nsec;
         y->tv_sec -= nsec;
     }

     /* Compute the time remaining to wait. tv_usec is certainly positive. */
     result->tv_sec = x->tv_sec - y->tv_sec;
     result->tv_usec = x->tv_usec - y->tv_usec;

     /* Return 1 if result is negative. */
     return x->tv_sec < y->tv_sec;
}
#endif

void kl_histogram_zeroize(struct kl_histogram *hg)
{
	int i;

	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {
		hg->counter[i].count = 0;
#ifdef KL_USERSPACE
		hg->counter[i].update_time.tv_sec = 0;
		hg->counter[i].update_time.tv_usec = 0;
#else
		hg->counter[i].update_time = 0;
#endif
	}
}

#define TABLE_HEADER_VALUES \
/*  0 -  9ms */ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, \
/* 10 - 19ms */ 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, \
/* 20 - 29ms */ 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, \
/* 30 - 39ms */ 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, \
/* 40 - 49ms */ 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, \
/*      50ms */ 50

/* First 0-900ms then 1, 5, 15, 60 seconds
 * If you are building a new table, the golden rule is that values in positions
 * 0 to 50 must containt 0..50 milliseconds.
 * Entries of the table 51+ can be any values you like, and the update
 * mechanisms will perform an optimized lookup.
 */
static const u32 kl_bucket_defaults_video[KL_HISTOGRAM_COUNTERS] =
{
	TABLE_HEADER_VALUES,

	/* slots 51 - 59 can by any value */
	100, 150, 200, 250, 300, 350, 500, 700, 900,

	/* slots 60 - 63 */
	 1 * 1000, /* 1 seconds to 60 seconds */
	 5 * 1000,
	15 * 1000,
	60 * 1000
};

void kl_histogram_reset(struct kl_histogram *hg, const char *name, enum kl_histogram_bucket_t t)
{
	int i;

	if ((!hg) || (!name))
		return;

	if (strlen(name) >= sizeof(hg->name))
		return;

	memset(hg, 0, sizeof(*hg));
	strcpy(hg->name, name);

#ifdef KL_USERSPACE
	gettimeofday(&hg->time_curr, 0);
#else
	hg->msecs_curr = jiffies_to_msecs(jiffies);
#endif
	hg->cumulative_msecs = 0;

	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {
		if (t == KL_BUCKET_VIDEO)
			hg->counter[i].val = kl_bucket_defaults_video[i];
	}
}

#if HAVE_RRD_H
int kl_histogram_rrd_gauge_enable(struct kl_histogram *hg, const char *rrd_filename, const char *chartTitle)
{
	if (!hg || !rrd_filename || hg->rrd_required || !chartTitle)
		return -1;

	hg->rrd_filename_db = strdup(rrd_filename);
	hg->rrd_chartTitle = strdup(chartTitle);

	const char *params[] = {
		"DS:myval:GAUGE:600:0:U",
		"RRA:AVERAGE:0.5:1:576",
		NULL
	};
	int ret = rrd_create_r(hg->rrd_filename_db, 1, 0, 2, params);
	printf("Creating RRD chart %s = %d\n", hg->rrd_filename_db, ret);
	hg->rrd_initialized = 1;
	hg->rrd_required = 1;

	return 0;
}

static void _updateRRD(struct kl_histogram *hg, uint64_t valueMs)
{
	// printf("Add %" PRIu64 " value to %s\n", valueMs, hg->rrd_filename_db);

	time_t now;
	time(&now);
	char data[64];
	sprintf(data, "N:%" PRIu64, valueMs);

	const char *params[] = {
       data,
       NULL
    };

    rrd_update_r(hg->rrd_filename_db, NULL, 1, params);

	/*
	 * rrdtool graph /tmp/myfile.png -a PNG --title "X264 GOP Compression" \
	 *		"DEF:myval=/tmp/myfile.rrd:myval:AVERAGE" "LINE2:myval#00CC00: ms" --start -15 --end now
	 */
 }
#endif

static void _kl_histogram_update(struct kl_histogram *hg)
{
	int i;
	u64 msecs_diff;

	if (!hg)
		return;

#ifdef KL_USERSPACE
	hg->time_prev = hg->time_curr; /* Implied struct copy */
	gettimeofday(&hg->time_curr, 0);
	struct timeval result;
	timeval_subtract(&result, &hg->time_curr, &hg->time_prev);
	msecs_diff = timeval_to_ms(&result);
#else
	hg->msecs_prev = hg->msecs_curr;
	hg->msecs_curr = jiffies_to_msecs(jiffies);
	msecs_diff = hg->msecs_curr - hg->msecs_prev;
#endif

#if HAVE_RRD_H
	if (hg->rrd_required && hg->rrd_initialized) {
       _updateRRD(hg, msecs_diff);
	}
#endif

	if (msecs_diff <= KL_HISTOGRAM_LOOKUP_MAX) {
		i = msecs_diff;
		hg->counter[i].count++;
#ifdef KL_USERSPACE
		gettimeofday(&hg->counter[i].update_time, 0);
#else
		hg->counter[i].update_time = jiffies;
#endif
	} else {
		for (i = KL_HISTOGRAM_LOOKUP_MAX; i < KL_HISTOGRAM_COUNTERS; i++ ) {
			if (msecs_diff <= hg->counter[i].val) {
				hg->counter[i].count++;
#ifdef KL_USERSPACE
				gettimeofday(&hg->counter[i].update_time, 0);
#else
				hg->counter[i].update_time = jiffies;
#endif
				break;
			}
		}
	}
}

void kl_histogram_update(struct kl_histogram *hg)
{
	_kl_histogram_update(hg);
}

void kl_histogram_update_with_value(struct kl_histogram *hg, u32 val)
{
	int i;
	if (val <= KL_HISTOGRAM_LOOKUP_MAX) {
		hg->counter[val].count++;
#ifdef KL_USERSPACE
		gettimeofday(&hg->counter[val].update_time, 0);
#else
		hg->counter[val].update_time = jiffies;
#endif
	} else {
		for (i = KL_HISTOGRAM_LOOKUP_MAX; i < KL_HISTOGRAM_COUNTERS; i++ ) {
			if (val <= hg->counter[i].val) {
				hg->counter[i].count++;
#ifdef KL_USERSPACE
				gettimeofday(&hg->counter[i].update_time, 0);
#else
				hg->counter[i].update_time = jiffies;
#endif
				break;
			}
		}
	}
}

void kl_histogram_sample_begin(struct kl_histogram *hg)
{
	if (!hg)
		return;

#ifdef KL_USERSPACE
	hg->time_prev = hg->time_curr; /* Implied struct copy in USERLAND */
	gettimeofday(&hg->time_curr, 0);
#else
	hg->msecs_prev = hg->msecs_curr;
	hg->msecs_curr = jiffies_to_msecs(jiffies);
#endif
}

void kl_histogram_sample_complete(struct kl_histogram *hg)
{
	_kl_histogram_update(hg);
}

#ifdef KL_USERSPACE
void kl_histogram_printf(struct kl_histogram *hg)
{
	char timestamp[32];
	u32 entries = 0;
	int i;

	if (!hg)
		return;

	printf("Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {

		if (hg->counter[i].count == 0)
			continue;

		sprintf(timestamp, "%s", ctime(&hg->counter[i].update_time.tv_sec));
		timestamp[strlen(timestamp) - 1] = 0; /* Trim trailing CR */
		printf(
#if defined(__linux__)
		" ->%8d %12d %s (%ld.%" PRIu64 ")\n",
#endif
#if defined(__APPLE__)
		" ->%8d %12d %s (%ld.%d)\n",
#endif
			hg->counter[i].val,
			hg->counter[i].count,
			timestamp,
			hg->counter[i].update_time.tv_sec,
			hg->counter[i].update_time.tv_usec);

		entries++;
	}
	printf("Total: %d\n", entries);
}
void kl_histogram_dprintf(struct kl_histogram *hg, int fd)
{
	u32 entries = 0;
	int i;

	if (!hg)
		return;

	dprintf(fd, "Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {

		if (hg->counter[i].count == 0)
			continue;

        dprintf(fd,
#if defined(__linux__)
		" ->%8d %12d %ld.%" PRIu64 "\n",
#endif
#if defined(__APPLE__)
		" ->%8d %12d %ld.%d\n",
#endif
			hg->counter[i].val,
			hg->counter[i].count,
			hg->counter[i].update_time.tv_sec,
			hg->counter[i].update_time.tv_usec);

		entries++;
	}
	dprintf(fd, "Total: %d\n", entries);
}
#else
void kl_histogram_print_procfs(struct kl_histogram *hg, struct seq_file *m)
{
	u32 entries = 0;
	int i;

	if (!hg)
		return;

	seq_printf(m, "Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {

		if (hg->counter[i].count == 0)
			continue;

		seq_printf(m, " ->%8d %12d %Ld\n",
			hg->counter[i].val,
			hg->counter[i].count,
			hg->counter[i].update_time);

		entries++;
	}
	seq_printf(m, "Total: %d\n", entries);
}

void kl_histogram_print_syslog(struct kl_histogram *hg)
{
	u32 entries = 0;
	int i;

	if (!hg)
		return;

	printk(KERN_ERR "Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {

		if (hg->counter[i].count == 0)
			continue;

		printk(KERN_ERR " %8d %12d %Ld\n",
			hg->counter[i].val,
			hg->counter[i].count,
			hg->counter[i].update_time);

		entries++;
	}
	printk(KERN_ERR "Total: %d\n", entries);
}

void kl_histogram_print_v4l2_device(struct v4l2_device *dev, struct kl_histogram *hg)
{
	u32 entries = 0;
	int i;

	if (!hg)
		return;

	v4l2_info(dev, "Histogram named %s (ms, count, last_update_jiffy)\n", hg->name);
	for (i = 0; i < KL_HISTOGRAM_COUNTERS; i++) {

		if (hg->counter[i].count == 0)
			continue;

		v4l2_info(dev, " %8d %12d %Ld\n",
			hg->counter[i].val,
			hg->counter[i].count,
			hg->counter[i].update_time);

		entries++;
	}
	v4l2_info(dev, "Total: %d\n", entries);
}
#endif

#ifdef KL_USERSPACE
void kl_histogram_cumulative_initialize(struct kl_histogram *hg)
{
	if (!hg)
		return;

	hg->cumulative_msecs = 0;
}

void kl_histogram_cumulative_begin(struct kl_histogram *hg)
{
	if (!hg)
		return;

	kl_histogram_sample_begin(hg);
}

void kl_histogram_cumulative_complete(struct kl_histogram *hg)
{
	if (!hg)
		return;

	hg->time_prev = hg->time_curr; /* Implied struct copy */
	gettimeofday(&hg->time_curr, 0);
	struct timeval result;
	timeval_subtract(&result, &hg->time_curr, &hg->time_prev);

	hg->cumulative_msecs += timeval_to_ms(&result);
}

void kl_histogram_cumulative_finalize(struct kl_histogram *hg)
{
	if (!hg)
		return;

	kl_histogram_update_with_value(hg, hg->cumulative_msecs);
}

#endif /* KL_USERSPACE */


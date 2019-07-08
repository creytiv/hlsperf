/**
 * @file main.c Main application code
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <pthread.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static const char *uri;
static uint32_t num_sess = 1;
static struct client **cliv = NULL;


static void client_error_handler(struct client *cli, int err, void *arg)
{
	if (err) {
		DEBUG_WARNING("client error (%m)\n", err);
	}

	re_cancel();
}


static void tmr_handler(void *arg)
{
	re_printf("timer elapsed -- terminate\n");
	re_cancel();
}


static void signal_handler(int signum)
{
	size_t i;

	(void)signum;

	re_fprintf(stderr, "terminated on signal %d (thread %p)\n",
		   signum, pthread_self());

	for (i=0; i<num_sess; i++) {

		struct client *cli = cliv[i];

		client_close(cli, 0);
	}

	re_cancel();
}


static void usage(void)
{
	re_fprintf(stderr,
		   "usage: dashperf [-n num] [-t timeout] <http-uri>\n"
		   "\t-n <num>      Number of parallel sessions\n"
		   "\t-t <timeout>  Timeout in seconds\n");
}


struct stats {
	double min;
	double max;
	double acc;
	unsigned count;
};


static void stats_init(struct stats *stats)
{
	memset(stats, 0, sizeof(*stats));
}


static void stats_update(struct stats *stats, double val)
{
	if (stats->count) {

		stats->min = min(val, stats->min);
		stats->max = max(val, stats->max);
		stats->acc += val;
		++stats->count;
	}
	else {
		stats->min = val;
		stats->max = val;
		stats->acc = val;
		stats->count = 1;
	}
}


static double stats_average(const struct stats *stats)
{
	if (stats->count)
		return stats->acc / (double)stats->count;
	else
		return -1;
}


static int stats_print(struct re_printf *pf, const struct stats *stats)
{
	if (!stats)
		return 0;

	if (stats->count)
		return re_hprintf(pf, "%.1f/%.1f/%.1f",
				  stats->min,
				  stats_average(stats),
				  stats->max);
	else
		return re_hprintf(pf, "(not set)");
}


static void show_summary(struct client * const *clivx, size_t clic)
{
	struct stats stats_conn, stats_media, stats_bitrate;
	size_t n_connected = 0;
	size_t i, j;

	stats_init(&stats_conn);
	stats_init(&stats_media);
	stats_init(&stats_bitrate);

	for (i=0; i<clic; i++) {

		const struct client *cli = clivx[i];
		struct media_playlist * const *mplv;
		int64_t conn_time;

		if (!client_connected(cli))
			continue;

		++n_connected;

		conn_time = client_conn_time(cli);

		stats_update(&stats_conn, conn_time);

		mplv = client_playlists(cli);

		for (j=0; j<MAX_PLAYLISTS; j++) {

			struct media_playlist *mpl = mplv[j];

			if (!mpl)
				continue;

			if (mpl->media_count) {
				int64_t media_time;
				double bitrate;

				media_time = mpl->media_time_acc
					/ mpl->media_count;

				bitrate = (double)mpl->bitrate_acc
					/ mpl->media_count;
				bitrate *= .000001;

				stats_update(&stats_media, media_time);

				stats_update(&stats_bitrate, bitrate);
			}
		}
	}

	re_printf("- - - dashperf summary - - -\n");
	re_printf("total sessions:  %zu\n", clic);
	re_printf("connected:       %zu\n", n_connected);
	re_printf("conn min/avg/max:   %H ms\n", stats_print, &stats_conn);
	re_printf("media min/avg/max:  %H ms\n", stats_print, &stats_media);
	re_printf("peak bitrate min/avg/max:  %H Mbps\n",
		  stats_print, &stats_bitrate);
	re_printf("- - - - - - - - - - -  - - -\n");
}


static void *client_thread_handler(void *arg)
{
	struct client **clip = arg;
	int err;

	err = re_thread_init();
	if (err) {
		DEBUG_WARNING("re thread init: %m\n", err);
		return NULL;
	}

	/* must be set per thread.
	 * must be done after re_thread_init()
	 */
	err = fd_setsize(8192);
	if (err) {
		re_fprintf(stderr, "fd_setsize error: %m\n", err);
		goto out;
	}

	err = client_alloc(clip, uri, client_error_handler, NULL);
	if (err)
		goto out;

	err = client_start(*clip);
	if (err)
		goto out;

	/* run the main loop now */
	re_main(signal_handler);

 out:
	/* cleanup */
	re_thread_close();

	return NULL;
}


int main(int argc, char *argv[])
{
	struct tmr tmr;
	pthread_t *tidv = NULL;
	uint32_t timeout = 0;
	size_t i;
	int err = 0;

	for (;;) {

		const int c = getopt(argc, argv, "hn:t:");
		if (0 > c)
			break;

		switch (c) {

		case 'n':
			num_sess = atoi(optarg);
			break;

		case 't':
			timeout = atoi(optarg);
			break;

		case '?':
		default:
			err = EINVAL;
			/*@fallthrough@*/
		case 'h':
			usage();
			return err;
		}
	}

	if (argc < 2 || (argc != (optind + 1))) {
		usage();
		return -2;
	}

	uri = argv[optind + 0];

	re_printf("dashperf -- uri=%s, sessions=%u\n", uri, num_sess);

	re_printf("main: thread %p\n", pthread_self());

	err = fd_setsize(2048);
	if (err) {
		re_fprintf(stderr, "fd_setsize error: %m\n", err);
		goto out;
	}

	err = libre_init();
	if (err) {
		(void)re_fprintf(stderr, "libre_init: %m\n", err);
		goto out;
	}

	tmr_init(&tmr);

	(void)sys_coredump_set(true);

	cliv = mem_reallocarray(NULL, num_sess, sizeof(*cliv), NULL);
	tidv = mem_reallocarray(NULL, num_sess, sizeof(*tidv), NULL);
	if (!cliv || !tidv) {
		err = ENOMEM;
		goto out;
	}

	for (i=0; i<num_sess; i++) {

		err = pthread_create(&tidv[i], NULL,
				     client_thread_handler, &cliv[i]);
		if (err)
			return err;
	}

	if (timeout != 0) {
		re_printf("starting timeout timer, %u seconds\n", timeout);
		tmr_start(&tmr, timeout * 1000, tmr_handler, NULL);
	}

	(void)re_main(signal_handler);

	re_printf("Hasta la vista\n");

 out:
	if (cliv && tidv) {
		for (i=0; i<num_sess; i++) {

			struct client *cli = cliv[i];

			client_close(cli, 0);

			/* wait for thread to end */
			pthread_join(tidv[i], NULL);
		}

		show_summary(cliv, num_sess);

		for (i=0; i<num_sess; i++) {
			mem_deref(cliv[i]);
		}
	}
	mem_deref(cliv);
	mem_deref(tidv);
	tmr_cancel(&tmr);

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}

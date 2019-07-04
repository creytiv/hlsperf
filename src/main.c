/**
 * @file main.c Main application code
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static void client_error_handler(struct client *cli, int err, void *arg)
{
	DEBUG_WARNING("client error (%m)\n", err);

	if (cli->saved_scode == 404)
		re_cancel();
}


static void tmr_handler(void *arg)
{
	re_printf("timer elapsed -- terminate\n");
	re_cancel();
}


static void signal_handler(int signum)
{
	(void)signum;

	re_fprintf(stderr, "terminated on signal %d\n", signum);

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


static void show_summary(struct client * const *cliv, size_t clic)
{
	struct stats stats_conn, stats_media, stats_bitrate;
	size_t n_connected = 0;
	size_t i, j;

	stats_init(&stats_conn);
	stats_init(&stats_media);
	stats_init(&stats_bitrate);

	for (i=0; i<clic; i++) {

		const struct client *cli = cliv[i];
		int64_t conn_time;

		if (!cliv[i]->connected)
			continue;

		++n_connected;

		conn_time = cli->ts_conn - cli->ts_start;

		stats_update(&stats_conn, conn_time);

		for (j=0; j<ARRAY_SIZE(cli->mplv); j++) {

			struct media_playlist *mpl = cli->mplv[j];

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


int main(int argc, char *argv[])
{
	const char *uri;
	struct tmr tmr;
	struct client **cliv = NULL;
	uint32_t num_sess = 1;
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

	err = fd_setsize(4096);
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
	if (!cliv) {
		err = ENOMEM;
		goto out;
	}

	for (i=0; i<num_sess; i++) {

		err = client_alloc(&cliv[i], uri,
				   client_error_handler, NULL);
		if (err)
			goto out;

		err = client_start(cliv[i]);
		if (err)
			goto out;
	}

	if (timeout != 0) {
		re_printf("starting timeout timer, %u seconds\n", timeout);
		tmr_start(&tmr, timeout * 1000, tmr_handler, NULL);
	}

	(void)re_main(signal_handler);

	re_printf("Hasta la vista\n");

 out:
	if (cliv) {
		show_summary(cliv, num_sess);

		for (i=0; i<num_sess; i++) {
			mem_deref(cliv[i]);
		}
	}
	mem_deref(cliv);
	tmr_cancel(&tmr);

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}

/**
 * @file main.c Main application code
 *
 * Copyright (C) 2010 Creytiv.com
 */

#include <stdlib.h>
#include <getopt.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


static int dns_init(struct dnsc **dnsc)
{
	struct sa nsv[8];
	uint32_t nsn;
	int err;

	nsn = ARRAY_SIZE(nsv);

	err = dns_srv_get(NULL, 0, nsv, &nsn);
	if (err) {
		(void)re_fprintf(stderr, "dns_srv_get: %m\n", err);
		goto out;
	}

	err = dnsc_alloc(dnsc, NULL, nsv, nsn);
	if (err) {
		(void)re_fprintf(stderr, "dnsc_alloc: %m\n", err);
		goto out;
	}

 out:
	return err;
}


static void client_error_handler(int err, void *arg)
{
	DEBUG_WARNING("client error (%m) -- stop\n", err);
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


static void show_summary(struct client * const *cliv, size_t clic)
{
	size_t n_connected = 0;
	size_t i;
	int64_t conn_min=99999999, conn_avg = 0, conn_max = -1;

	for (i=0; i<clic; i++) {

		const struct client *cli = cliv[i];

		if (cliv[i]->connected) {

			int64_t conn_time = cli->ts_conn - cli->ts_start;

			++n_connected;

			conn_min = min(conn_time, conn_min);
			conn_avg += conn_time;
			conn_max = max(conn_time, conn_max);
		}
	}

	if (n_connected)
		conn_avg /= n_connected;
	else
		conn_avg = -1;

	re_printf("- - - dashperf summary - - -\n");
	re_printf("total sessions:  %zu\n", clic);
	re_printf("connected:       %zu\n", n_connected);
	re_printf("min/avg/max:     %lli/%lli/%lli ms\n",
		  conn_min, conn_avg, conn_max);
	re_printf("- - - - - - - - - - -  - - -\n");
}


int main(int argc, char *argv[])
{
	struct dnsc *dnsc = NULL;
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

	err = dns_init(&dnsc);
	if (err)
		goto out;

	cliv = mem_reallocarray(NULL, num_sess, sizeof(*cliv), NULL);
	if (!cliv) {
		err = ENOMEM;
		goto out;
	}

	for (i=0; i<num_sess; i++) {

		err = client_alloc(&cliv[i], dnsc, uri,
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
	show_summary(cliv, num_sess);
	for (i=0; i<num_sess; i++) {
		mem_deref(cliv[i]);
	}
	mem_deref(cliv);
	mem_deref(dnsc);
	tmr_cancel(&tmr);

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}

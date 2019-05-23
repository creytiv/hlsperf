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


struct client *cli;


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
		   "Usage: dashperf [options] <uri> <duration> <num>\n"
		   "\n"
		   "options:\n"
		   "\n");
}


int main(int argc, char *argv[])
{
	struct dnsc *dnsc = NULL;
	const char *uri, *dur, *num;
	struct tmr tmr;
	uint32_t duration;
	uint32_t num_sess;
	int err = 0;

	for (;;) {

		const int c = getopt(argc, argv, "h");
		if (0 > c)
			break;

		switch (c) {

		case '?':
		default:
			err = EINVAL;
			/*@fallthrough@*/
		case 'h':
			usage();
			return err;
		}
	}

	if (argc < 2 || (argc != (optind + 3))) {
		usage();
		return -2;
	}

	uri = argv[optind + 0];
	dur = argv[optind + 1];
	num = argv[optind + 2];

	re_printf("dashperf -- uri=%s, dur=%s, num=%s\n", uri, dur, num);

	duration = atoi(dur);
	num_sess = atoi(num);

	if (duration == 0) {
		re_printf("invalid duration\n");
		return -2;
	}
	if (num_sess == 0) {
		re_printf("invalid number of sessions\n");
		return -2;
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

	err = client_alloc(&cli, dnsc, uri);
	if (err)
		goto out;

	err = client_start(cli);
	if (err)
		goto out;

	tmr_start(&tmr, duration * 1000, tmr_handler, NULL);

	(void)re_main(signal_handler);

	re_printf("Hasta la vista\n");

 out:
	tmr_cancel(&tmr);
	mem_deref(cli);
	mem_deref(dnsc);

	libre_close();

	/* Check for memory leaks */
	tmr_debug();
	mem_debug();

	return err;
}

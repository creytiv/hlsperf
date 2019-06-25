/**
 * @file client.c Dash Performance client
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


enum {
	DURATION = 10
};


static int load_playlist(struct client *cli);
static void http_resp_handler(int err, const struct http_msg *msg, void *arg);


static void destructor(void *data)
{
	struct client *cli = data;

	tmr_cancel(&cli->tmr_play);
	tmr_cancel(&cli->tmr_load);
	list_flush(&cli->playlist);
	mem_deref(cli->cli);
	mem_deref(cli->uri);
}


static void print_summary(struct client *cli)
{
	double dur;
	size_t bits = cli->bytes * 8;

	dur = (double)(tmr_jiffies() - cli->ts_start) * .001;

	re_printf("- - - summary - - -\n");
	re_printf("summary: downloaded %zu bytes in %.1f seconds"
		  " (average %.1f bits/s)\n",
		  cli->bytes, dur, bits / dur);
	re_printf("- - - - - - - - - -\n");
}


static int get_item(struct client *cli, const char *uri)
{
	int err;

	err = http_request(NULL, cli->cli, "GET", uri,
			   http_resp_handler, NULL, cli, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}

	return 0;
}


static void handle_line(struct client *cli, const struct pl *line)
{
	struct pl file, ext;
	char *uri = NULL;
	int err;

	/* ignore comment */
	if (line->p[0] == '#')
		return;

	if (re_regex(line->p, line->l, "[^.]+.[a-z0-9]+", &file, &ext)) {
		DEBUG_NOTICE("could not parse line (%r)\n", line);
		return;
	}

	if (0 == pl_strcasecmp(&ext, "m3u8")) {

		if (cli->slid == 0) {

			struct pl slid;

			if (0 == re_regex(line->p, line->l,
					  "?slid=[0-9]+", &slid)) {

				cli->slid = pl_u32(&slid);
			}
		}

		/* recurse into next playlist */
		err = re_sdprintf(&uri, "%r%r", &cli->path, line);
		if (err)
			return;

		get_item(cli, uri);
	}
	else if (0 == pl_strcasecmp(&ext, "m4s") ||
		 0 == pl_strcasecmp(&ext, "mp4")) {

		char *filename;

		pl_strdup(&filename, line);

		mediafile_new(&cli->playlist, filename);

		mem_deref(filename);
	}
	else {
		DEBUG_NOTICE("hls: unknown extension: %r\n", &ext);
	}

	mem_deref(uri);
}


static void complete(struct client *cli)
{
	print_summary(cli);
}


static void start_player(struct client *cli)
{
	struct mediafile *mf;
	char *uri;
	int err;

	/* get the first playlist item */

	mf = list_ledata(list_head(&cli->playlist));
	if (!mf) {
		re_printf("playlist is empty!\n");
		complete(cli);
		return;
	}

	/* download the media file */

	err = re_sdprintf(&uri, "%r%s", &cli->path, mf->filename);
	if (err)
		return;

	get_item(cli, uri);

	mem_deref(uri);
}


static int handle_hls_playlist(struct client *cli, const struct http_msg *msg)
{
	struct pl pl;

#if 0
	re_printf("- - - - - - - - - - \n");
	re_printf("%b\n", mbuf_buf(msg->mb), mbuf_get_left(msg->mb));
	re_printf("- - - - - - - - - - \n");
#endif

	pl_set_mbuf(&pl, msg->mb);

	while (pl.l > 1) {

		const char *end;
		struct pl line;

		end = pl_strchr(&pl, '\n');
		if (!end)
			break;

		line.p = pl.p;
		line.l = end - pl.p;

		handle_line(cli, &line);

		pl_advance(&pl, line.l + 1);
	}

#if 0
	re_printf("hls playlist done, %u entries, slid=%u\n",
		  list_count(&cli->playlist), cli->slid);
#endif

	if (!list_isempty(&cli->playlist)) {

		start_player(cli);
	}

	return 0;
}


static void tmr_handler(void *data)
{
	struct client *cli = data;

	start_player(cli);
}


static void client_close(struct client *cli, int err)
{
	tmr_cancel(&cli->tmr_play);
	tmr_cancel(&cli->tmr_load);
	cli->cli = mem_deref(cli->cli);

	if (cli->errorh)
		cli->errorh(err, cli->arg);

	cli->errorh = NULL;
}


static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct client *cli = arg;

	if (err) {
		re_printf("http error: %m\n", err);
		cli->saved_err = err;
		client_close(cli, err);
		return;
	}

	if (msg->scode <= 199)
		return;
	else if (msg->scode >= 300) {
		re_printf("request failed (%u %r)\n",
			  msg->scode, &msg->reason);
		cli->saved_scode = msg->scode;
		client_close(cli, EPROTO);
		return;
	}

#if 0
	re_printf("%H\n", http_msg_print, msg);
#endif

	if (msg_ctype_cmp(&msg->ctyp, "application", "vnd.apple.mpegurl")) {

		if (!cli->ts_conn)
			cli->ts_conn = tmr_jiffies();

		cli->connected = true;

		handle_hls_playlist(cli, msg);
	}
	else if (msg_ctype_cmp(&msg->ctyp, "video", "mp4")) {

		uint32_t delay;

		cli->bytes += msg->clen;

		delay = DURATION*1000 + rand_u32() % 1000;

		mem_deref(list_ledata(cli->playlist.head));

		tmr_start(&cli->tmr_play, delay, tmr_handler, cli);
	}
	else {
		DEBUG_NOTICE("unknown content-type: %r/%r\n",
			  &msg->ctyp.type, &msg->ctyp.subtype);
	}
}


int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri,
		 client_error_h *errorh, void *arg)
{
	struct client *cli;
	const char *rslash;
	int err;

#if 0
#ifdef DARWIN
	const char *cafile = "/etc/ssl/cert.pem";
#else
	const char *cafile = "/etc/ssl/certs/ca-certificates.crt";
#endif
#endif

	if (!clip || !dnsc || !uri)
		return EINVAL;

	rslash = strrchr(uri, '/');
	if (!rslash) {
		re_printf("invalid uri '%s'\n", uri);
		return EINVAL;
	}

	cli = mem_zalloc(sizeof(*cli), destructor);
	if (!cli)
		return ENOMEM;

	err = http_client_alloc(&cli->cli, dnsc);
	if (err)
		goto out;

#if 0
	err = tls_add_ca(http_client_tls(cli->cli), cafile);
	if (err) {
		DEBUG_WARNING("failed to add cafile '%s'\n", cafile);
		goto out;
	}
#endif

	err = str_dup(&cli->uri, uri);
	if (err)
		goto out;

	cli->path.p = uri;
	cli->path.l = rslash + 1 - uri;

	cli->errorh = errorh;
	cli->arg = arg;

	re_printf("uri path = '%r'\n", &cli->path);

 out:
	if (err)
		mem_deref(cli);
	else
		*clip = cli;

	return err;
}


/* Load or Re-load playlist */
static int load_playlist(struct client *cli)
{
	int err;

	re_printf("load playlist: %s\n", cli->uri);

	cli->ts_start = tmr_jiffies();

	err = http_request(NULL, cli->cli, "GET", cli->uri,
			   http_resp_handler, NULL, cli, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}


	return 0;
}


static void tmr_load_handler(void *data)
{
	struct client *cli = data;

	load_playlist(cli);
}


int client_start(struct client *cli)
{
	uint32_t delay = 500 + rand_u32() % 500;

	re_printf("start: %s (delay=%ums)\n", cli->uri, delay);

	tmr_start(&cli->tmr_load, delay, tmr_load_handler, cli);

	return 0;
}



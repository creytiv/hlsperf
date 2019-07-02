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


static int load_playlist(struct client *cli);
static void http_resp_handler(int err, const struct http_msg *msg, void *arg);


static void destructor(void *data)
{
	struct client *cli = data;
	unsigned i;

	cli->terminated = true;

	tmr_cancel(&cli->tmr_load);
	for (i=0; i<ARRAY_SIZE(cli->mplv); i++) {

		mem_deref(cli->mplv[i]);
	}

	mem_deref(cli->cli);
	mem_deref(cli->uri);
}


static int add_playlist(struct client *cli, const char *filename)
{
	struct pl media_ix;
	int err = 0;

	re_printf("add_playlist (%s)\n", filename);

	if (0 == re_regex(filename, str_len(filename),
			  "media_[0-9]+", &media_ix)) {

		unsigned ix = pl_u32(&media_ix);

		if (ix >= ARRAY_SIZE(cli->mplv)) {
			re_printf("index out of range (%u >= %zu)\n",
				  ix, ARRAY_SIZE(cli->mplv));
			return ERANGE;
		}

		err = playlist_new(&cli->mplv[ix], cli, filename);
		if (err)
			goto out;

		err = playlist_start(cli->mplv[ix]);
		if (err)
			goto out;
	}
	else {
		DEBUG_WARNING("m3u8 file not handled (%s)\n", filename);
	}

 out:
	return err;
}


static void handle_line(struct client *cli, const struct pl *line)
{
	struct pl file, ext, val;
	char *uri = NULL;
	int err = 0;

	/* ignore comment */
	if (line->p[0] == '#') {

		struct pl pl_dur;

		/* field: #EXTINF:10.000000 */
		if (0 == re_regex(line->p, line->l,
				  "EXTINF:[0-9.]+", &pl_dur)) {

			DEBUG_WARNING("unexpected field (%r)\n", line);
		}

		if (0 == re_regex(line->p, line->l,
				  "#EXT-X-MEDIA:[^]+", &val)) {

			struct pl muri;

			re_printf("---> got EXT-X-MEDIA: %r\n", &val);

			if (0 == re_regex(val.p, val.l,
					  "URI=\"[^\"]+\"", &muri)) {

				char buf[256];

				re_printf("---> uri = '%r'\n", &muri);

				pl_strcpy(&muri, buf, sizeof(buf));

				err = add_playlist(cli, buf);
				if (err)
					goto out;
			}
		}

		return;
	}

	if (re_regex(line->p, line->l, "[^.]+.[a-z0-9]+", &file, &ext)) {
		DEBUG_NOTICE("could not parse line (%r)\n", line);
		return;
	}

	if (0 == pl_strcasecmp(&ext, "m3u8")) {

		char buf[256];

		re_snprintf(buf, sizeof(buf), "%r", line);

		err = add_playlist(cli, buf);
		if (err)
			goto out;

		if (cli->slid == 0) {

			struct pl slid;

			if (0 == re_regex(line->p, line->l,
					  "?slid=[0-9]+", &slid)) {

				cli->slid = pl_u32(&slid);
			}
		}
	}
	else {
		DEBUG_NOTICE("hls: unknown extension: %r\n", &ext);
	}

 out:
	if (err)
		re_printf("WARNING: parse error\n");

	mem_deref(uri);
}


static int handle_hls_playlist(struct client *cli, const struct http_msg *msg)
{
	struct pl pl;

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

	return 0;
}


void client_close(struct client *cli, int err)
{
	if (!cli)
		return;

	cli->terminated = true;
	tmr_cancel(&cli->tmr_load);
	cli->cli = mem_deref(cli->cli);

	if (cli->errorh)
		cli->errorh(cli, err, cli->arg);

	cli->errorh = NULL;
}


static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct client *cli = arg;

	if (cli->terminated)
		return;

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

 out:
	if (err)
		mem_deref(cli);
	else
		*clip = cli;

	return err;
}


/* Load master playlist */
static int load_playlist(struct client *cli)
{
	int err;

	if (!cli->ts_start)
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
	uint32_t delay = rand_u32() % 5000;

	if (!cli)
		return EINVAL;

	tmr_start(&cli->tmr_load, delay, tmr_load_handler, cli);

	return 0;
}

/**
 * @file playlist.c HLS Performance client -- media playlist
 *
 * Copyright (C) 2019 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include "hlsperf.h"


#define DEBUG_MODULE "hlsperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


enum {
	RELOAD_INTERVAL = 6  /* seconds */
};


static void start_player(struct media_playlist *mpl);


static void destructor(void *data)
{
	struct media_playlist *pl = data;

	pl->terminated = true;

	tmr_cancel(&pl->tmr_play);
	tmr_cancel(&pl->tmr_reload);
	mem_deref(pl->filename);
	mem_deref(pl->req);
	mem_deref(pl->req_media);
	list_flush(&pl->playlist);
}


void playlist_close(struct media_playlist *mpl, int err)
{
	if (!mpl)
		return;

	mpl->terminated = true;

	tmr_cancel(&mpl->tmr_play);
	tmr_cancel(&mpl->tmr_reload);

	mpl->req       = mem_deref(mpl->req);
	mpl->req_media = mem_deref(mpl->req_media);
}


static int http_data_handler(const uint8_t *buf, size_t size,
			     const struct http_msg *msg, void *arg)
{
	/* ignore data */

	return 0;
}


static void media_http_resp_handler(int err, const struct http_msg *msg,
				    void *arg)
{
	struct media_playlist *mpl = arg;

	if (mpl->terminated)
		return;

	if (err) {
		re_printf("playlist: http error: %m\n", err);
		playlist_close(mpl, err);
		return;
	}
	else if (msg->scode >= 300) {
		re_printf("playlist: request failed (%u %r)\n",
			  msg->scode, &msg->reason);
		playlist_close(mpl, EPROTO);
		return;
	}

	if (msg_ctype_cmp(&msg->ctyp, "video", "mp4") ||
	    msg_ctype_cmp(&msg->ctyp, "application", "octet-stream")) {

		int64_t media_time;
		double bitrate;

		mpl->ts_media_resp = tmr_jiffies();

		media_time = mpl->ts_media_resp - mpl->ts_media_req;

		mpl->media_time_acc += media_time;
		++mpl->media_count;

		mpl->bytes += msg->clen;

		bitrate = (double)(mpl->bytes * 8000) / media_time;

		mpl->bitrate_acc += bitrate;
	}
	else {
		DEBUG_NOTICE("unknown content-type: %r/%r\n",
			  &msg->ctyp.type, &msg->ctyp.subtype);
	}
}


static void tmr_play_handler(void *data)
{
	struct media_playlist *mpl = data;

	start_player(mpl);
}


static int get_media_file(struct media_playlist *mpl, struct mediafile *mf,
			  const char *uri)
{
	int err;

	mpl->req_media = mem_deref(mpl->req_media);

	err = http_request(&mpl->req_media, client_http_cli(mpl->cli),
			   "GET", uri,
			   media_http_resp_handler,
			   http_data_handler, mpl, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}

	mf->played = true;

	return 0;
}


static void start_player(struct media_playlist *mpl)
{
	struct mediafile *mf;
	char *uri = NULL;
	uint32_t delay = 10000;
	int err;

	/* get the next playlist item */
	mf = mediafile_next(&mpl->playlist);
	if (mf) {

		delay = mf->duration*1000;

		/* download the media file */
		err = re_sdprintf(&uri, "%r%s",
				  client_path(mpl->cli), mf->filename);
		if (err)
			goto out;

		mpl->ts_media_req = tmr_jiffies();

		get_media_file(mpl, mf, uri);
	}

 out:
	tmr_start(&mpl->tmr_play, delay, tmr_play_handler, mpl);

	mem_deref(uri);
}


static void handle_line(struct media_playlist *mpl, const struct pl *line)
{
	struct pl file, ext;
	int err = 0;

	/* ignore comment */
	if (line->p[0] == '#') {

		struct pl pl_dur;

		/* field: #EXTINF:10.000000 */
		if (0 == re_regex(line->p, line->l,
				  "EXTINF:[0-9.]+", &pl_dur)) {

			double dur = pl_float(&pl_dur);

			if (dur > 1.0)
				mpl->last_dur = dur;
		}

		return;
	}

	if (re_regex(line->p, line->l, "[^.]+.[a-z0-9]+", &file, &ext)) {
		DEBUG_NOTICE("could not parse line (%r)\n", line);
		return;
	}

	if (0 == pl_strcasecmp(&ext, "m4s")) {

		char *filename;

		err = pl_strdup(&filename, line);
		if (err)
			goto out;

		if (!mediafile_find(&mpl->playlist, filename))
			mediafile_new(&mpl->playlist, filename, mpl->last_dur);

		mem_deref(filename);
	}
	else {
		DEBUG_NOTICE("hls: unknown extension: %r\n", &ext);
	}

 out:
	if (err)
		re_printf("parse error\n");
}


static int handle_hls_playlist(struct media_playlist *mpl,
			       const struct http_msg *msg)
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

		handle_line(mpl, &line);

		pl_advance(&pl, line.l + 1);
	}

	return 0;
}


/* Response: content of media_0.m3u8 */
static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct media_playlist *pl = arg;

	if (err) {
		re_printf("playlist: http error: %m\n", err);
		playlist_close(pl, err);
		return;
	}

	if (msg->scode <= 199)
		return;
	else if (msg->scode >= 300) {
		re_printf("playlist: request failed (%u %r)\n",
			  msg->scode, &msg->reason);
		playlist_close(pl, EPROTO);
		return;
	}

	if (msg_ctype_cmp(&msg->ctyp, "application", "vnd.apple.mpegurl")) {

		handle_hls_playlist(pl, msg);
	}
	else {
		DEBUG_NOTICE("unknown content-type: %r/%r\n",
			  &msg->ctyp.type, &msg->ctyp.subtype);
	}
}


static int load_playlist(struct media_playlist *mpl)
{
	char uri[512];
	int err;

	re_snprintf(uri, sizeof(uri), "%r%s",
		    client_path(mpl->cli), mpl->filename);

	err = http_request(&mpl->req, client_http_cli(mpl->cli), "GET", uri,
			   http_resp_handler, NULL, mpl, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}

	return 0;
}


static void timeout_reload(void *data)
{
	struct media_playlist *pl = data;

	tmr_start(&pl->tmr_reload, RELOAD_INTERVAL*1000, timeout_reload, pl);

	load_playlist(pl);
}


int playlist_new(struct media_playlist **plp, const struct client *cli,
		 const char *filename)
{
	struct media_playlist *pl;
	int err;

	if (!plp || !cli || !filename)
		return EINVAL;

	pl = mem_zalloc(sizeof(*pl), destructor);
	if (!pl)
		return ENOMEM;

	pl->cli = cli;
	pl->last_dur = 10.0;

	err = str_dup(&pl->filename, filename);
	if (err)
		goto out;

	tmr_init(&pl->tmr_reload);

 out:
	if (err)
		mem_deref(pl);
	else
		*plp = pl;

	return err;
}


int playlist_start(struct media_playlist *pl)
{
	int err;

	if (!pl)
		return EINVAL;

	err = load_playlist(pl);
	if (err)
		return err;

	tmr_start(&pl->tmr_reload, RELOAD_INTERVAL*1000, timeout_reload, pl);

	tmr_start(&pl->tmr_play, 5*1000, tmr_play_handler, pl);

	return err;
}

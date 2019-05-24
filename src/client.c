
#include <string.h>
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct client {
	struct http_cli *cli;
	char *uri;
	struct pl path;
	struct list playlist;  /* struct mediafile */
	uint32_t slid;
};


struct mediafile {
	struct le le;
	char *filename;
};


static int load_playlist(struct client *cli);
static void http_resp_handler(int err, const struct http_msg *msg, void *arg);


static void mediafile_destructor(void *data)
{
	struct mediafile *mf = data;

	list_unlink(&mf->le);
	mem_deref(mf->filename);
}


static void destructor(void *data)
{
	struct client *cli = data;

	list_flush(&cli->playlist);
	mem_deref(cli->cli);
	mem_deref(cli->uri);
}


static int mediafile_new(struct list *lst, const char *filename)
{
	struct mediafile *mf;

	mf = mem_zalloc(sizeof(*mf), mediafile_destructor);

	str_dup(&mf->filename, filename);

	list_append(lst, &mf->le, mf);

	return 0;
}


static int get_item(struct client *cli, const char *uri)
{
	int err;

	re_printf("get item: %s\n", uri);

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
	char *uri = NULL;
	int err;

	/* ignore comment */
	if (line->p[0] == '#')
		return;

	if (0 == re_regex(line->p, line->l, "m3u8")) {

		if (cli->slid == 0) {

			struct pl slid;

			if (0 == re_regex(line->p, line->l,
					  "?slid=[0-9]+", &slid)) {
				cli->slid = pl_u32(&slid);

				re_printf("*** Setting SLID to %u\n",
					  cli->slid);
			}
		}

		/* recurse into next playlist */
		err = re_sdprintf(&uri, "%r%r", &cli->path, line);

		get_item(cli, uri);
	}
	else if (0 == re_regex(line->p, line->l, "m4s")) {

		char *filename;

		pl_strdup(&filename, line);

		mediafile_new(&cli->playlist, filename);

		mem_deref(filename);
	}
	else {
		re_printf("line unhandled: %r\n", line);
	}

	mem_deref(uri);
}


static int handle_hls_playlist(struct client *cli, const struct http_msg *msg)
{
	struct pl pl;

	re_printf("handle hls playlist\n");

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

	re_printf("hls playlist done, %u entries, slid=%u\n",
		  list_count(&cli->playlist), cli->slid);

	return 0;
}


static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct client *cli = arg;

	if (err) {
		re_printf("http error: %m\n", err);
		return;
	}

	if (msg->scode <= 199)
		return;
	else if (msg->scode >= 300) {
		re_printf("request failed (%u %r)\n",
			  msg->scode, &msg->reason);
		return;
	}

	re_printf("success. %u bytes\n", msg->clen);

#if 0
	re_printf("%H\n", http_msg_print, msg);
#endif

	if (msg_ctype_cmp(&msg->ctyp, "application", "vnd.apple.mpegurl")) {
		handle_hls_playlist(cli, msg);
	}
	else {
		re_printf("unknown content-type: %r/%r\n",
			  &msg->ctyp.type, &msg->ctyp.subtype);
	}
}


int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri)
{
	struct client *cli;
	const char *rslash;
	int err;

#ifdef DARWIN
	const char *cafile = "/etc/ssl/cert.pem";
#else
	const char *cafile = "/etc/ssl/certs/ca-certificates.crt";
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

	err = tls_add_ca(http_client_tls(cli->cli), cafile);
	if (err) {
		DEBUG_WARNING("failed to add cafile '%s'\n", cafile);
		goto out;
	}

	err = str_dup(&cli->uri, uri);
	if (err)
		goto out;

	cli->path.p = uri;
	cli->path.l = rslash + 1 - uri;

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

	err = http_request(NULL, cli->cli, "GET", cli->uri,
			   http_resp_handler, NULL, cli, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}


	return 0;
}


int client_start(struct client *cli)
{
	re_printf("start: %s\n", cli->uri);

	return load_playlist(cli);
}



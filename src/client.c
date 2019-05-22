
#include <re.h>
#include "dashperf.h"


#define DEBUG_MODULE "dashperf"
#define DEBUG_LEVEL 6
#include <re_dbg.h>


struct client {
	struct http_cli *cli;
	char *uri;
};


static void destructor(void *data)
{
	struct client *cli = data;

	mem_deref(cli->cli);
	mem_deref(cli->uri);
}


static void http_resp_handler(int err, const struct http_msg *msg, void *arg)
{
	struct client *cli = arg;

	re_printf("resp:\n");

	if (err) {
		re_printf("http error: %m\n", err);
		return;
	}

	if (300 <= msg->scode && msg->scode <= 399) {

		const struct http_hdr *hdr;

		hdr = http_msg_hdr(msg, HTTP_HDR_LOCATION);

		re_printf("redirect to %r\n", &hdr->val);

	}

	re_printf("scode = %u\n", msg->scode);

	re_printf("%H\n", http_msg_print, msg);
}


int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri)
{
	struct client *cli;
	int err;

	cli = mem_zalloc(sizeof(*cli), destructor);

	err = http_client_alloc(&cli->cli, dnsc);
	if (err)
		goto out;

	err = str_dup(&cli->uri, uri);
	if (err)
		goto out;


 out:
	if (err)
		mem_deref(cli);
	else
		*clip = cli;

	return err;
}


int client_start(struct client *cli)
{
	int err;

	re_printf("start: %s\n", cli->uri);

	err = http_request(NULL, cli->cli, "GET", cli->uri,
			   http_resp_handler, NULL, cli, NULL);
	if (err) {
		re_printf("http request failed (%m)\n", err);
		return err;
	}


	return 0;
}



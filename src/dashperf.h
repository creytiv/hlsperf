/**
 * @file dashperf.h Dash Performance client -- interface
 *
 * Copyright (C) 2019 Creytiv.com
 */


/*
 * Client
 */

struct client;


typedef void (client_error_h)(struct client *cli, int err, void *arg);

struct client {
	struct http_cli *cli;
	char *uri;
	struct pl path;
	struct list playlist;  /* struct mediafile */
	uint32_t slid;
	struct tmr tmr_load;
	struct tmr tmr_play;
	uint64_t ts_start;
	uint64_t ts_conn;
	size_t bytes;
	bool connected;
	int saved_err;
	uint16_t saved_scode;

	uint64_t ts_media_req;
	uint64_t ts_media_resp;
	uint64_t media_time_acc;
	unsigned media_count;

	uint64_t bitrate_acc;

	bool terminated;
	client_error_h *errorh;
	void *arg;
};


struct client;


int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri,
		 client_error_h *errorh, void *arg);
int client_start(struct client *cli);


/*
 * Mediafile
 */

struct mediafile {
	struct le le;
	char *filename;
};


int mediafile_new(struct list *lst, const char *filename);

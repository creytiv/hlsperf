/**
 * @file dashperf.h Dash Performance client -- interface
 *
 * Copyright (C) 2019 Creytiv.com
 */


/*
 * Client
 */

typedef void (client_error_h)(int err, void *arg);

struct client {
	struct http_cli *cli;
	char *uri;
	struct pl path;
	struct list playlist;  /* struct mediafile */
	uint32_t slid;
	struct tmr tmr_load;
	struct tmr tmr_play;
	uint64_t ts_start;
	size_t bytes;
	bool connected;
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

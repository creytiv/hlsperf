/**
 * @file dashperf.h Dash Performance client -- interface
 *
 * Copyright (C) 2019 Creytiv.com
 */


/*
 * Client
 */

struct client;


typedef void (client_error_h)(int err, void *arg);


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

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
	struct media_playlist *mplv[2];
	uint32_t slid;
	struct tmr tmr_load;
	uint64_t ts_start;
	uint64_t ts_conn;
	bool connected;
	bool terminated;
	int saved_err;
	uint16_t saved_scode;
	client_error_h *errorh;
	void *arg;
};


int  client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri,
		  client_error_h *errorh, void *arg);
int  client_start(struct client *cli);
void client_close(struct client *cli, int err);


/*
 * Mediafile
 */

struct mediafile {
	struct le le;
	char *filename;
	double duration;  /* seconds */
	bool played;
};


int mediafile_new(struct list *lst, const char *filename, double duration);
struct mediafile *mediafile_find(const struct list *lst, const char *filename);
struct mediafile *mediafile_next(const struct list *lst);


/*
 * Playlist
 */


/*
 * represent media_0.m3u8
 */
struct media_playlist {
	const struct client *cli;
	char *filename;
	struct list playlist;
	struct http_req *req;
	struct tmr tmr_reload;
	struct tmr tmr_play;
	double last_dur;
	bool terminated;

	size_t bytes;
	uint64_t ts_media_req;
	uint64_t ts_media_resp;
	uint64_t media_time_acc;
	unsigned media_count;
	uint64_t bitrate_acc;
};


int playlist_new(struct media_playlist **plp, const struct client *cli,
		 const char *filename);
int playlist_start(struct media_playlist *pl);

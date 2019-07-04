/**
 * @file dashperf.h Dash Performance client -- interface
 *
 * Copyright (C) 2019 Creytiv.com
 */


#define MAX_PLAYLISTS 2


/*
 * Client
 */

struct client;

typedef void (client_error_h)(struct client *cli, int err, void *arg);

int  client_alloc(struct client **clip, const char *uri,
		  client_error_h *errorh, void *arg);
int  client_start(struct client *cli);
void client_close(struct client *cli, int err);
bool client_connected(const struct client *cli);
int64_t client_conn_time(const struct client *cli);
struct media_playlist * const *client_playlists(const struct client *cli);
struct http_cli *client_http_cli(const struct client *cli);
const struct pl *client_path(const struct client *cli);


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


/*
 * Utils
 */

int dns_init(struct dnsc **dnsc);

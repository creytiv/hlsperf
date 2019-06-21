

struct client;


typedef void (client_error_h)(int err, void *arg);


int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri,
		 client_error_h *errorh, void *arg);
int client_start(struct client *cli);

//

struct client;

int client_alloc(struct client **clip, struct dnsc *dnsc, const char *uri);
int client_start(struct client *cli);

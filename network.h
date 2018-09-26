#ifndef NETWORK_H
#define NETWORK_H

/* modes */
#define SERVER	0
#define CLIENT	1

#include "buffer.h"

struct network_conf {
	int mode;
	int verbose;
	char ip_address[16];
	int port;
	int buffer_lines;
	int	buffer_length;
	int log;
	int buffer_access_mode;
	struct timespec timeout;
	struct timespec transmit_delay;
	struct timespec reconnect_delay;
	void* thread;
};
typedef struct network_conf network_conf;
typedef struct network_conf *network_conf_pt;

struct network {
	buffer_pt in;
	buffer_pt out;
	network_conf_pt conf;
};
typedef struct network network;
typedef struct network *network_pt;

network_conf_pt default_conf();
network_pt network_init_mode(char *ip, int port, int mode);
network_pt network_init_client(char *ip, int port);
network_pt network_init_server(int port);
network_pt network_init(network_conf_pt conf);
void network_destroy(network_pt network);
#endif

/*
 * port_guard.c
 *
 *  Created on: Aug 5, 2017
 *      Author: sting
 */
#define _GNU_SOURCE
#include <signal.h>
#include "network.h"

#define MASK 1
#define UNMASK 2
#define REVERSED_MASK 2

struct thread_param {
	char* mask;
	int mask_mode;
    buffer_pt pop;
    buffer_pt push;
    pthread_t self;
};
typedef struct thread_param thread_param;
typedef struct thread_param *thread_param_pt;


/*signal*/
void sig_handler(int signo);
/*threads*/
void *worker(void *param);

pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv, char **envp) {

	int exit = 0;
	int arg = 0;
	char c = '\0';
	int mask_mode = 0;
	char* mask;

	network_conf_pt server_conf = default_conf();
	server_conf->mode = SERVER;
	strcpy(server_conf->ip_address,"ANY");
	server_conf->port = 1234;
	network_conf_pt client_conf = default_conf();
	client_conf->mode = CLIENT;
	strcpy(client_conf->ip_address, "127.0.0.1");
	client_conf->port = 4444;

	while ((c = getopt(argc, argv, "Lvhl:s:D:d:S:t:T:c:M:m")) != -1) {
		if (exit++ > 10) {
			break;
		}
		switch (c) {
		case 'l':
			arg = atoi(optarg);
			if (arg > 0) {
				server_conf->buffer_lines = arg;
				client_conf->buffer_lines = arg;
			} else
				printf("using default buffer line count");
			break;

		case 'c':
			arg = atoi(optarg);
			if (arg > 0) {
				server_conf->buffer_length = arg;
				client_conf->buffer_length = arg;
			} else
				printf("using default buffer line length");
			break;

		case 'M':
			mask = calloc(strlen(optarg)+1,sizeof(char));
			mask_mode = 1;
			strcpy(mask, optarg);
			break;

		case 'm':
			mask_mode = REVERSED_MASK;
			break;

		case 's':
			arg = atoi(optarg);
			if (arg > 0) {
				server_conf->port = arg;
			} else
				printf("using default server port");
			break;

		case 'd':
			arg = atoi(optarg);
			if (arg > 0) {
				client_conf->port = arg;
			} else
				printf("using default client port");
			break;

		case 'S':
			strcpy(server_conf->ip_address, optarg);
			break;

		case 'D':
			strcpy(client_conf->ip_address, optarg);
			break;

		case 'v':
			server_conf->verbose = 1;
			client_conf->verbose = 1;
			break;

		case 't':
			arg = atoi(optarg);
			if (arg > 0) {
				client_conf->reconnect_delay.tv_nsec = arg;
				server_conf->reconnect_delay.tv_nsec = arg;
			} else
				printf("using default time to reconnect");
			break;

		case 'T':
			arg = atoi(optarg);
			if (arg > 0) {
				client_conf->transmit_delay.tv_sec = arg / 1000;
				client_conf->transmit_delay.tv_nsec = (arg % 1000) * 1000000;

				server_conf->transmit_delay.tv_sec = arg / 1000;
				server_conf->transmit_delay.tv_nsec = (arg % 1000) * 1000000;
			} else
				printf("using default time to delay");
			break;

		case 'h':
			printf("\nPORT GUARD\n");
			printf(
					"created\t18.11.2016\nby\tJohannes Waidner\n\tmr.strohzwei@web.de\n\n");
			printf("usage:\n");
			printf("-s\t server port\t\t\t(external default=%d)\n",
					server_conf->port);
			printf("-S\t server ip\t\t\t(default=ANY)\n");
			printf("-d\t client port\t\t\t(internal default=%d)\n",
					client_conf->port);
			printf("-D\t client ip\t\t\t(default=127.0.0.1)\n");
			printf("-l\t buffer lines\t\t\t(default=%d)\n",
					server_conf->buffer_lines);
			printf("-c\t buffer line length\t\t(default=%d chars)\n",
					server_conf->buffer_length);
			printf("-v\t verbose output\t\t\t(default=%d)\n",
					server_conf->verbose);

			printf("-T\t delay between sends\t\t(default=%ldms)\n",
					server_conf->transmit_delay.tv_sec * 1000
							+ server_conf->transmit_delay.tv_nsec * 1000000);
			printf(
					"-t\t time to wait to reconnect\t(internal default=%ldnsec)\n\n",
					server_conf->reconnect_delay.tv_nsec);
			return 0;
		}
	}
	signal(SIGINT, sig_handler);

	network_pt server_net = network_init(server_conf);
	network_pt client_net = network_init(client_conf);

	thread_param_pt t0 = malloc(sizeof(*t0));
	thread_param_pt t1 = malloc(sizeof(*t1));


	t0->mask_mode = 0;
	t1->mask_mode = 0;

	if (mask_mode != 0){
		t0->mask = mask;
		t1->mask = mask;

		t0->mask_mode = UNMASK;
		t1->mask_mode = MASK;

		if (mask_mode == REVERSED_MASK){
			t0->mask_mode = MASK;
			t1->mask_mode = UNMASK;
		}
	}

	t0->pop = server_net->in;
	t0->push = client_net->out;

	t1->pop = client_net->in;
	t1->push = server_net->out;

	pthread_create(&t0->self, NULL, worker, t0);
	pthread_create(&t1->self, NULL, worker, t1);

	pthread_mutex_lock(&exit_mutex);
	pthread_mutex_lock(&exit_mutex);

	push(server_net->in,"",0,NONBLOCKED);
	push(server_net->out,"",0,NONBLOCKED);

	push(client_net->in,"",0,NONBLOCKED);
	push(client_net->out,"",0,NONBLOCKED);

	pthread_join(t0->self, NULL);
	pthread_join(t1->self, NULL);

	network_destroy(server_net);
	network_destroy(client_net);

	if (mask_mode != 0) free(mask);
	free(t0);
	free(t1);
}
void *worker(void *param){
	 thread_param_pt args = (thread_param_pt) param;
	 pthread_setname_np(args->self, "worker");
	 int mask_offset = 0;
	 if (args->mask_mode != 0) mask_offset = strlen(args->mask);
	 void* data = calloc(args->pop->size+mask_offset, sizeof(char));
	 void* tmp = calloc(args->pop->size+mask_offset, sizeof(char));
	 int readed;
	 while ((readed = pop(args->pop,data,BLOCKED)) > 0){
		 if (args->mask_mode == MASK){
			 memcpy(tmp,args->mask,mask_offset);
			 tmp += mask_offset * sizeof(char);
			 memcpy(tmp,data,args->pop->size);
			 tmp -= mask_offset * sizeof(char);
			 push(args->push,tmp,readed+mask_offset,BLOCKED);
			 continue;
		 }else if(args->mask_mode == UNMASK){
			 data += mask_offset * sizeof(char);
			 push(args->push,data,readed,BLOCKED);
			 data -= mask_offset * sizeof(char);
			 continue;
		 }
		 push(args->push,data,readed,BLOCKED);
	 }
	 free(data);
	 free(tmp);
	 pthread_exit((void*) exit);
}

void sig_handler(int signo) {
	pthread_mutex_unlock(&exit_mutex);
}

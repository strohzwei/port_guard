/*
* port_guard.c
*
*  Created on: 18.11.2016
*      Author: Johannes Waidner
*      E-Mail: Mr.strohzwei@web.de
*      TODO:
*      TEST TIME w8
*
*
*#EXAMPEL:


int main(int argc, char **argv, char **envp) {
//char *ip = "127.0.0.1";
int port = 9999;
//network_pt net = network_init_client(ip,port);
network_pt net = network_init_server(port);
net->conf.verbose = 1;
int count = 10;
while (count-- != 0){
	char *output = (char*) calloc(net->in->size, sizeof(char));
	if (pop(net->in,output,BLOCKED) == 0)
		{printf("%s",output);
	}
	push(net->out, output, sizeof(output),BLOCKED);
	free(output);
}
network_destroy(net);
return 0;
}

*
*/

#define _GNU_SOURCE
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
/* ... parameter */
#include <stdarg.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#include <syslog.h>

#include "network.h"

#define DEFAULT_CONF_BUFFER_LINES 255
#define DEFAULT_CONF_BUFFER_LENGTH 1024
#define DEFAULT_CONF_LOG 0
#define DEFAULT_CONF_TRANSMIT_DELAY 0
#define DEFAULT_CONF_RECONNECT_DELAY 1000 //ns
#define DEFAULT_CONF_VERBOSE 0
#define DEFAULT_CONF_TCP_TIMOUT_SEC 1
#define DEFAULT_CONF_TCP_TIMOUT_NSEC 0
#define DEFAULT_CONF_BUFFER_ACCESS_MODE BLOCKED

#define TEXT_SRC_PORT_NAME "server_monitor"
#define TEXT_DST_PORT_NAME "client_monitor"
#define TEXT_DEFAULT_SEND_NAME "send"
#define TEXT_DEFAULT_RECV_NAME "recv"
#define TEXT_DST_SEND_NAME "client_send"
#define TEXT_DST_RECV_NAME "client_recv"
#define TEXT_SRC_SEND_NAME "server_send"
#define TEXT_SRC_RECV_NAME "server_recv"

struct thread_param {
    int en;
    int debug;
    int socket;
    char ip[15];
    pthread_t self;
    buffer_pt buffer;
    buffer_pt buffer_out;
    int buffer_access_mode;
    struct timespec send_delay;
    struct timespec tcp_timeout;
    struct timespec reconnect_delay;
};
typedef struct thread_param thread_param;
typedef struct thread_param *thread_param_pt;

/*threads*/
void *send_from_buffer(void *param);
void *recv_to_buffer(void *param);
void *source(void *param);
void *destination(void *param);


/*macros*/
void thread_send_recv_handler(thread_param_pt send_param,
    thread_param_pt recv_param, thread_param_pt own_param, int sock,
    char *fake_buffer, int thread_type);
void prepare_thread_param(thread_param_pt send_param,
    thread_param_pt recv_param, thread_param_pt own_param);




void *source(void *param) {
    thread_param_pt args = (thread_param_pt) param;

    int c;
    int socket_desc;
    int client_sock;
    char fake_buffer[args->buffer->size];
    struct sockaddr_in server, client;
    struct timespec timeout = args->tcp_timeout;

    thread_param_pt recv_param = malloc(sizeof(*recv_param));
    thread_param_pt send_param = malloc(sizeof(*send_param));

    pthread_setname_np(args->self, TEXT_SRC_PORT_NAME);

    //socket
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        perror("network: on source socket create failed. Error");
        free(recv_param);
        free(send_param);

        pthread_exit((void*) exit);
    }

    //sockaddr_in structure
    server.sin_family = AF_INET;
    if (strcmp(args->ip,"ANY") != 0){
        server.sin_addr.s_addr = inet_addr(args->ip);
    }else{
        server.sin_addr.s_addr = INADDR_ANY;
    }
    server.sin_port = htons(args->socket);

    //bind
    while (bind(socket_desc, (struct sockaddr *) &server, sizeof(server)) < 0) {
    	char *msg = (char*)malloc(13 * sizeof(char));
    	sprintf(msg, "network: Can not bin %s:%d. Error", args->ip,args->socket);
        perror(msg);
        free(msg);
        sleep(2);
    }
    struct timespec delay_used;
    prepare_thread_param(send_param, recv_param, args);
    setsockopt (socket_desc, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(timeout));
    while (args->en) {
        listen(socket_desc, 10);

        c = sizeof(struct sockaddr_in);
        args->socket = socket_desc;
        client_sock = accept(socket_desc, (struct sockaddr *) &client,
            (socklen_t*) &c);
        if (client_sock < 0) {
            perror("network: on source accept failed.");
            while(nanosleep(&args->reconnect_delay,&delay_used)==-1)continue;
            continue;
        }
        setsockopt (client_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(timeout));
        args->socket = client_sock;
        thread_send_recv_handler(send_param, recv_param, args, client_sock,
            fake_buffer, 1);

    }
    free(recv_param);
    free(send_param);

    if (fcntl(socket_desc, F_GETFL) != -1 || errno != EBADF){
        close(socket_desc);
    }
    pthread_exit((void*) exit);
}

void *destination(void *param) {
    thread_param_pt args = (thread_param_pt) param;

    int sock;
    struct sockaddr_in server;
    char fake_buffer[args->buffer->size];
    thread_param_pt recv_param = malloc(sizeof(*recv_param));
    thread_param_pt send_param = malloc(sizeof(*send_param));

    pthread_setname_np(args->self, TEXT_DST_PORT_NAME);

    //own port
    server.sin_addr.s_addr = inet_addr(args->ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(args->socket);

    struct timespec  timeout = args->reconnect_delay;
    struct timespec delay_used;

    prepare_thread_param(send_param, recv_param, args);

    while (args->en) {
        //socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            perror("network: on destination could not create socket");
            free(recv_param);
            free(send_param);

            pthread_exit((void*) exit);
        }

        //connect
        args->socket = sock;
        if (connect(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
            //refused
            close(sock);
            while(nanosleep(&args->reconnect_delay,&delay_used)==-1)continue;
            continue;
        }
        //TODO
        setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,sizeof(timeout));
        args->socket = sock;

        thread_send_recv_handler(send_param, recv_param, args, sock,
            fake_buffer, 2);
    }
    free(recv_param);
    free(send_param);
    pthread_exit((void*) exit);
}

void prepare_thread_param(thread_param_pt send_param,
    thread_param_pt recv_param, thread_param_pt own_param) {
        send_param->en = 1;
        send_param->debug = own_param->debug;
        send_param->buffer = own_param->buffer_out;
        send_param->send_delay = own_param->send_delay;
        send_param->buffer_access_mode = own_param->buffer_access_mode;

        recv_param->en = 1;
        recv_param->debug = own_param->debug;
        recv_param->buffer = own_param->buffer;
        recv_param->buffer_access_mode = own_param->buffer_access_mode;
}

void thread_send_recv_handler(thread_param_pt send_param,
    thread_param_pt recv_param, thread_param_pt own_param, int sock,
    char *fake_buffer, int thread_type) {

        send_param->socket = sock;
        recv_param->socket = sock;
        pthread_create(&send_param->self, NULL, send_from_buffer, send_param);
        pthread_create(&recv_param->self, NULL, recv_to_buffer, recv_param);

        if (thread_type == 1) {
            pthread_setname_np(send_param->self, TEXT_SRC_SEND_NAME);
            pthread_setname_np(recv_param->self, TEXT_SRC_RECV_NAME);

        } else if (thread_type == 2) {
            pthread_setname_np(send_param->self, TEXT_DST_SEND_NAME);
            pthread_setname_np(recv_param->self, TEXT_DST_RECV_NAME);
        } else {
            pthread_setname_np(send_param->self, TEXT_DEFAULT_SEND_NAME);
            pthread_setname_np(recv_param->self, TEXT_DEFAULT_RECV_NAME);
        }

        //monitor socket for closed
        while (recv(sock, fake_buffer, sizeof(fake_buffer),
            MSG_PEEK) != 0){
                usleep(1000);
        }

        send_param->en = 0;
        recv_param->en = 0;

        usleep(1000);

        push(own_param->buffer, " ",0,NONBLOCKED);
        push(own_param->buffer_out, " ",0,NONBLOCKED);

        pthread_join(send_param->self, NULL);
        pthread_join(recv_param->self, NULL);


        close(sock);

        send_param->en = 1;
        recv_param->en = 1;
}

void *recv_to_buffer(void *param) {

    thread_param_pt args = (thread_param_pt) param;
    buffer_pt buffer = args->buffer;
    int read_size;
    char *client_message = (char*) calloc(buffer->size, sizeof(char));
    //monitor socket
    while ((read_size = recv(args->socket, client_message, buffer->size, 0))
        != 0) {

			if (args->debug == 1) {
				printf("<--:\t");
				print_buffer_data_as_string(buffer,client_message);
				printf("\n\t");
				print_buffer_data_as_hex(buffer,client_message);
				printf("\n");
			}

            push(buffer, client_message, read_size, args->buffer_access_mode);

            free(client_message);
            client_message = (char*) calloc(buffer->size, sizeof(char));

            if (read_size == 0) {
                //disconnected
                fflush(stdout);
            } else if (read_size == -1) {
                perror("network: recv failed");
            }
    }
    free(client_message);
    //signal_changes(buffer);
    pthread_exit((void*) exit);
}


void *send_from_buffer(void *param) {

    thread_param_pt args = (thread_param_pt) param;
    buffer_pt buffer = args->buffer;
    int read_size = -1;
    char *output = (char*) calloc(buffer->size, sizeof(char));
    struct timespec send_delay_used;
    //monitor socket
    while (args->en) {
        read_size = pop(buffer, output, args->buffer_access_mode);

        if (read_size > 0) {
            int send_result = send(args->socket, output, read_size, MSG_NOSIGNAL);
            if (send_result >= 0) {

				if (args->debug == 1) {
					printf("-->:\t");
					print_buffer_data_as_string(buffer,output);
					printf("\n\t");
					print_buffer_data_as_hex(buffer,output);
					printf("\n");
				}

                while(nanosleep(&args->send_delay,&send_delay_used)==-1)continue;
            } else {
                switch (errno) {
                case EPIPE:
                    perror("network: send failed write back to buffer. Error");
                    push(buffer,output ,read_size ,args->buffer_access_mode);
                    break;
                default:
                    perror("network: send failed write back to buffer. Error");
                    break;
                }
                free(output);
                //signal_changes(buffer);
                pthread_exit((void*) exit);
            }
        }
    }
    free(output);
    //signal_changes(buffer);
    pthread_exit((void*) exit);
}

network_conf_pt default_conf(){
	network_conf_pt conf = malloc(sizeof(*conf));

	conf->buffer_access_mode = DEFAULT_CONF_BUFFER_ACCESS_MODE;
	conf->buffer_length = DEFAULT_CONF_BUFFER_LENGTH;
	conf->buffer_lines = DEFAULT_CONF_BUFFER_LINES;
	conf->log = DEFAULT_CONF_LOG;
	conf->verbose = DEFAULT_CONF_VERBOSE;

	struct timespec reconnect_delay;
	reconnect_delay.tv_sec = 0;
	reconnect_delay.tv_nsec = DEFAULT_CONF_RECONNECT_DELAY;
	conf->reconnect_delay = reconnect_delay;

	struct timespec transmit_delay;
	transmit_delay.tv_sec = 0;
	transmit_delay.tv_nsec = 0;
	conf->transmit_delay = transmit_delay;

	struct timespec tcp_timeout;
	tcp_timeout.tv_sec = DEFAULT_CONF_TCP_TIMOUT_SEC;
	tcp_timeout.tv_nsec = DEFAULT_CONF_TCP_TIMOUT_NSEC;
	conf->timeout = tcp_timeout;

	return conf;
}


network_pt network_init_client(char *ip, int port){


	network_conf_pt conf = default_conf();
	strncpy(conf->ip_address,ip,15);
	conf->port = port;
	conf->mode = CLIENT;
	return network_init(conf);
}

network_pt network_init_server(int port){

	network_conf_pt conf = default_conf();
	strcpy(conf->ip_address,"ANY");
	conf->port = port;
	conf->mode = SERVER;

	return network_init(conf);
}

network_pt network_init_mode(char *ip, int port, int mode){

	network_conf_pt conf = default_conf();
	strncpy(conf->ip_address,ip,15);
	conf->port = port;
	conf->mode = mode;

	return network_init(conf);
}

network_pt network_init(network_conf_pt conf){


	network_pt network = malloc(sizeof(*network));
	buffer_pt buffer_in = malloc(sizeof(buffer));
	buffer_pt buffer_out = malloc(sizeof(buffer));
	thread_param_pt param = malloc(sizeof(*param));
	network->conf = conf;
	network->conf->thread = (void*) param;

	param->en = 1;
	param->debug = conf->verbose;

	strcpy(param->ip,conf->ip_address);
	param->socket = conf->port;

	buffer_in->size =conf->buffer_length;
	buffer_out->size =conf->buffer_length;

	buffer_in->lines =conf->buffer_lines;
	buffer_out->lines =conf->buffer_lines;

	param->buffer = buffer_in;
	param->buffer_out = buffer_out;
	param->buffer_access_mode = conf->buffer_access_mode;

	param->send_delay = conf->transmit_delay;
	param->reconnect_delay = conf->reconnect_delay;
	param->tcp_timeout = conf->timeout;

buffer_init(buffer_in);
	    buffer_init(buffer_out);

	    if (network->conf->mode == CLIENT){
			pthread_create(&param->self, NULL, destination, param);
		}else{
			pthread_create(&param->self, NULL, source, param);
		}
	    network->in = buffer_in;
	    network->out = buffer_out;

    return network;
}

void network_destroy(network_pt network){

	thread_param_pt param = network->conf->thread;
	buffer_pt in = network->in;
	buffer_pt out = network->out;

	param->en = 0;

	push(in, " ", 0, NONBLOCKED);
	push(out, " ", 0, NONBLOCKED);

	shutdown(param->socket, SHUT_RDWR);
	pthread_join(param->self, NULL);

	buffer_destroy(in);
	buffer_destroy(out);

	free(in);
	free(out);
	free(network->conf);
	free(param);
	free(network);
	return;

}

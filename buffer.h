/*
* buffer.h
*
*  Created on: 18.11.2016
*      Author: Johannes Waidner
*      E-Mail: Mr.strohzwei@web.de
*/
#ifndef BUFFER_H
#define BUFFER_H

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>

/* states  */
#define READABLE   4
#define WRITEABLE  6
#define RDLOCK    -4
#define WRLOCK    -6
#define RDBUSY	  -3
#define WRBUSY	  -2
#define LENGTH_ERROR -8
#define EMPTY	-10
#define FULL	-11

/*  */
#define NONBLOCKED 0
#define BLOCKED 1

struct buffer {
	int tmp;
	int size;
	int lines;
	int data_count;
	char **data;
	int *line_status;
	int *line_usage;
	pthread_mutex_t *line_rdmutex;
	pthread_mutex_t *line_wrmutex;
	pthread_cond_t change_condition;
	pthread_mutex_t change_cv_mutex;
};
typedef struct buffer buffer;
typedef struct buffer *buffer_pt;

/* buffer */
void buffer_init(buffer_pt buffer);
void buffer_destroy(buffer_pt buffer);

int buffer_line_to_output(buffer_pt buffer, int buffer_line, void *output, int option);
int input_to_buffer_line(buffer_pt buffer, int buffer_line, void *input, int size, int option);

void wait_for_changes(buffer_pt buffer);
void signal_changes(buffer_pt buffer);

int push(buffer_pt buffer, void* data ,int size, int option);
int pop(buffer_pt buffer, void* data, int option);

void print_buffer_data_as_string(buffer_pt buffer, void* data);
void print_buffer_data_as_hex(buffer_pt buffer, void* data);
int buffer_data_len(buffer_pt buffer, void* data);
#endif

/*
* buffer.c
*
*  Created on: 18.11.2016
*      Author: Johannes Waidner
*      E-Mail: Mr.strohzwei@web.de
*/
#include "buffer.h"

int pop(buffer_pt buffer, void* data, int option){
    int read_size = 0;
    int line_buffer = 0;
    while ((read_size = buffer_line_to_output(buffer, line_buffer, data, option)) < 0) {
        if (read_size == EMPTY) {
            return EMPTY;
        }
        line_buffer++;
        if (line_buffer > buffer->lines) {
            line_buffer = 0;
        }
    }
    return read_size;
}

int buffer_data_len(buffer_pt buffer, void* data){
    return buffer->size;
}

void print_buffer_data_as_hex(buffer_pt buffer, void* data){
    int i;
    for (i=0;i<buffer_data_len(buffer,data);i++){
        printf("%x ",((char*)data)[i]& 0xff);
        if (((char*)data)[i] == '\0') break;
    }
    printf("len: %d",--i);
}

void print_buffer_data_as_string(buffer_pt buffer, void* data){
    int i;
    for (i=0;i<buffer_data_len(buffer,data);i++){
        printf("%c",((char*)data)[i]);
        if (((char*)data)[i] == '\0') break;
    }
    printf("len: %d",--i);
}

void wait_for_changes(buffer_pt buffer) {
    pthread_mutex_lock(&buffer->change_cv_mutex);
    pthread_cond_broadcast(&buffer->change_condition);
    pthread_cond_wait(&buffer->change_condition, &buffer->change_cv_mutex);
    pthread_mutex_unlock(&buffer->change_cv_mutex);
}

void signal_changes(buffer_pt buffer) {
    pthread_mutex_lock(&buffer->change_cv_mutex);
    pthread_cond_broadcast(&buffer->change_condition);
    pthread_mutex_unlock(&buffer->change_cv_mutex);
}

int push(buffer_pt buffer, void* data ,int size, int option) {
    int test;
    int line_buffer = 0;
    if (size > buffer->size){
    	return LENGTH_ERROR;
    }
    while ((test = input_to_buffer_line(buffer, line_buffer, data, size, option)) < 0) {
        line_buffer++;
        if (line_buffer >= buffer->lines) {
            line_buffer = 0;
        }
        if (test == FULL) {
            return FULL;
        } else if (test == LENGTH_ERROR) {
            return LENGTH_ERROR;
        }
    }
    return 0;
}

int input_to_buffer_line(buffer_pt buffer, int buffer_line, void *input ,int size, int option) {

    if (buffer_line > buffer->lines || buffer_line < 0) {
        buffer_line = 0;
    }

    pthread_mutex_lock(&buffer->change_cv_mutex);

    if (buffer->data_count >= buffer->lines) {
        if (option == BLOCKED){
			pthread_cond_wait(&buffer->change_condition, &buffer->change_cv_mutex);
			pthread_mutex_unlock(&buffer->change_cv_mutex);
			return input_to_buffer_line(buffer,buffer_line,input,size,option);
        }else{
        pthread_mutex_unlock(&buffer->change_cv_mutex);
        return FULL;
        }
    }
    pthread_mutex_unlock(&buffer->change_cv_mutex);

    if (pthread_mutex_trylock(&buffer->line_rdmutex[buffer_line]) != EBUSY) {
        if (buffer->line_status[buffer_line] == WRITEABLE) {
            memcpy(buffer->data[buffer_line], input, size);
            buffer->line_usage[buffer_line] = size;
            buffer->line_status[buffer_line] = READABLE;

            pthread_mutex_unlock(&buffer->line_rdmutex[buffer_line]);
            pthread_mutex_lock(&buffer->change_cv_mutex);
            buffer->data_count++;
            pthread_cond_signal(&buffer->change_condition);
            pthread_mutex_unlock(&buffer->change_cv_mutex);

            return 0;
        }
        pthread_mutex_unlock(&buffer->line_rdmutex[buffer_line]);
        return WRLOCK;
    }
    return RDBUSY;
}

int buffer_line_to_output(buffer_pt buffer, int buffer_line, void *output, int option) {
    if (buffer->lines < buffer_line || buffer_line < 0) {
        return LENGTH_ERROR;
    }

    pthread_mutex_lock(&buffer->change_cv_mutex);
    if (buffer->data_count <= 0) {
    	if (option == BLOCKED){
			pthread_cond_wait(&buffer->change_condition, &buffer->change_cv_mutex);
			pthread_mutex_unlock(&buffer->change_cv_mutex);
			return buffer_line_to_output(buffer,buffer_line,output,option);
    	 }else{
        pthread_mutex_unlock(&buffer->change_cv_mutex);
        return EMPTY;
    	}
    }
    pthread_mutex_unlock(&buffer->change_cv_mutex);

    if (pthread_mutex_trylock(&buffer->line_rdmutex[buffer_line]) != EBUSY) {

        if (buffer->line_status[buffer_line] == READABLE) {
        	buffer->tmp = buffer->line_usage[buffer_line];
            memcpy(output, buffer->data[buffer_line], buffer->line_usage[buffer_line]);
            buffer->line_status[buffer_line] = WRITEABLE;
            pthread_mutex_unlock(&buffer->line_rdmutex[buffer_line]);
            pthread_mutex_lock(&buffer->change_cv_mutex);
            buffer->data_count--;
            pthread_cond_signal(&buffer->change_condition);
            pthread_mutex_unlock(&buffer->change_cv_mutex);
            return buffer->tmp;
        }
        pthread_mutex_unlock(&buffer->line_rdmutex[buffer_line]);
        pthread_mutex_lock(&buffer->change_cv_mutex);
        pthread_cond_signal(&buffer->change_condition);
        pthread_mutex_unlock(&buffer->change_cv_mutex);
        return RDLOCK;
    }
    return RDBUSY;
}

void buffer_destroy(buffer_pt buffer) {

    int i;
    for (i = 0; i < buffer->lines; i++) {
        pthread_mutex_destroy(&buffer->line_rdmutex[i]);
        pthread_mutex_destroy(&buffer->line_wrmutex[i]);
        free(buffer->data[i]);

    }
    free(buffer->data);
    free(buffer->line_usage);
    free(buffer->line_status);
    free(buffer->line_rdmutex);
    free(buffer->line_wrmutex);
}

void buffer_init(buffer_pt buffer) {
    int i;

    buffer->line_status = (int*) calloc(buffer->lines + 1, sizeof(int));
    if (buffer->line_status == NULL) {
        perror("buffer_init calloc line_status ");
    }

    buffer->line_usage = (int*) calloc(buffer->lines + 1, sizeof(int));
        if (buffer->line_usage == NULL) {
            perror("buffer_init calloc line_status ");
        }

    buffer->line_rdmutex = (pthread_mutex_t*) calloc(buffer->lines + 1,
        sizeof(pthread_mutex_t));
    if (buffer->line_rdmutex == NULL) {
        perror("buffer_init malloc line_rdmutex ");
    }

    buffer->line_wrmutex = (pthread_mutex_t*) calloc(buffer->lines + 1,
        sizeof(pthread_mutex_t));
    if (buffer->line_wrmutex == NULL) {
        perror("buffer_init malloc line_wrmutex ");
    }

    pthread_cond_init(&buffer->change_condition, NULL);
    pthread_mutex_init(&buffer->change_cv_mutex, NULL);

    buffer->data = malloc(sizeof(buffer) * buffer->lines);
    if (buffer->data == NULL) {
        perror("buffer_init malloc data");
    }
    for (i = 0; i < buffer->lines; i++) {
        pthread_mutex_init(&buffer->line_rdmutex[i], NULL);
        pthread_mutex_init(&buffer->line_wrmutex[i], NULL);
        buffer->data[i] = calloc(buffer->size, sizeof(char));
        if (buffer->data[i] == NULL) {
            perror("buffer_init calloc data[]");
        }
        buffer->line_status[i] = WRITEABLE;
        buffer->line_usage[i] = 0;
    }
    buffer->data_count = 0;

}

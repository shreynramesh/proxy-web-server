#ifndef SAFEPQUEUE_H
#define SAFEPQUEUE_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

struct http_request {
    char *method;
    char *path;
    char* request;
    int delay;
    int request_length;
    int client_fd;
};


typedef struct node {
    int priority;
    struct http_request* hr;
} node;

typedef struct queue {
    int capacity;
    int length;
    node* heap;

    pthread_mutex_t lock;
    pthread_cond_t empty;
} queue;

int create_queue(queue* q, int capacity);
int add_work(queue* q, struct http_request* hr);
struct http_request* get_work_nonblocking(queue* q);
struct http_request* get_work(queue* q);
void free_queue(queue *q);
void print_queue(queue *q);

#endif


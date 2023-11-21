#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "proxyserver.h"

void print_and_flush(const char *format, ...) {
    va_list args;

    // Print using the format string and variable arguments
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    // Flush the standard output
    fflush(stdout);
}

int create_queue(queue* q, int capacity) {
    q->capacity = capacity;
    q->length = -1;

    q->heap = (node*) malloc(sizeof(node) * capacity);
    if(q->heap == NULL) {
        free(q);
        return -1;
    }

    for(int i = 0; i < capacity; i++) {
        q->heap[i].hr = NULL;
        q->heap[i].priority = -1;
    }

    pthread_mutex_init(&(q->lock), NULL);
    pthread_cond_init(&(q->empty), NULL);

    return 0;
}

void cpy_hr(struct http_request* src, struct http_request* dest) {
    // print_and_flush("DEBUG: %p %s %s %d %p\n", src, src->method, src->path, *(src->delay), dest);
    dest->method = strdup(src->method);
    dest->path = strdup(src->path);
    dest->request = strdup(src->request);
    dest->delay = src->delay;
    dest->request_length = src->request_length;
    dest->client_fd = src->client_fd;
}

void free_hr(struct http_request* hr) {
    free(hr->method);
    free(hr->path);
    free(hr->request);
    free(hr);
}

void cpy_node(node* src, node* dest) {
    dest->priority = src->priority;
    cpy_hr(src->hr, dest->hr);
    
}

int find_priority (const char* path) {
    int slashCount = 0;
    const char *currentChar = path;

    while (*currentChar) {
        if (*currentChar == '/') {
            slashCount++;

            // Check if it's the 1st slash
            if (slashCount == 1) {
                currentChar++; // Move to the character after the 4th slash

                // Extract the number until the next slash or the end of the string
                int number = 0;
                while (*currentChar >= '0' && *currentChar <= '9') {
                    number = number * 10 + (*currentChar - '0');
                    currentChar++;
                }

                return number;
            }
        } else {
            currentChar++;
        }
    }

    return -1;
}


// Retrurns index of a given node's parent
int parent(int i) {
    return (i - 1) / 2;
}

// Returns index of a given node's left child
int leftChild(int i) {
 
    return ((2 * i) + 1);
}
 
// Returns index of a given node's lefrightt child
int rightChild(int i) {
    return ((2 * i) + 2);
}

// Swapping values of 2 nodes in an array
void swap(node* a, node* b) {
    node* tmp = (node*) malloc(sizeof(node));
    tmp->hr = (struct http_request*) malloc(sizeof(struct http_request));
    tmp->priority = -1;

    cpy_node(a, tmp);
    cpy_node(b, a);
    cpy_node(tmp, b);

    free(tmp);
}

// Shift node up heap to maintain priority relations
void shiftUp(queue* q, int i)
{
    while (i > 0 && q->heap[parent(i)].priority < q->heap[i].priority) {
 
        // Swap parent and current node
        swap(&(q->heap[parent(i)]), &(q->heap[i]));
 
        // Update i to parent of i
        i = parent(i);
    }
}

// Shift node down heap to maintain priority relations
void shiftDown(queue* q, int i) {
    int maxIndex = i;
 
    // Left Child
    int l = leftChild(i);
 
    if (l <= q->length && q->heap[l].priority > q->heap[maxIndex].priority) {
        maxIndex = l;
    }
 
    // Right Child
    int r = rightChild(i);
 
    if (r <= q->length && q->heap[r].priority > q->heap[maxIndex].priority) {
        maxIndex = r;
    }
 
    // If i not same as maxIndex
    if (i != maxIndex) {
        swap(&(q->heap[i]), &(q->heap[maxIndex]));
        shiftDown(q, maxIndex);
    }
}

// Inserting a new request to the queue
int add_work(queue* q, struct http_request* hr) {  
    print_and_flush("DEBUG: Inside add_work\n"); 
    int priority;
    
    // Creating new node and putting in queue
    print_and_flush("add_work: Acquiring Lock\n");
    pthread_mutex_lock(&(q->lock));
    print_and_flush("add_work: Obtained Lock\n");
    
    // Validating queue size
    if(q->length == q->capacity - 1) {
        pthread_mutex_unlock(&(q->lock));
        return QUEUE_FULL;
    }
    
    q->length++;
    print_and_flush("DEBUG: %s %s %d\n", hr->method, hr->path, q->length);

    (q->heap[q->length]).hr = malloc(sizeof(struct http_request));
    cpy_hr(hr, (q->heap[q->length]).hr);
    print_and_flush("DEBUG: %p\n", q);
    priority = find_priority(hr->path);    
    print_and_flush("DEBUG: %d\n", priority);
    (q->heap[q->length]).priority = priority;
    print_and_flush("DEBUG: %d\n", priority);

    // DEBUG
    // pthread_mutex_unlock(&(q->lock));
    // print_queue(q);
    // pthread_mutex_lock(&(q->lock));
 
    // Shift up to maintain heap property
    shiftUp(q, q->length);

    pthread_cond_signal(&q->empty);
    print_and_flush("add_work: Tried to wake up worker\n");
    pthread_mutex_unlock(&q->lock);
    print_and_flush("add_work: Freed Lock\n");

    print_and_flush("DEBUG: Returning from add_work\n"); 
    return 0;
}

struct http_request* get_work_nonblocking(queue* q) {
    print_and_flush("get_work_nonblocking: Acquiring Lock\n");
    pthread_mutex_lock(&q->lock);
    print_and_flush("get_work_nonblocking: Obtained Lock\n");

    // Validing queue length
    if(q->length == -1) {
        pthread_mutex_unlock(&q->lock);
        return (struct http_request*) QUEUE_EMPTY;
    }

    struct http_request* res = malloc(sizeof(struct http_request));
    cpy_hr(q->heap[0].hr, res);

    // Replacing value at root with last leaf
    cpy_node(&(q->heap[q->length]), &(q->heap[0]));
    print_and_flush("DEBUG: About to free request %d w/ address %p\n", q->length, q->heap[q->length].hr);
    free_hr(q->heap[q->length].hr);
    q->heap[q->length].hr = NULL;
    q->length--;

    // Shift down to maintain heap property
    shiftDown(q, 0);

    // Returning 0 if queue is empty after dequeue
    // if(q->length == -1) {
    //     pthread_mutex_unlock(&q->lock);
    //     return (struct http_request*) -1;
    // }

    pthread_mutex_unlock(&q->lock);
    print_and_flush("get_work_nonblocking: Freed Lock\n");
    return res;
}

struct http_request* get_work(queue* q) {
    print_and_flush("get_work: Acquiring Lock\n");
    pthread_mutex_lock(&q->lock);
    print_and_flush("get_work: Obtained Lock\n");

    // Validing queue length
    while (q->length == -1) {
        print_and_flush("get_work: Queue empty going to sleep\n");
        pthread_cond_wait(&q->empty, &q->lock);
        print_and_flush("get_work: Woke up and acquired lock\n");
    }

    struct http_request* res = malloc(sizeof(struct http_request));
    cpy_hr(q->heap[0].hr, res);

    // Replacing value at root with last leaf
    cpy_node(&(q->heap[q->length]), &(q->heap[0]));
    print_and_flush("DEBUG: About to free request %d w/ address %p\n", q->length, q->heap[q->length].hr);
    free_hr(q->heap[q->length].hr);
    print_and_flush("Freed\n");
    q->heap[q->length].hr = NULL;
    q->heap[q->length].priority = -1;
    q->length--;

    // Shift down to maintain heap property
    shiftDown(q, 0);

    // Returning 0 if queue is empty after dequeue
    // if(q->length == -1) {
    //     pthread_mutex_unlock(&q->lock);
    //     return (struct http_request*) -1;
    // }

    pthread_mutex_unlock(&q->lock);
    print_and_flush("get_work: Freed Lock\n");
    return res;
}

void free_queue(queue *q) {
    for(int i = 0; i < q->capacity; i++) {
        // print_and_flush("DEBUG: FREE: %p\n", q->heap[i].hr);
        if(q->heap[i].hr != NULL || q->heap[i].priority != -1) {
            free_hr(q->heap[i].hr);
        }
        
    }

    free(q->heap);

    pthread_mutex_destroy(&(q->lock));
    pthread_cond_destroy(&(q->empty));
    free(q);
}

void print_queue(queue *q) {
    print_and_flush("print_queue: Acquiring Lock\n");
    pthread_mutex_lock(&(q->lock));
    print_and_flush("print_queue: Obtained Lock\n");

    print_and_flush("Length: %d", q->length);
    for (int i = 0; i < q->capacity; i++) {
        if(q->heap[i].hr != NULL) {
            print_and_flush("\tPriority: %d\n", q->heap[i].priority);
            print_and_flush("\tMethod: %s\n", q->heap[i].hr->method);
            print_and_flush("\tPath: %s\n", q->heap[i].hr->path);
            print_and_flush("\tClient_fd: %d\n", q->heap[i].hr->client_fd);
            print_and_flush("\tDelay: %d\n", q->heap[i].hr->delay);
            print_and_flush("\tRequest Length: %d\n", q->heap[i].hr->request_length);
            print_and_flush("\tRequest: %.6s\n", q->heap[i].hr->request);
            print_and_flush("\n");   
        }  
    }

    pthread_mutex_unlock(&(q->lock));
    print_and_flush("print_queue: Freed Lock\n");
}
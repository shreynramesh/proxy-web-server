#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "proxyserver.h"

void http_start_response(int fd, int status_code) {
    dprintf(fd, "HTTP/1.0 %d %s\r\n", status_code,
            http_get_response_message(status_code));
}

void http_send_header(int fd, char *key, char *value) {
    dprintf(fd, "%s: %s\r\n", key, value);
}

void http_end_headers(int fd) {
    dprintf(fd, "\r\n");
}

int http_send_string(int fd, char *data) {
    return http_send_data(fd, data, strlen(data));
}

int http_send_data(int fd, char *data, size_t size) {
    ssize_t bytes_sent;
    while (size > 0) {
        bytes_sent = write(fd, data, size);
        if (bytes_sent < 0)
            return -1; // Indicates a failure
        size -= bytes_sent;
        data += bytes_sent;
    }
    return 0; // Indicate success
}

void http_fatal_error(char *message) {
    fprintf(stderr, "%s\n", message);
    exit(ENOBUFS);
}

/*
 * Constants
 */
#define LIBHTTP_REQUEST_MAX_SIZE 8192
#define RESPONSE_BUFSIZE 10000

// struct http_request *http_request_parse(int fd) {
//     struct http_request *request = malloc(sizeof(struct http_request));
//     if (!request) http_fatal_error("Malloc failed");

//     char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
//     if (!read_buffer) http_fatal_error("Malloc failed");

//     int bytes_read = read(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE);
//     read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

//     char *read_start, *read_end;
//     size_t read_size;

//     do {
//         /* Read in the HTTP method: "[A-Z]*" */
//         read_start = read_end = read_buffer;
//         while (*read_end >= 'A' && *read_end <= 'Z') {
//             print_and_flush("%c", *read_end);
//             read_end++;
//         }
//         read_size = read_end - read_start;
//         if (read_size == 0) break;
//         request->method = malloc(read_size + 1);
//         memcpy(request->method, read_start, read_size);
//         request->method[read_size] = '\0';
//         print_and_flush("parsed method %s\n", request->method);

//         /* Read in a space character. */
//         read_start = read_end;
//         if (*read_end != ' ') break;
//         read_end++;

//         /* Read in the path: "[^ \n]*" */
//         read_start = read_end;
//         while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n')
//             read_end++;
//         read_size = read_end - read_start;
//         if (read_size == 0) break;
//         request->path = malloc(read_size + 1);
//         memcpy(request->path, read_start, read_size);
//         request->path[read_size] = '\0';
//         print_and_flush("parsed path %s\n", request->path);

//         /* Read in HTTP version and rest of request line: ".*" */
//         read_start = read_end;
//         while (*read_end != '\0' && *read_end != '\n')
//             read_end++;
//         if (*read_end != '\n') break;
//         read_end++;

//         // Temp Delay init
//         request->delay = malloc(1);
//         request->delay[0] = '\0';

//         free(read_buffer);
//         return request;
//     } while (0);

//     /* An error occurred. */
//     free(request);
//     free(read_buffer);
//     return NULL;
// }

struct http_request* parse_client_request(int client_fd) { 
    struct http_request *hr = malloc(sizeof(struct http_request));
    char* buffer = (char *)malloc(LIBHTTP_REQUEST_MAX_SIZE * sizeof(char));
    int bytes_read = read(client_fd, buffer, LIBHTTP_REQUEST_MAX_SIZE);
    char* buffer_cpy = strdup(buffer);
    // Initializing variables
    hr->method = NULL;
    hr->path = NULL;
    hr->delay = -1;
    hr->request = buffer;
    hr->request_length = bytes_read;
    hr->client_fd = client_fd;
    char* line = strtok(buffer_cpy, "\r\n");
    while(line != NULL) {
    // Parse first line to get HTTP method and URL
        if(hr->method == NULL && hr->path == NULL) {
            hr->method = malloc(strlen(line) + 1);
            hr->path = malloc(strlen(line) + 1);
            sscanf(line, "%s %s", hr->method, hr->path);
            if(strstr(hr->path, "GetJob") != NULL) {
                free(hr->method);
                free(hr->path);
                free(buffer_cpy);
                return NULL;
            }
        }
        // Looking for delay in following lines
        if (strstr(line, "Delay:") != NULL) {
            sscanf(line, "Delay: %d", &(hr->delay));
        }
        // Moving to the next line
        line = strtok(NULL, "\r\n");
    }
    free(buffer_cpy);
    return hr;
}


char *http_get_response_message(int status_code) {
    switch (status_code) {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Internal Server Error";
    }
}


/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;
queue* pq;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(int client_fd) {
    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        print_and_flush("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));

    // forward the client request to the fileserver
    int bytes_read = read(client_fd, buffer, RESPONSE_BUFSIZE);
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);

    if (ret < 0) {
        print_and_flush("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }

    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}

void* work_forever() { 
    int fileserver_fd;   

    while(1) {
        // create a fileserver socket
        fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (fileserver_fd == -1) {
            fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
            exit(errno);
        }

        // create the full fileserver address
        struct sockaddr_in fileserver_address;
        fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
        fileserver_address.sin_family = AF_INET;
        fileserver_address.sin_port = htons(fileserver_port);

        // Removing request from priority queue - sleep if empty
        struct http_request* hr = get_work(pq);

        // Sleep if needed
        if(hr->delay != -1 || hr->delay == 0) {
            sleep(hr->delay);
        }

        // connect to the fileserver
        int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                        sizeof(fileserver_address));
        if (connection_status < 0) {
            // failed to connect to the fileserver
            print_and_flush("Failed to connect to the file server\n");
            send_error_response(hr->client_fd, BAD_GATEWAY, "Bad Gateway");
            return NULL;
        }

        int ret = http_send_data(fileserver_fd, hr->request, hr->request_length);

        if (ret < 0) {
            print_and_flush("Failed to send request to the file server\n");
            send_error_response(hr->client_fd, BAD_GATEWAY, "Bad Gateway");

        } else {
            // forward the fileserver response to the client
            print_and_flush("Forwarding the fileserver response to the client\n");
            while (1) {
                int bytes_read = recv(fileserver_fd, hr->request, RESPONSE_BUFSIZE - 1, 0);
                if (bytes_read <= 0) // fileserver_fd has been closed, break
                    break;
                ret = http_send_data(hr->client_fd, hr->request, bytes_read);
                if (ret < 0) { // write failed, client_fd has been closed
                    break;
                }
            }
        }

        // close the connection to the client
        shutdown(hr->client_fd, SHUT_RDWR);
        close(hr->client_fd);
        shutdown(fileserver_fd, SHUT_RDWR);
        close(fileserver_fd);
        free(hr);
    }
    
    return NULL;
}

/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void* serve_forever(void* arg) {
    listener_args* args = (listener_args*) arg;
    int server_fd = args->server_fd;
    int port = args->port;

    // manipulate options for the socket
    int socket_option = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                   sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }


    int proxy_port = port;
    // create the full address of this proxyserver
    struct sockaddr_in proxy_address;
    memset(&proxy_address, 0, sizeof(proxy_address));
    proxy_address.sin_family = AF_INET;
    proxy_address.sin_addr.s_addr = INADDR_ANY;
    proxy_address.sin_port = htons(proxy_port); // listening port

    // bind the socket to the address and port number specified in
    if (bind(server_fd, (struct sockaddr *)&proxy_address,
             sizeof(proxy_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    // starts waiting for the client to request a connection
    if (listen(server_fd, port) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    print_and_flush("Listening on port %d...\n", proxy_port);

    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;

    while (1) {
        print_and_flush("serve_forever: waiting for a client\n");
        client_fd = accept(server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0) {
            perror("Error accepting socket");
            continue;
        }

        print_and_flush("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);

        // Parse client request to init http_request
        print_and_flush("DEBUG: About to parse client request\n");
        struct http_request* hr;
        hr = parse_client_request(client_fd);

        // Taking care of GetJob request
        if(hr != NULL) {
            print_and_flush("DEBUG: About to add to queue: %s %s %d\n", hr->method, hr->path, hr->request_length);

            // Add http_request to queue - will copy contents so we can free this ptr
            int rc = add_work(pq, hr);
            if(rc == QUEUE_FULL) {
                send_error_response(client_fd, QUEUE_FULL, http_get_response_message(QUEUE_FULL));

                // close the connection to the client
                close(hr->client_fd);
            }
            print_and_flush("DEBUG: Added to queue\n");
            print_queue(pq);
            free(hr);

        } else {

            struct http_request* rq = get_work_nonblocking(pq);
            if(rq == (struct http_request*) QUEUE_EMPTY) {
                print_and_flush("Error Code for empty GetJob: %p\n", rq);
                send_error_response(client_fd, QUEUE_EMPTY, http_get_response_message(QUEUE_EMPTY));
                
            } else {
                // Sending removed request to client
                // send_error_response(client_fd, OK, rq->request);
                http_start_response(client_fd, OK);
                http_send_string(client_fd, "\r\n");
                http_send_string(client_fd, rq->path);

                print_and_flush("DEBUG: Non-blocking remove from queue for GetJob\n");
                print_queue(pq);

                free(rq);
            }

            close(client_fd);
        }
    }

    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);

    return NULL;
}

/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    print_and_flush("\t---- Setting ----\n");
    print_and_flush("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        print_and_flush(" %d", listener_ports[i]);
    print_and_flush(" ]\n");
    print_and_flush("\t%d workers\n", num_listener);
    print_and_flush("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    print_and_flush("\tmax queue size  %d\n", max_queue_size);
    print_and_flush("\t  ----\t----\t\n");
}

// void signal_callback_handler(int signum) {
//     print_and_flush("Caught signal %d: %s\n", signum, strsignal(signum));
//     for (int i = 0; i < num_listener; i++) {
//         if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
//     }
//     // free_queue(pq);
//     free(listener_ports);
//     exit(0);
// }

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {print_and_flush("DEBUG\n");
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    // signal(SIGINT, signal_callback_handler);

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp("-l", argv[i]) == 0) {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++) {
                listener_ports[j] = atoi(argv[++i]);
            }
        } else if (strcmp("-w", argv[i]) == 0) {
            num_workers = atoi(argv[++i]);
        } else if (strcmp("-q", argv[i]) == 0) {
            max_queue_size = atoi(argv[++i]);
        } else if (strcmp("-i", argv[i]) == 0) {
            fileserver_ipaddr = argv[++i];
        } else if (strcmp("-p", argv[i]) == 0) {
            fileserver_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();

    // Initializing priority queue
    pq = (queue*)malloc(sizeof(queue));
    if (pq == NULL) {
        return -1;
    }
    create_queue(pq, max_queue_size);
    print_and_flush("queue pointer: %p\n", pq);
    print_queue(pq);

    pthread_t *listeners_t = malloc(sizeof(pthread_t) * num_listener);
    pthread_t *workers_t = malloc(sizeof(pthread_t) * num_workers);
    listener_args* l_args = malloc(sizeof(listener_args) * num_listener);
    int* listener_sockets = malloc(sizeof(int) * num_listener);

    if (listeners_t == NULL || workers_t == NULL || l_args == NULL) {
        // Handle memory allocation failure
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    // Creating listener threads
    print_and_flush("Creating %d listeners threads...\n", num_listener);
    for(int j = 0; j < num_listener; j++) {
        // create a socket to listen
        listener_sockets[j]= socket(PF_INET, SOCK_STREAM, 0);
        if (listener_sockets[j] == -1) {
            perror("Failed to create a new socket");
            exit(errno);
        }

        l_args[j].server_fd = listener_sockets[j];
        l_args[j].port = listener_ports[j];
        pthread_create(&listeners_t[j], NULL, serve_forever, &l_args[j]);
    }

    // Creating worker threads
    print_and_flush("Creating %d worker threads...\n", num_workers);
    for(int j = 0; j < num_workers; j++) {
        pthread_create(&workers_t[j], NULL, work_forever, NULL);
    }

    // Waiting for listener threads
    for(int j = 0; j < num_listener; j++) {
        pthread_join(listeners_t[0], NULL);
    }

    // Waiting for worker threads
    for(int j = 0; j < num_workers; j++) {
        pthread_join(listeners_t[0], NULL);
    }

    free(listeners_t);
    free(workers_t);
    free(l_args);
    return EXIT_SUCCESS;
}

//  pthread_t *listeners_t = malloc(sizeof(pthread_t) * num_listener);
//     pthread_t *workers_t = malloc(sizeof(pthread_t) * num_workers);
    
//     // for(int j = 0; j < num_listener; i++) {
//     //     listener_args* l_args = malloc(sizeof(listener_args));
//     //     l_args->server_fd = &server_fd;
//     //     l_args->port = listener_ports[j];
//     //     pthread_create(&listeners_t[j], NULL, serve_forever, &l_args);
//     //     pthread_join(listeners_t[j], NULL);
//     //     free(l_args);
//     // }
//         listener_args* l_args = malloc(sizeof(listener_args));
//         l_args->server_fd = &server_fd;
//         l_args->port = listener_ports[0];
//     pthread_create(&listeners_t[0], NULL, serve_forever, &l_args);
//         pthread_join(listeners_t[0], NULL);
//     // Starting worker threads - FOR NOW JUST 1
//     // for(int j = 0; j < num_workers; i++) {
//     //     pthread_create(&workers_t[j], NULL, work_forever, NULL);
//     //     pthread_join(workers_t[j], NULL);
//     // }
//     pthread_create(&workers_t[0], NULL, work_forever, NULL);
//         pthread_join(workers_t[0], NULL);

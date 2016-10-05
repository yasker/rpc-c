#ifndef LONGHORN_RPC_SERVER_HEADER
#define LONGHORN_RPC_SERVER_HEADER

#include <pthread.h>

#include "longhorn-rpc-protocol.h"

struct server_connection {
        int fd;

        pthread_t response_thread;

        struct handler_callbacks *cbs;
        struct Message *msg_table;
        pthread_mutex_t mutex;
};

struct handler_callbacks {
        int (*read_at) (void *buf, size_t count, off_t offset);
        int (*write_at) (void *buf, size_t count, off_t offset);
};

struct server_request {
        struct server_connection *conn;
        struct Message* msg;
};

struct server_connection *new_server_connection(char *socket_path, struct handler_callbacks *cbs);
int start_server(struct server_connection *conn);
void shutdown_server_connection(struct server_connection *conn);

#endif

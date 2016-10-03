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

int start_server(char *socket_path, struct handler_callbacks *cbs);

#endif

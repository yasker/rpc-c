#ifndef RPC_C_HEADER
#define RPC_C_HEADER

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>

#include "uthash.h"

struct connection {
        int seq;  // must be atomic
        int fd;

        pthread_t response_thread;

        struct Message *msg_table;
        pthread_mutex_t mutex;
};

struct Message {
        uint32_t        Seq;
        uint32_t        Type;
        int64_t         Offset;
        uint32_t        DataLength;
        void*           Data;

	pthread_cond_t  cond;
	pthread_mutex_t mutex;

        UT_hash_handle hh;
};

enum uint32_t {
	TypeRead,
	TypeWrite,
	TypeResponse,
	TypeError,
	TypeEOF
};

struct connection *new_connection(char *socket_path);
int free_connection(struct connection *conn);

int read_at(struct connection *conn, void *buf, size_t count, off_t offset);
int write_at(struct connection *conn, void *buf, size_t count, off_t offset);

void start_response_processing(struct connection *conn);

#endif

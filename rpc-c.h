#ifndef RPC_C_HEADER
#define RPC_C_HEADER

#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

struct connection {
        int seq;  // must be atomic
        int fd;

        pthread_t response_thread;

        struct entry *msg_table;
        pthread_mutex_t table_mutex;
};

struct MessageHeader{
        uint32_t        Seq;
        uint32_t        Type;
        int64_t         Offset;
        uint32_t        DataLength;
};

struct Message {
        struct MessageHeader Header;
        void*           Data;

	pthread_cond_t  cond;
	pthread_mutex_t mutex;
};

enum uint32_t {
	TypeRead,
	TypeWrite,
	TypeResponse,
	TypeError,
	TypeEOF
};

#endif

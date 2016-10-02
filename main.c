#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <sys/mman.h>

#include "uthash.h"

#include "rpc-c.h"

const int request_count = 1;

const size_t SAMPLE_SIZE = 100 * 1024 * 1024;

int start_test(struct connection *conn, int request_size, int queue_depth) {
        int rc = 0;
        int i, request_count;

        char *buf;
        void *tmpbuf = malloc(request_size);

        if (SAMPLE_SIZE & request_size != 0) {
                fprintf(stderr, "Request_size if not aligned with SAMPLE_SIZE!\n");
                exit(-1);
        }
        request_count = SAMPLE_SIZE / request_size;

        buf = mmap(NULL, SAMPLE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (buf == (void *)-1) {
                perror("Cannot allocate enough memory");
                exit(-1);
        }

        for (i = 0; i < SAMPLE_SIZE; i ++) {
                buf[i] = rand() % 26 + 'a';
        }

        printf("Sample memory generated\n");

        // We're going to write the whole thing to server(which store it in
        // memory), then read from it.
        for (i = 0; i < request_count; i ++) {
                int rc, offset = i * request_size;

//                printf("Write %d at %d\n", request_size, offset);
                rc = write_at(conn, buf + offset, request_size, offset);
                if (rc < 0) {
                        fprintf(stderr, "Fail to complete write for %d\n", offset);
                        return -EFAULT;
                }
        }

        for (i = 0; i < request_count; i ++) {
                int rc, offset = i * request_size;

                //printf("Read %d at %d\n", request_size, offset);

                rc = read_at(conn, tmpbuf, request_size, offset);
                if (rc < 0) {
                        fprintf(stderr, "Fail to complete read for %d\n", offset);
                        return -EFAULT;
                }

                if (memcmp(tmpbuf, buf + offset, request_size) != 0) {
                        fprintf(stderr, "Inconsistency found at %d!\n", offset);
                        return -EFAULT;
                }
        }
        printf("Done\n");
}

int main(int argc, char *argv[])
{
        int request_size = 4096;
        char *socket_path = NULL;
        int queue_depth = 128;
	int c, rc = 0;
        struct connection *conn;

        while ((c = getopt(argc, argv, "r:q:s:")) != -1) {
                switch (c) {
                case 'r':
                        request_size = atoi(optarg);
                        break;
                case 'q':
                        queue_depth = atoi(optarg);
                        break;
                case 's':
                        socket_path = malloc(strlen(optarg) + 1);
                        strcpy(socket_path, optarg);
			break;
                default:
                        fprintf(stderr, "Cannot understand command\n");
                        return -EINVAL;
                }
        }
        if (socket_path == NULL) {
		socket_path = "/tmp/rpc.sock";
	}

	printf("Socket %s, request %d, queue depth %d\n", socket_path,
			request_size, queue_depth);

        conn = new_connection(socket_path);
        if (conn == NULL) {
                fprintf(stderr, "cannot estibalish connection");
        }

        start_response_processing(conn);

        start_test(conn, request_size, queue_depth);

        rc = pthread_join(conn->response_thread, NULL);
        if (rc < 0) {
                perror("Fail to wait for response thread to exit");
        }
        return 0;
}


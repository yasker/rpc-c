#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "uthash.h"

#include "longhorn-rpc-client.h"
#include "longhorn-rpc-server.h"

const int request_count = 1;

const size_t SAMPLE_SIZE = 100 * 1024 * 1024;

static void *server_buf;

int start_test(struct client_connection *conn, int request_size, int queue_depth) {
        int rc = 0;
        int i, request_count;

        char *buf;
        void *tmpbuf = malloc(request_size);

        struct timespec start, read_stop, write_stop;
        uint32_t delta_write_ms, delta_read_ms;
        float write_bw, read_bw;

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

        clock_gettime(CLOCK_MONOTONIC_RAW, &start);
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

        clock_gettime(CLOCK_MONOTONIC_RAW, &write_stop);
        printf("Write done\n");

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
        clock_gettime(CLOCK_MONOTONIC_RAW, &read_stop);
        printf("Read done\n");

        delta_write_ms = (write_stop.tv_sec - start.tv_sec) * 1E3 + (write_stop.tv_nsec - start.tv_nsec) / 1E6;
        delta_read_ms = (read_stop.tv_sec - start.tv_sec) * 1E3 + (read_stop.tv_nsec - start.tv_nsec) / 1E6;
        write_bw = (SAMPLE_SIZE / 1024 / 1024) / (delta_write_ms / 1E3);
        read_bw = (SAMPLE_SIZE / 1024 / 1024) / (delta_read_ms / 1E3);
        printf("Write done in %d ms\n", delta_write_ms);
        printf("Write bandwidth is %.2f M/s\n", write_bw);
        printf("Read done in %d ms\n", delta_read_ms);
        printf("Read bandwidth is %.2f M/s\n", read_bw);
}

int server_read_at(void *buf, size_t count, off_t offset) {
        if (offset + count > SAMPLE_SIZE) {
                return -EINVAL;
        }
        memcpy(buf, server_buf + offset, count);
        return 0;
}

int server_write_at(void *buf, size_t count, off_t offset) {
        if (offset + count > SAMPLE_SIZE) {
                return -EINVAL;
        }
        memcpy(server_buf + offset, buf, count);
        return 0;
}

static struct handler_callbacks cbs = {
        .read_at = server_read_at,
        .write_at = server_write_at,
};

int main(int argc, char *argv[])
{
        int request_size = 4096;
        char *socket_path = NULL;
        int queue_depth = 128;
        int client = 0;
	int c, rc = 0;
        struct client_connection *conn;

        while ((c = getopt(argc, argv, "r:q:s:c")) != -1) {
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
                case 'c':
                        client = 1;
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

        if (client) {
                conn = new_client_connection(socket_path);
                if (conn == NULL) {
                        fprintf(stderr, "cannot estibalish connection");
                }

                start_response_processing(conn);

                start_test(conn, request_size, queue_depth);

                rc = pthread_join(conn->response_thread, NULL);
                if (rc < 0) {
                        perror("Fail to wait for response thread to exit");
                }
        } else {
                unlink(socket_path);

                server_buf = mmap(NULL, SAMPLE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (server_buf == (void *)-1) {
                        perror("Cannot allocate enough memory");
                        exit(-1);
                }
                bzero(server_buf, SAMPLE_SIZE);

                start_server(socket_path, &cbs);
        }
        return 0;
}


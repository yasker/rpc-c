#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "uthash.h"

#include "longhorn-rpc-client.h"
#include "longhorn-rpc-server.h"

const int request_count = 1;

const size_t SAMPLE_SIZE = 100 * 1024 * 1024;

static void *server_buf;

static struct client_connection *client_conn;
static struct server_connection *server_conn;

int start_test(struct client_connection *conn, int request_size, int queue_depth) {
        int rc = 0;
        int i, request_count;

        char *buf;
        void *tmpbuf = malloc(request_size);

        struct timespec write_start, write_stop, read_start, read_stop;
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

        clock_gettime(CLOCK_MONOTONIC_RAW, &write_start);
        // We're going to write the whole thing to server(which store it in
        // memory), then read from it.
        for (i = 0; i < request_count; i ++) {
                int rc, offset = i * request_size;

//                printf("Write %d at %d\n", request_size, offset);
                rc = write_at(conn, buf + offset, request_size, offset);
                if (rc < 0) {
                        fprintf(stderr, "Fail to complete write for %d\n", offset);
                        goto out;
                }
        }

        clock_gettime(CLOCK_MONOTONIC_RAW, &write_stop);
        delta_write_ms = (write_stop.tv_sec - write_start.tv_sec) * 1E3 + (write_stop.tv_nsec - write_start.tv_nsec) / 1E6;
        write_bw = (SAMPLE_SIZE / 1024 / 1024) / (delta_write_ms / 1E3);
        printf("Write done in %d ms\n", delta_write_ms);
        printf("Write bandwidth is %.2f M/s\n", write_bw);

        clock_gettime(CLOCK_MONOTONIC_RAW, &read_start);
        for (i = 0; i < request_count; i ++) {
                int rc, offset = i * request_size;

                rc = read_at(conn, tmpbuf, request_size, offset);
                if (rc < 0) {
                        fprintf(stderr, "Fail to complete read for %d\n", offset);
                        goto out;
                }

                if (memcmp(tmpbuf, buf + offset, request_size) != 0) {
                        fprintf(stderr, "Inconsistency found at %d!\n", offset);
                        goto out;
                }
        }
        clock_gettime(CLOCK_MONOTONIC_RAW, &read_stop);
        delta_read_ms = (read_stop.tv_sec - read_start.tv_sec) * 1E3 + (read_stop.tv_nsec - read_start.tv_nsec) / 1E6;
        read_bw = (SAMPLE_SIZE / 1024 / 1024) / (delta_read_ms / 1E3);
        printf("Read done in %d ms\n", delta_read_ms);
        printf("Read bandwidth is %.2f M/s\n", read_bw);
out:
        free(tmpbuf);
        munmap(buf, SAMPLE_SIZE);
        return rc;
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

void signal_handler(int signo) {
        if (signo == SIGINT) {
                printf("SIGINT received, stop process\n");
        }
        if (client_conn != NULL) {
                shutdown_client_connection(client_conn);
        }
        if (server_conn != NULL) {
                shutdown_server_connection(server_conn);
        }
        exit(0);
}

int main(int argc, char *argv[])
{
        int request_size = 4096;
        char *socket_path = NULL;
        int queue_depth = 128;
        int client = 0;
	int c, rc = 0;

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

        if (signal(SIGINT, signal_handler) == SIG_ERR) {
                printf("Cannot catch signal, failed initialization\n");
                exit(-1);
        }
        if (client) {
                client_conn = new_client_connection(socket_path);
                if (client_conn == NULL) {
                        fprintf(stderr, "cannot estibalish connection");
                }

                start_response_processing(client_conn);

                start_test(client_conn, request_size, queue_depth);

                rc = pthread_join(client_conn->response_thread, NULL);
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

                server_conn = new_server_connection(socket_path, &cbs);

                start_server(server_conn);
        }
        return 0;
}

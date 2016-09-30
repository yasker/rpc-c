#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "rpc-c.h"

const int request_count = 1;

int read_full(int fd, void *buf, int len) {
        int readed = 0;
        int ret;

        while (readed < len) {
                ret = read(fd, buf + readed, len - readed);
                if (ret < 0) {
                        return ret;
                }
                readed += ret;
        }
        return readed;
}

int write_full(int fd, void *buf, int len) {
        int wrote = 0;
        int ret;

        while (wrote < len) {
                ret = write(fd, buf + wrote, len - wrote);
                if (ret < 0) {
                        return ret;
                }
                wrote += ret;
        }
        return wrote;
}

int send_request(struct connection *conn, struct Message *req) {
        int n;

        n = write_full(conn->fd, &req->Header.Seq, sizeof(req->Header.Seq));
        if (n != sizeof(req->Header.Seq)) {
                fprintf(stderr, "fail to write seq");
		return -EINVAL;
        }
        n = write_full(conn->fd, &req->Header.Type, sizeof(req->Header.Type));
        if (n != sizeof(req->Header.Type)) {
                fprintf(stderr, "fail to write type");
		return -EINVAL;
        }
        n = write_full(conn->fd, &req->Header.Offset, sizeof(req->Header.Offset));
        if (n != sizeof(req->Header.Offset)) {
                fprintf(stderr, "fail to write offset");
		return -EINVAL;
        }
        n = write_full(conn->fd, &req->Header.DataLength, sizeof(req->Header.DataLength));
        if (n != sizeof(req->Header.DataLength)) {
                fprintf(stderr, "fail to write datalength");
		return -EINVAL;
        }
	if (req->Header.DataLength != 0) {
		n = write_full(conn->fd, req->Data, req->Header.DataLength);
		if (n != req->Header.DataLength) {
			fprintf(stderr, "fail to write data");
			return -EINVAL;
		}
	}
        return 0;
}

int receive_response(struct connection *conn, struct Message *resp) {
	void *buf;
	int n;
       
	n = read_full(conn->fd, &resp->Header.Seq, sizeof(resp->Header.Seq));
        if (n != sizeof(resp->Header.Seq)) {
                fprintf(stderr, "fail to write seq");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->Header.Type, sizeof(resp->Header.Type));
        if (n != sizeof(resp->Header.Type)) {
                fprintf(stderr, "fail to read type");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->Header.Offset, sizeof(resp->Header.Offset));
        if (n != sizeof(resp->Header.Offset)) {
                fprintf(stderr, "fail to read offset");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->Header.DataLength, sizeof(resp->Header.DataLength));
        if (n != sizeof(resp->Header.DataLength)) {
                fprintf(stderr, "fail to read datalength");
		return -EINVAL;
        }

	if (resp->Header.Type != TypeResponse) {
		fprintf(stderr, "Invalid response received");
		return -EINVAL;
	}

	if (resp->Header.DataLength > 0) {
		resp->Data = malloc(resp->Header.DataLength);
		n = read_full(conn->fd, resp->Data, resp->Header.DataLength);
		if (n != resp->Header.DataLength) {
			free(resp->Data);
			return -EINVAL;
		}
	}
	return 0;
}

void* response_process(void *arg) {
        struct connection *conn = arg;
        struct Message *resp;
        int ret = 0;

        printf("response thread ready\n");
	resp = malloc(sizeof(struct Message));
	ret = receive_response(conn, resp);
        while (ret == 0) {
                printf("Received size %d, %s\n", resp->Header.DataLength, (char *)resp->Data);
                free(resp->Data);
                free(resp);
                ret = receive_response(conn, resp);
        }
        if (ret != 0) {
                fprintf(stderr, "Fail to receive response");
        }
}

void start_processing(struct connection *conn) {
        int rc;

        rc = pthread_create(&conn->response_thread, NULL, &response_process, conn);
        if (rc < 0) {
                perror("Fail to create response thread");
                exit(-1);
        }
}

int new_seq(struct connection *conn) {
        return __sync_fetch_and_add(&conn->seq, 1);
}

int send_requests(struct connection *conn, int request_size, int queue_depth) {
        const char *buf = "Request";
        int rc = 0;

        printf("request thread ready\n");
        for (int i = 0; i < request_count; i ++) {
                struct Message *req = malloc(sizeof(struct Message));
                req->Header.Seq = new_seq(conn);
                req->Header.Type = TypeWrite;
                req->Header.Offset = 0;
                req->Header.DataLength = strlen(buf) + 1;
                req->Data = malloc(req->Header.DataLength);
                memcpy(req->Data, buf, req->Header.DataLength);
                printf("about to send request %s\n", buf);
                rc = send_request(conn, req);
                printf("request sent\n");
                if (rc < 0) {
                        fprintf(stderr, "Fail to send request");
                }

                free(req->Data);
                free(req);
        }
        printf("all requests sent\n");
}

struct connection *new_connection(char *socket_path) {
        struct sockaddr_un addr;
        int fd, rc = 0;
        struct connection *conn = NULL;

        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd == -1) {
                perror("socket error");
                exit(-1);
        }

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        if (strlen(socket_path) >= 108) {
                fprintf(stderr, "socket path is too long, more than 108 characters");
                exit(-EINVAL);
        }

        strncpy(addr.sun_path, socket_path, strlen(socket_path));

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                perror("connect error");
                exit(-EFAULT);
        }

        conn = malloc(sizeof(struct connection));
        conn->fd = fd;
        conn->seq = 0;
        conn->msg_table = NULL;
        //conn->table_mutex = PTHREAD_MUTEX_INITIALIZER;
        return conn;
}

int free_connection(struct connection *conn) {
        free(conn);
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

        start_processing(conn);

        send_requests(conn, request_size, queue_depth);

        rc = pthread_join(conn->response_thread, NULL);
        if (rc < 0) {
                perror("Fail to wait for response thread to exit");
        }
        return 0;
}


#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "longhorn-rpc-server.h"

void *server_process_requests(void *arg) {
        struct server_request *req = arg;
        struct server_connection *conn = req->conn;
        struct Message *msg = req->msg;
        int rc;

        free(req);

        if (msg->Type == TypeRead) {
                rc = conn->cbs->read_at(msg->Data, msg->DataLength, msg->Offset);
        } else if (msg->Type == TypeWrite) {
                rc = conn->cbs->write_at(msg->Data, msg->DataLength, msg->Offset);
        }
        msg->Type = TypeResponse;

        pthread_mutex_lock(&conn->mutex);
        rc = send_msg(conn->fd, msg);
        pthread_mutex_unlock(&conn->mutex);

        if (rc < 0) {
                fprintf(stderr, "fail to send response\n");
        }
        if (msg->DataLength != 0) {
                free(msg->Data);
        }
        free(msg);
}

int server_dispatch_requests(struct server_connection *conn, struct Message *msg) {
        int rc = 0;
        pthread_t pid;

        struct server_request *req;

        if (msg->Type != TypeRead && msg->Type != TypeWrite) {
                fprintf(stderr, "Invalid request type");
                return -EINVAL;
        }

        req = malloc(sizeof(struct server_request));
        req->msg = msg;
        req->conn = conn;
        rc = pthread_create(&pid, NULL, server_process_requests, req);
        return rc;
}

int start_server(char *socket_path, struct handler_callbacks *cbs) {
        struct sockaddr_un addr;
        int fd, connfd, rc = 0;
        struct server_connection *conn = NULL;

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

        rc = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
        if (rc < 0) {
                perror("fail to bind server");
        }

        rc = listen(fd, 1);
        connfd  = accept(fd, (struct sockaddr*)NULL, NULL);

        conn = malloc(sizeof(struct server_connection));
        conn->fd = connfd;
        conn->cbs = cbs;
        pthread_mutex_init(&conn->mutex, NULL);

        while (1) {
                // msg will be freed after done processing
                struct Message *msg = malloc(sizeof(struct Message));
		bzero(msg, sizeof(struct Message));

                rc = receive_msg(connfd, msg);
		if (rc < 0) {
			fprintf(stderr, "Fail to receive request\n");
			return rc;
		}
		rc = server_dispatch_requests(conn, msg);
		if (rc < 0) {
			fprintf(stderr, "Fail to process requests\n");
			return rc;
		}
        }
}

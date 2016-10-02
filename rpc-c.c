#include <stdio.h>

#include "rpc-c.h"

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
        int n, rc = 0;

        pthread_mutex_lock(&conn->mutex);
        n = write_full(conn->fd, &req->Seq, sizeof(req->Seq));
        if (n != sizeof(req->Seq)) {
                fprintf(stderr, "fail to write seq");
                rc = -EINVAL;
                goto out;
        }
        n = write_full(conn->fd, &req->Type, sizeof(req->Type));
        if (n != sizeof(req->Type)) {
                fprintf(stderr, "fail to write type");
                rc = -EINVAL;
                goto out;
        }
        n = write_full(conn->fd, &req->Offset, sizeof(req->Offset));
        if (n != sizeof(req->Offset)) {
                fprintf(stderr, "fail to write offset");
                rc = -EINVAL;
                goto out;
        }
        n = write_full(conn->fd, &req->DataLength, sizeof(req->DataLength));
        if (n != sizeof(req->DataLength)) {
                fprintf(stderr, "fail to write datalength");
                rc = -EINVAL;
                goto out;
        }
	if (req->DataLength != 0) {
		n = write_full(conn->fd, req->Data, req->DataLength);
		if (n != req->DataLength) {
			fprintf(stderr, "fail to write data");
                        rc = -EINVAL;
                        goto out;
		}
	}
out:
        pthread_mutex_unlock(&conn->mutex);
        return rc;
}

int receive_response(struct connection *conn, struct Message *resp) {
	void *buf;
	int n;

        // There is only one thread reading the response, and socket is
        // full-duplex, so no need to lock
	n = read_full(conn->fd, &resp->Seq, sizeof(resp->Seq));
        if (n != sizeof(resp->Seq)) {
                fprintf(stderr, "fail to write seq");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->Type, sizeof(resp->Type));
        if (n != sizeof(resp->Type)) {
                fprintf(stderr, "fail to read type");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->Offset, sizeof(resp->Offset));
        if (n != sizeof(resp->Offset)) {
                fprintf(stderr, "fail to read offset");
		return -EINVAL;
        }
        n = read_full(conn->fd, &resp->DataLength, sizeof(resp->DataLength));
        if (n != sizeof(resp->DataLength)) {
                fprintf(stderr, "fail to read datalength");
		return -EINVAL;
        }

	if (resp->Type != TypeResponse) {
		fprintf(stderr, "Invalid response received");
		return -EINVAL;
	}

        //printf("response: seq %d, type %d, offset %ld, length %d\n",
                        //resp->Seq, resp->Type, resp->Offset, resp->DataLength);

	if (resp->DataLength > 0) {
		resp->Data = malloc(resp->DataLength);
                if (resp->Data == NULL) {
                        perror("cannot allocate memory for data");
                        return -EINVAL;
                }
		n = read_full(conn->fd, resp->Data, resp->DataLength);
		if (n != resp->DataLength) {
                        fprintf(stderr, "Cannot read full from fd, %d vs %d\n",
                                resp->DataLength, n);
			free(resp->Data);
			return -EINVAL;
		}
	}
	return 0;
}

void* response_process(void *arg) {
        struct connection *conn = arg;
        struct Message *req, *resp;
        int ret = 0;

	resp = malloc(sizeof(struct Message));
        if (resp == NULL) {
            perror("cannot allocate memory for resp");
            return NULL;
        }

        // TODO Need to add multiple event poll to gracefully shutdown
	ret = receive_response(conn, resp);
        while (ret == 0) {
                /*
                if (resp->Seq % 1024 == 0) {
                        printf("Received seq %d\n", resp->Seq);
                }
                */

                if (resp->Type != TypeResponse) {
                        fprintf(stderr, "Wrong type for response of seq %d\n",
                                        resp->Seq);
                        continue;
                }

                pthread_mutex_lock(&conn->mutex);
                HASH_FIND_INT(conn->msg_table, &resp->Seq, req);
                if (req != NULL) {
                        HASH_DEL(conn->msg_table, req);
                }
                pthread_mutex_unlock(&conn->mutex);

                pthread_mutex_lock(&req->mutex);
                req->Data = resp->Data;
                pthread_mutex_unlock(&req->mutex);

                pthread_cond_signal(&req->cond);

                ret = receive_response(conn, resp);
        }
        free(resp);
        if (ret != 0) {
                fprintf(stderr, "Fail to receive response");
        }
}

void start_response_processing(struct connection *conn) {
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

int process_request(struct connection *conn, void *buf, size_t count, off_t offset, 
                uint32_t type) {
        struct Message *req = malloc(sizeof(struct Message));
        int rc = 0;

        if (req == NULL) {
            perror("cannot allocate memory for req");
            return -EINVAL;
        }

        if (type != TypeRead && type != TypeWrite) {
                fprintf(stderr, "BUG: Invalid type for process_request %d\n", type);
                return -EINVAL;
        }
        req->Seq = new_seq(conn);
        req->Type = type;
        req->Offset = offset;
        req->DataLength = count;
        req->Data = buf;

        if (req->Type == TypeRead) {
                bzero(req->Data, count);
        }

        rc = pthread_cond_init(&req->cond, NULL);
        if (rc < 0) {
                perror("Fail to init phread_cond");
                return rc;
        }
        rc = pthread_mutex_init(&req->mutex, NULL);
        if (rc < 0) {
                perror("Fail to init phread_mutex");
                return rc;
        }

        pthread_mutex_lock(&conn->mutex);
        HASH_ADD_INT(conn->msg_table, Seq, req);
        pthread_mutex_unlock(&conn->mutex);

        pthread_mutex_lock(&req->mutex);
        rc = send_request(conn, req);
        if (rc < 0) {
                goto out;
        }

        pthread_cond_wait(&req->cond, &req->mutex);

        if (req->Type == TypeRead) {
                memcpy(buf, req->Data, req->DataLength);
        }
out:
        pthread_mutex_unlock(&req->mutex);
        free(req);
        return rc;
}

int read_at(struct connection *conn, void *buf, size_t count, off_t offset) {
        process_request(conn, buf, count, offset, TypeRead);
}

int write_at(struct connection *conn, void *buf, size_t count, off_t offset) {
        process_request(conn, buf, count, offset, TypeWrite);
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
        if (conn == NULL) {
            perror("cannot allocate memory for conn");
            return NULL;
        }

        conn->fd = fd;
        conn->seq = 0;
        conn->msg_table = NULL;

        rc = pthread_mutex_init(&conn->mutex, NULL);
        if (rc < 0) {
                perror("fail to init conn->mutex");
                exit(-EFAULT);
        }
        return conn;
}

int free_connection(struct connection *conn) {
        free(conn);
}

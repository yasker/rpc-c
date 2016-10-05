#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "longhorn-rpc-protocol.h"

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

int send_msg(int fd, struct Message *msg) {
        int n = 0;

        n = write_full(fd, &msg->Seq, sizeof(msg->Seq));
        if (n != sizeof(msg->Seq)) {
                fprintf(stderr, "fail to write seq");
                return -EINVAL;
        }
        n = write_full(fd, &msg->Type, sizeof(msg->Type));
        if (n != sizeof(msg->Type)) {
                fprintf(stderr, "fail to write type");
                return -EINVAL;
        }
        n = write_full(fd, &msg->Offset, sizeof(msg->Offset));
        if (n != sizeof(msg->Offset)) {
                fprintf(stderr, "fail to write offset");
                return -EINVAL;
        }
        n = write_full(fd, &msg->DataLength, sizeof(msg->DataLength));
        if (n != sizeof(msg->DataLength)) {
                fprintf(stderr, "fail to write datalength");
                return -EINVAL;
        }
	if (msg->DataLength != 0) {
		n = write_full(fd, msg->Data, msg->DataLength);
		if (n != msg->DataLength) {
			fprintf(stderr, "fail to write data");
                        return -EINVAL;
		}
	}
        return 0;
}

// Caller need to release msg->Data
int receive_msg(int fd, struct Message *msg) {
	void *buf;
	int n;

        bzero(msg, sizeof(struct Message));

        // There is only one thread reading the response, and socket is
        // full-duplex, so no need to lock
	n = read_full(fd, &msg->Seq, sizeof(msg->Seq));
        if (n != sizeof(msg->Seq)) {
                fprintf(stderr, "fail to write seq");
		return -EINVAL;
        }
        n = read_full(fd, &msg->Type, sizeof(msg->Type));
        if (n != sizeof(msg->Type)) {
                fprintf(stderr, "fail to read type");
		return -EINVAL;
        }
        n = read_full(fd, &msg->Offset, sizeof(msg->Offset));
        if (n != sizeof(msg->Offset)) {
                fprintf(stderr, "fail to read offset");
		return -EINVAL;
        }
        n = read_full(fd, &msg->DataLength, sizeof(msg->DataLength));
        if (n != sizeof(msg->DataLength)) {
                fprintf(stderr, "fail to read datalength");
		return -EINVAL;
        }

	if (msg->DataLength > 0) {
		msg->Data = malloc(msg->DataLength);
                if (msg->Data == NULL) {
                        perror("cannot allocate memory for data");
                        return -EINVAL;
                }
		n = read_full(fd, msg->Data, msg->DataLength);
		if (n != msg->DataLength) {
                        fprintf(stderr, "Cannot read full from fd, %d vs %d\n",
                                msg->DataLength, n);
			free(msg->Data);
			return -EINVAL;
		}
	}
	return 0;
}

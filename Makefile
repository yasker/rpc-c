all:
	gcc main.c longhorn-rpc-server.h longhorn-rpc-client.h \
		longhorn-rpc-protocol.h longhorn-rpc-protocol.c \
		longhorn-rpc-server.c longhorn-rpc-client.c \
		-o rpc -lpthread -ggdb

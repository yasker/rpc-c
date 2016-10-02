all:
	gcc main.c rpc-c.h rpc-c.c -o rpc -lpthread -ggdb

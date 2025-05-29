# -maulis-  2025.1.24
.PHONY: clean all debug test

all: fslatency fslatency_server

clean:
	rm -f fslatency
	rm -f fslatency_server
	rm -f test_nameregistry
	rm -f nameregistry.o
	rm -f fslatency_debug
	rm -f fslatency_server_debug
	rm -f nameregistry_debug.o

fslatency: fslatency.c datablock.h ringbuffer.inc
	gcc --static -Wall -o fslatency fslatency.c -l pthread -l m
	strip fslatency

fslatency_server: fslatency_server.c datablock.h ringbuffer.inc nameregistry.h nameregistry.o
	gcc --static -Wall -o fslatency_server fslatency_server.c nameregistry.o -l pthread -l m
	strip fslatency_server

nameregistry.o: nameregistry.c nameregistry.h
	gcc -Wall -c -o nameregistry.o nameregistry.c

debug: fslatency_debug fslatency_server_debug

fslatency_debug: fslatency.c datablock.h ringbuffer.inc
	gcc -DDEBUG -Wall -o fslatency_debug fslatency.c -l pthread -l m

fslatency_server_debug: fslatency_server.c datablock.h ringbuffer.inc nameregistry.h nameregistry_debug.o
	gcc -DDEBUG -Wall -o fslatency_server_debug fslatency_server.c nameregistry_debug.o -l pthread -l m

nameregistry_debug.o: nameregistry.c nameregistry.h
	gcc -DDEBUG -Wall -c -o nameregistry_debug.o nameregistry.c

test_nameregistry: test_nameregistry.c nameregistry.o
	gcc -Wall -o test_nameregistry test_nameregistry.c nameregistry.o

test: test_nameregistry
	./test_nameregistry 509 128

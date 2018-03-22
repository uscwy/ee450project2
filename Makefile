
CC=g++
CFLAGS=-g -Wall
LDFLAGS=-lz

all: sender receiver

receiver: receiver.cpp

sender:	sender.cpp

.cpp:
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f sender receiver

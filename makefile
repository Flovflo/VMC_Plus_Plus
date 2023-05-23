CC = gcc
CFLAGS = -Wall -Wextra
LIBS = -lwiringPi -lsqlite3 -lpthread -lwebsockets

all: exe

exe: main8.c
	$(CC) $(CFLAGS) main8.c -o exe $(LIBS)

clean:
	rm -f exe

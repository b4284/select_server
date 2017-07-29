CFLAGS = -c -O2 -Wall -Wextra 
LDFLAGS =

all: server client
	
clean: 
	rm -f *.o server client

.PHONY: all clean

server: server.o	
	gcc $(LDFLAGS) -o $@ $<

client: client.o
	gcc $(LDFLAGS) -o $@ $<

%.o: %.c
	gcc $(CFLAGS) -o $@ $<

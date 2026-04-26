CC     = gcc
CFLAGS = -Wall -g
INC    = -I include
LIBS   = -lsqlite3 -lssl -lcrypto

all: bin/server bin/client

bin/server: src/server/server.c src/server/client_handler.c src/server/auth.c src/server/tls.c
	$(CC) $(CFLAGS) $(INC) $^ $(LIBS) -o $@

bin/client: src/client/client.c src/server/tls.c
	$(CC) $(CFLAGS) $(INC) $^ $(LIBS) -lncurses -o $@

clean:
	rm -f bin/server bin/client authenticate.db
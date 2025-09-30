CC = clang
CFLAGS = -Wall -g
SERVER_SRC = src/server.c
CLIENT_SRC = src/client.c
EXEC_SERVER = server
EXEC_CLIENT = client

all: $(EXEC_SERVER) $(EXEC_CLIENT)

$(EXEC_SERVER): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^

$(EXEC_CLIENT): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(EXEC_SERVER) $(EXEC_CLIENT) *.o

.PHONY: all clean
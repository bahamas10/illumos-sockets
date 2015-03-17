CC ?= cc
CFLAGS = -Wall

opensockets: opensockets.c
	$(CC) $(CFLAGS) -lproc -lsocket -lnsl $^ -o $@

.PHONY: clean
clean:
	rm -f opensockets

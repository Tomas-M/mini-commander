CC = gcc
CFLAGS = -lncurses

ncurses-menu: mc.c
	$(CC) mc.c $(CFLAGS) -o mc

.PHONY: clean

test: mc
	./mc

clean:
	rm -f mc

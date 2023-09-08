CC = gcc
CFLAGS = -lncurses

ncurses-menu: ncurses-menu.c
	$(CC) ncurses-menu.c $(CFLAGS) -o ncurses-menu

test: ncurses-menu
	TERM=xfce ./ncurses-menu "Test title which is very longggggggggggggggggggggggggggggggggggggggggggggggggggggg" "1 aaa" "2 sss" "3 ddd" "select this item" "4 fff" 2> out
	if [ "$(cat out)" != "select this item" ]; then echo "!!!!!  error"; fi
	rm -f out

.PHONY: clean

clean:
	rm -f ncurses-menu

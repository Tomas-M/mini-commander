CC = gcc
CFLAGS += -lncurses -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64  -Os -s -g0

mc: *.c *.h
	$(CC) mc.c cmd.c dialog.c filelist.c init.c panel.c ui.c viewer.c $(CFLAGS) -o mc
	if which upx >/dev/null; then upx --lzma --best mc; fi

.PHONY: clean

tags:
	#
	# This will create a TAGS file for Midnight Commander's editor
	# allowing navigation between functions with Alt+Enter
	#
	ctags -e --totals=yes --tag-relative=yes --PHP-kinds=+f-v --JavaScript-kinds=+f-v --c-kinds=+f+d+t+v *

test: mc
	./mc

clean:
	rm -f mc
	rm -f TAGS

singlefile:
	rm -f singlefile.c
	cat *.h *.c | grep '^#include <' | sort | uniq > singlefile.c0
	cat types.h globals.h | sed -r 's/^extern//' >> singlefile.c0
	cat *.c | grep -v '^#include "' >> singlefile.c0
	mv singlefile.c0 singlefile.c

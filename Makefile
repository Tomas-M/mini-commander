CC = gcc
CFLAGS = -lncurses

mc: *.c *.h
	$(CC) mc.c cmd.c dialog.c filelist.c init.c panel.c ui.c viewer.c $(CFLAGS) -o mc
	strip --strip-unneeded mc
	if which upx >/dev/null; then upx --lzma --best mc; fi

.PHONY: clean

tags:
	#
	# This will create TAGS file for midnight commander
	#
	ctags -e --totals=yes --tag-relative=yes --PHP-kinds=+f-v --JavaScript-kinds=+f-v --c-kinds=+f+d+t+v *

test: mc
	./mc

clean:
	rm -f mc
	rm -f TAGS

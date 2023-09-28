#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>

#include "types.h"
#include "globals.h"

void init_screen() {
    initscr();
    refresh();
    mouseinterval(50);
    start_color();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);

    init_pair(COLOR_WHITE_ON_BLACK, COLOR_WHITE, COLOR_BLACK);
    init_pair(COLOR_BLACK_ON_WHITE, COLOR_BLACK, COLOR_WHITE);

    init_pair(COLOR_WHITE_ON_BLUE, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_YELLOW_ON_BLUE, COLOR_YELLOW, COLOR_BLUE);
    init_pair(COLOR_GREEN_ON_BLUE, COLOR_GREEN, COLOR_BLUE);
    init_pair(COLOR_RED_ON_BLUE, COLOR_RED, COLOR_BLUE);
    init_pair(COLOR_MAGENTA_ON_BLUE, COLOR_MAGENTA, COLOR_BLUE);
    init_pair(COLOR_CYAN_ON_BLUE, COLOR_CYAN, COLOR_BLUE);

    init_pair(COLOR_YELLOW_ON_CYAN, COLOR_YELLOW, COLOR_CYAN);
    init_pair(COLOR_BLACK_ON_CYAN, COLOR_BLACK, COLOR_CYAN);
}

void cleanup() {
    delwin(win1);
    delwin(win2);
    endwin();
}

void redraw_ui() {
   // Get screen dimensions
   int maxY, maxX;
   getmaxyx(stdscr, maxY, maxX);

   draw_windows(maxY, maxX);
   draw_buttons(maxY, maxX);
   update_cmd();
   refresh();
}

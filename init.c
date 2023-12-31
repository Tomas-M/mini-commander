#include "includes.h"
#include "types.h"
#include "globals.h"


SCREEN *screen = NULL;

void initialize_ncurses() {
    if (screen) return;

    const char *terms[] = {NULL, "xterm", "xfce", "linux"};
    screen = NULL;
    for (int i = 0; i < 4 && screen == NULL; ++i) {
        screen = newterm(terms[i], stdout, stdin);
    }

    if (screen == NULL) {
        // last attempt
        initscr();
    }
}


void init_screen() {
    initialize_ncurses();
    refresh();
    mouseinterval(50);
    ESCDELAY = 50;
    start_color();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);

    if (color_enabled) {
        init_pair(COLOR_WHITE_ON_BLACK, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_BLACK_ON_WHITE, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_WHITE_ON_RED, COLOR_WHITE, COLOR_RED);
        init_pair(COLOR_WHITE_ON_BLUE, COLOR_WHITE, COLOR_BLUE);
        init_pair(COLOR_YELLOW_ON_BLUE, COLOR_YELLOW, COLOR_BLUE);
        init_pair(COLOR_GREEN_ON_BLUE, COLOR_GREEN, COLOR_BLUE);
        init_pair(COLOR_RED_ON_BLUE, COLOR_RED, COLOR_BLUE);
        init_pair(COLOR_MAGENTA_ON_BLUE, COLOR_MAGENTA, COLOR_BLUE);
        init_pair(COLOR_CYAN_ON_BLUE, COLOR_CYAN, COLOR_BLUE);
        init_pair(COLOR_CYAN_ON_BLACK, COLOR_CYAN, COLOR_BLACK);
        init_pair(COLOR_YELLOW_ON_CYAN, COLOR_YELLOW, COLOR_CYAN);
        init_pair(COLOR_BLACK_ON_CYAN, COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_BLACK_ON_CYAN_BTN, COLOR_BLACK, COLOR_CYAN);
        init_pair(COLOR_BLACK_ON_CYAN_PMPT, COLOR_BLACK, COLOR_CYAN);
    } else { // black and white mode
        init_pair(COLOR_WHITE_ON_BLACK, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_BLACK_ON_WHITE, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_WHITE_ON_RED, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_WHITE_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_YELLOW_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_GREEN_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_RED_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_MAGENTA_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_CYAN_ON_BLUE, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_CYAN_ON_BLACK, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_YELLOW_ON_CYAN, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_BLACK_ON_CYAN, COLOR_BLACK, COLOR_WHITE);
        init_pair(COLOR_BLACK_ON_CYAN_BTN, COLOR_WHITE, COLOR_BLACK);
        init_pair(COLOR_BLACK_ON_CYAN_PMPT, COLOR_WHITE, COLOR_BLACK);
    }
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

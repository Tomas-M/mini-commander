#include "includes.h"
#include "types.h"
#include "globals.h"


SCREEN *screen = NULL;

void initialize_ncurses() {
    if (screen) return;

    // Decode terminal text using the user's locale without changing date/number formats.
    setlocale(LC_CTYPE, "");

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
    // Let wide-character input decode Alt and alternate function-key sequences.
    define_key("\033s", KEY_ALT_s);
    define_key("\033a", KEY_ALT_a);
    define_key("\033\n", KEY_ALT_ENTER);
    define_key("\033\r", KEY_ALT_ENTER);
    define_key("\033[1~", KEY_HOME);
    define_key("\033[2~", KEY_IC);
    define_key("\033[4~", KEY_END);
    define_key("\033[12~", KEY_F(2));
    define_key("\033[13~", KEY_F(3));
    define_key("\033[14~", KEY_F(4));
    define_key("\033[31~", KEY_SHIFT_F5);
    define_key("\033[32~", KEY_SHIFT_F6);
    define_key("\033[33~", KEY_SHIFT_F7);
    define_key("\033[15;2~", KEY_SHIFT_F5);
    define_key("\033[17;2~", KEY_SHIFT_F6);
    define_key("\033[18;2~", KEY_SHIFT_F7);
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
}

#include <ncurses.h>
#include <string.h>

// Global windows
WINDOW *win1;
WINDOW *win2;

void draw_buttons(int maxY, int maxX) {

    char *buttons[] = {"Sort", "View", "Edit", "Copy", "Move", "Mkdir", "Del", "Refresh", "Quit"};
    int num_buttons = sizeof(buttons) / sizeof(char *);

    int total_width = maxX;
    int button_width = total_width / num_buttons;

    int extra_space = total_width - (button_width * num_buttons);

    int x = 0;
    for (int i = 0; i < num_buttons; ++i) {
        int extra = 0;
        if (extra_space > 0) {
            extra = 1;
            extra_space--;
        }

        mvprintw(maxY - 1, x, "F%d", i + 2);
        attron(A_REVERSE);
        mvprintw(maxY - 1, x + 2, "%-*s", button_width - 2 + extra, buttons[i]);
        attroff(A_REVERSE);

        x += button_width + extra;
    }
}

void draw_windows() {
    // Refresh stdscr to ensure it's updated
    refresh();

    // Get screen dimensions
    int maxY, maxX;
    getmaxyx(stdscr, maxY, maxX);

    // Calculate window dimensions
    int winHeight = maxY - 4;
    int winWidth1 = maxX / 2;
    int winWidth2 = maxX / 2;

    // Adjust for odd COLS
    if (maxX % 2 != 0) {
        winWidth1 += 1;
    }

    // Delete old windows
    delwin(win1);
    delwin(win2);

    // Create new windows
    win1 = newwin(winHeight, winWidth1, 1, 0);
    win2 = newwin(winHeight, winWidth2, 1, winWidth1);

    // Add borders to windows using wborder()
    wborder(win1, '|', '|', '-', '-', '+', '+', '+', '+');
    wborder(win2, '|', '|', '-', '-', '+', '+', '+', '+');

    // Refresh windows to make borders visible
    wrefresh(win1);
    wrefresh(win2);

    // Draw buttons
    draw_buttons(maxY, maxX);
}

void cleanup() {
    delwin(win1);
    delwin(win2);
    endwin();
}

int main() {
    // Initialize ncurses
    initscr();
    noecho();
    cbreak();
    keypad(stdscr, TRUE);

    // Initial drawing
    draw_windows();

    int ch;
    while ((ch = getch()) != KEY_F(10)) {
        if (ch == KEY_RESIZE) {
            // Redraw windows on resize
            draw_windows();
        }
    }

    cleanup();
    return 0;
}

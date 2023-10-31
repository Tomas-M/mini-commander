#include "includes.h"
#include "types.h"
#include "globals.h"

void cursor_to_cmd() {
    // move cursor where it belongs
    move(LINES - 2, prompt_length + cursor_pos - cmd_offset);
    curs_set(1);
}

void update_cmd() {

    attron(COLOR_PAIR(COLOR_WHITE_ON_BLACK));

    // Print username, hostname, and current directory path
    move(LINES - 2, 0);
    clrtoeol();
    printw("%s@%s:%s# ", username, unameData.nodename, active_panel->path);

    // Calculate max command display length
    prompt_length = strlen(username) + strlen(unameData.nodename) + strlen(active_panel->path) + 4;  // 5 accounts for '@', ':', '#', and spaces.
    int max_cmd_display = COLS - prompt_length;

    // Print the visible part of the command, limited to max_cmd_display characters
    printw("%.*s", max_cmd_display, cmd + cmd_offset);

    cursor_to_cmd();

    // Refresh only the changed parts
    refresh();

    return;
}


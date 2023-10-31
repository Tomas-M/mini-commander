#include "includes.h"
#include "types.h"
#include "globals.h"

WINDOW *progress;

// Function to create a dialog window with a title and two progress bars
void create_progress_dialog(int title_lines) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int width = max_x / 2;
    int height = 9 + title_lines;

    int start_y = (max_y - height) / 2;
    int start_x = (max_x - width) / 2;

    progress = newwin(height, width, start_y, start_x);
    wbkgd(progress, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    wattron(progress, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    show_shadow(progress);

    // Draw the borders and title
    mvwaddch(progress, 1, 1, '+'); // Top left corner
    mvwaddch(progress, 1, width - 2, '+'); // Top right corner
    mvwaddch(progress, height - 2, 1, '+'); // Bottom left corner
    mvwaddch(progress, height - 2, width - 2, '+'); // Bottom right corner
    mvwhline(progress, 1, 2, '-', width - 4); // Top border
    mvwhline(progress, height - 2, 2, '-', width - 4); // Bottom border
    mvwvline(progress, 2, 1, '|', height - 4); // Left border
    mvwvline(progress, 2, width - 2, '|', height - 4); // Right border
    mvwhline(progress, height - 4, 2, '-', width - 4); // Horizontal line above buttons
    mvwaddch(progress, height - 4, 1, '+'); // Left intersection
    mvwaddch(progress, height - 4, width - 2, '+'); // Right intersection

    wrefresh(progress);
}


int update_progress_dialog(char *title, int current_progress, int total_progress, char *infotext) {
    int width, height;
    getmaxyx(progress, height, width);

    if (current_progress > 100) current_progress = 100;
    if (total_progress > 100) total_progress = 100;

    static int active_button = 0;

    int title_lines = lines(title);
    if (title_lines < 1) title_lines = 1;

    static int previous_title_lines = 1;
    if (previous_title_lines < title_lines) {
        delwin(progress);
        create_progress_dialog(title_lines);
        previous_title_lines = title_lines;
    }

    // cleanup stastic var for another dialog, when calling with empty or zero arguments
    if (title == NULL && current_progress == 0 && total_progress == 0 && infotext == NULL) {
        previous_title_lines = 1;
        return -1;
    }

    // print title if provided
    if (title != NULL) {
        int title_line = 2;
        char * title_copy = strdup(title);
        char * line = strtok(title_copy, "\n");
        while (line) {
            mvwprintw(progress, title_line, 3, "%s", SHORTEN(line, width - 6));
            line = strtok(NULL, "\n");
            title_line++;
        }
        free(title_copy);
    }

    // Draw the progress bars if progress is provideed
    if (infotext == NULL) {
        mvwaddch(progress, 3 + title_lines, 3, '[');
        mvwaddch(progress, 3 + title_lines, width - 8, ']');
        mvwaddch(progress, 4 + title_lines, 3, '[');
        mvwaddch(progress, 4 + title_lines, width - 8, ']');
        mvwhline(progress, 3 + title_lines, 4, '.', width - 12);
        mvwhline(progress, 4 + title_lines, 4, '.', width - 12);

        mvwprintw(progress, 3 + title_lines, width - 7, "%3d%%", current_progress);
        mvwprintw(progress, 4 + title_lines, width - 7, "%3d%%", total_progress);

        int bar_width = width - 12;
        int current_fill = (current_progress * bar_width) / 100;
        int total_fill = (total_progress * bar_width) / 100;

        for (int i = 0; i < current_fill; i++) { mvwaddch(progress, 3 + title_lines, 4 + i, '#'); }
        for (int i = 0; i < total_fill; i++) { mvwaddch(progress, 4 + title_lines, 4 + i, '#'); }
    } else {
        int info_line = 2 + title_lines;
        char * info_copy = strdup(infotext);
        char * line = strtok(info_copy, "\n");
        while (line) {
            mvwprintw(progress, info_line, 3, "%s", SHORTEN(line, width - 6));
            line = strtok(NULL, "\n");
            info_line++;
        }
        free(info_copy);
    }

    char * buttons[] = {"Skip", "Abort", NULL};

    int move_cursor_pos = 0;
    int total_buttons_width = 0;
    int i = 0;
    while (buttons[i] != NULL) {
        total_buttons_width += strlen(buttons[i]) + 4;
        i++;
    }
    total_buttons_width += 2 * (i - 1);

    int cursor_pos = (width - total_buttons_width) / 2;
    int y_pos = 6;
    i = 0;
    while (buttons[i] != NULL) {
        if (i == active_button) {
            wattron(progress, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
            move_cursor_pos = cursor_pos + 2;
        }
        mvwprintw(progress, y_pos + title_lines, cursor_pos, "[ %s ]", buttons[i]);
        if (i == active_button) {
            wattron(progress, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        }
        cursor_pos += strlen(buttons[i]) + 6;
        i++;
    }

    if (move_cursor_pos > 0) {
        wmove(progress, y_pos + title_lines, move_cursor_pos);
    }

    wrefresh(progress);

    timeout(0);
    int ch = getch();
    timeout(-1);

    if (ch == KEY_LEFT) active_button--;
    if (ch == KEY_RIGHT) active_button++;
    if (active_button > 1) active_button = 0;
    if (active_button < 0) active_button = 1;

    if (ch == '\n') return active_button + 1;

    return -1;
}


int update_progress_dialog_delta(char *title, int current_progress, int total_progress, char *infotext) {
    static struct timeval last_time = {0};
    struct timeval current_time;
    gettimeofday(&current_time, NULL);

    if (title == NULL && current_progress == 0 && total_progress == 0 && infotext == NULL) {
        last_time.tv_sec = 0;
        last_time.tv_usec = 0;
        update_progress_dialog(title, current_progress, total_progress, infotext);
        return -1;
    }

    // Calculate the elapsed time in milliseconds
    long elapsed_ms = (current_time.tv_sec - last_time.tv_sec) * 1000 +
                      (current_time.tv_usec - last_time.tv_usec) / 1000;

    if ( current_progress == 100 || (last_time.tv_sec == 0 && last_time.tv_usec == 0) || elapsed_ms > 200) {
        int t = update_progress_dialog(title, current_progress, total_progress, infotext);
        if (t != -1) return t;
        last_time = current_time;  // update the last_time to current_time
    }

    return -1;
}


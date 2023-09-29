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

#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "types.h"
#include "globals.h"


char *find_newline(char *buffer, size_t length) {
    char *pos_r = memchr(buffer, '\r', length);
    char *pos_n = memchr(buffer, '\n', length);

    if (pos_r && pos_n) {
        if (pos_r + 1 == pos_n) {
            return pos_n; // if \r\n is encountered, break on the later
        }
        return pos_r < pos_n ? pos_r : pos_n;
    } else if (pos_r) {
        return pos_r;
    } else {
        return pos_n;
    }
}


file_lines* read_file_lines(const char *filename, int *num_lines) {
    *num_lines = 0;
    int initial_buffer_size = 4096;

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        //perror("Failed to open file");
        return NULL;
    }

    file_lines *head = NULL, *current = NULL;
    char *buffer = (char *)malloc(initial_buffer_size);
    size_t buffer_size = initial_buffer_size;
    size_t bytes_read, total_bytes_read = 0;

    while ((bytes_read = fread(buffer + total_bytes_read, 1, buffer_size - total_bytes_read, file)) > 0) {
        total_bytes_read += bytes_read;
        char *newline_pos = find_newline(buffer, total_bytes_read);

        while (newline_pos != NULL) {
            file_lines *new_line = (file_lines *)malloc(sizeof(file_lines));
            new_line->line_length = newline_pos - buffer + 1;
            new_line->line = (char *)malloc(new_line->line_length);
            memcpy(new_line->line, buffer, new_line->line_length);
            new_line->next = NULL;
            (*num_lines)++;

            if (current == NULL) {
                head = new_line;
                current = head;
            } else {
                current->next = new_line;
                current = new_line;
            }

            total_bytes_read -= new_line->line_length;
            memmove(buffer, newline_pos + 1, total_bytes_read);
            newline_pos = find_newline(buffer, total_bytes_read);
        }

        if (total_bytes_read == buffer_size) {
            buffer_size += initial_buffer_size;
            char *new_buffer = realloc(buffer, buffer_size);
            buffer = new_buffer;
        }
    }

    // Process any remaining content in the buffer into a line
    if (total_bytes_read > 0) {
        file_lines *new_line = (file_lines *)malloc(sizeof(file_lines));
        new_line->line_length = total_bytes_read;
        new_line->line = (char *)malloc(new_line->line_length);
        memcpy(new_line->line, buffer, new_line->line_length);
        new_line->next = NULL;
        (*num_lines)++;

        if (current == NULL) {
            head = new_line;
        } else {
            current->next = new_line;
        }
    }

    free(buffer);
    fclose(file);
    return head;
}

void free_file_lines(file_lines *head) {
    while (head != NULL) {
        file_lines *temp = head;
        head = head->next;
        free(temp->line);
        free(temp);
    }
}

void display_line(WINDOW *win, file_lines *line, int max_x, int current_col) {
    char *ptr = line->line;
    int offset = 0;
    while (offset < line->line_length && offset < current_col) {
        ptr++;
        offset++;
    }
    if (offset < current_col) {
        // If the line is shorter than current_col, just clear the line
        wclrtoeol(win);
    } else {
        for (int x = 0; x < max_x && offset < line->line_length; x++, ptr++, offset++) {
            if (isprint((unsigned char)*ptr)) { // Check if the character is printable
                waddch(win, *ptr);
            } else {
                waddch(win, '.'); // If not, print a "."
            }
        }
    }
    wrefresh(win);
}


int view_file(char *filename) {
    int input;
    int max_y, max_x;
    int current_line = 0;
    int current_col = 0; // Add a variable to keep track of the current column offset

    // Get the screen dimensions
    getmaxyx(stdscr, max_y, max_x);

    // Top line on screen
    WINDOW *toprow_win = newwin(1, max_x, 0, 0);
    wbkgd(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
    wattron(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));

    // Create a new window for displaying the file content
    WINDOW *content_win = newwin(max_y - 2, max_x, 1, 0);
    wbkgd(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    wattron(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    // Build the linked list of line pointers
    int num_lines;
    file_lines *lines = read_file_lines(filename, &num_lines);

    // Initial display
    file_lines *current = lines;
    for (int i = 0; i < max_y - 2 && current != NULL; i++) {
        wmove(content_win, i, 0);
        display_line(content_win, current, max_x, current_col); // Pass current_col to display_line
        current = current->next;
    }

    current = lines;
    curs_set(0);

    // Handle user input for scrolling
    while(1) {

        int shown_line_max = current_line + max_y - 2;
        if (shown_line_max > num_lines) shown_line_max = num_lines;

        // Initial top row stats
        mvwprintw(toprow_win, 0, 0, "%s", filename);
        int num_width = snprintf(NULL, 0, "   %d/%d   %d%%", shown_line_max, num_lines, 100 * shown_line_max / num_lines);
        mvwprintw(toprow_win, 0, max_x - num_width, "   %d/%d   %d%%", shown_line_max, num_lines, 100 * shown_line_max / num_lines);
        wrefresh(toprow_win);

        input = noesc(getch());
        switch (input) {
            case KEY_F(10):
            case KEY_F(3):
            case 27:
                delwin(content_win);
                free_file_lines(lines);
                curs_set(1);
                return 0;
            case KEY_UP:
                if (current_line > 0) {
                    current_line--;
                }
                break;
            case KEY_DOWN:
                if (current_line < num_lines - (max_y - 2)) {
                    current_line++;
                }
                break;
            case KEY_LEFT:
                if (current_col >= 10) {
                    current_col-=10;
                }
                break;
            case KEY_RIGHT:
                current_col+=10;
                break;
            case KEY_PPAGE: // PgUp
                current_line -= max_y - 2;
                if (current_line < 0) current_line = 0;
                break;
            case KEY_NPAGE: // PgDn
                current_line += max_y - 2;
                if (current_line > num_lines - (max_y - 2)) {
                    current_line = num_lines - (max_y - 2);
                }
                break;
            case KEY_HOME: // Handle Home key
                current_line = 0;
                break;
            case KEY_END: // Handle End key
                current_line = num_lines - (max_y - 2);
                if (current_line < 0) current_line = 0;
                break;
            case KEY_RESIZE: // Handle screen resize
                getmaxyx(stdscr, max_y, max_x); // Update max_y and max_x
                wclear(content_win); // Clear the old window
                wrefresh(content_win);
                delwin(content_win); // Delete the old window
                content_win = newwin(max_y - 2, max_x, 1, 0); // Create a new window with the new dimensions
                wbkgd(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
                wattron(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
                break;
        }

        // Update the current pointer based on current_line
        current = lines;
        for (int i = 0; i < current_line; i++) {
            current = current->next;
        }

        // Redisplay the window content
        file_lines *temp = current;
        for (int i = 0; i < max_y - 2 && temp != NULL; i++) {
            wmove(content_win, i, 0);
            wclrtoeol(content_win);
            display_line(content_win, temp, max_x, current_col); // Pass current_col to display_line
            temp = temp->next;
        }
    }

    return 0;
}

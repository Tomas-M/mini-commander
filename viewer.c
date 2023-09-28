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

// Function to build the linked list of line pointers
viewer_lines *build_line_index(char *file_content, off_t file_size, int *num_lines) {
    viewer_lines *head = NULL, *tail = NULL;
    *num_lines = 0;
    char *line_start = file_content;
    for (off_t i = 0; i < file_size; i++) {
        if (file_content[i] == '\n' || i == file_size - 1) {
            viewer_lines *new_viewer_lines = (viewer_lines *)malloc(sizeof(viewer_lines));
            new_viewer_lines->line = line_start;
            new_viewer_lines->next = NULL;
            if (tail == NULL) {
                head = tail = new_viewer_lines;
            } else {
                tail->next = new_viewer_lines;
                tail = new_viewer_lines;
            }
            line_start = file_content + i + 1;
            (*num_lines)++;
        }
    }
    return head;
}

void display_line(WINDOW *win, viewer_lines *line, int max_x, int current_col) {
    char *ptr = line->line;
    int offset = 0;
    while (*ptr != '\n' && offset < current_col) {
        ptr++;
        offset++;
    }
    if (offset < current_col) {
        // If the line is shorter than current_col, just clear the line
        wclrtoeol(win);
    } else {
        for (int x = 0; x < max_x && *ptr != '\n'; x++, ptr++) {
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

    // Open and map the file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return 1;
    }

    struct stat st;
    fstat(fd, &st);
    char *file_content = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    // Build the linked list of line pointers
    int num_lines;
    viewer_lines *lines = build_line_index(file_content, st.st_size, &num_lines);

    // Initial display
    viewer_lines *current = lines;
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
        int num_width = snprintf(NULL, 0, "   %d%%", 100 * shown_line_max / num_lines);
        mvwprintw(toprow_win, 0, max_x - num_width, "   %d%%", 100 * shown_line_max / num_lines);
        wrefresh(toprow_win);

        input = noesc(getch());
        switch (input) {
            case KEY_F(10):
            case KEY_F(3):
            case 27:
                delwin(content_win);

                // Clean up
                munmap(file_content, st.st_size);
                close(fd);
                delwin(content_win);

                // Free the linked list
                while (lines != NULL) {
                    viewer_lines *temp = lines;
                    lines = lines->next;
                    free(temp);
                }

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
                if (current_col > 0) {
                    current_col--;
                }
                break;
            case KEY_RIGHT:
                current_col++;
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
        viewer_lines *temp = current;
        for (int i = 0; i < max_y - 2 && temp != NULL; i++) {
            wmove(content_win, i, 0);
            wclrtoeol(content_win);
            display_line(content_win, temp, max_x, current_col); // Pass current_col to display_line
            temp = temp->next;
        }
    }

    return 0;
}

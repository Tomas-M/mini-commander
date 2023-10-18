// this is long line with comments  .dfc ropigop rkrpok erpogk reopg k regopker gpork pogk popo kvopdfkv odkpokrepo fkerpogk ergpoerk goperk gpeor gkeropgk erop gkj
// this is long line with comments  .dfc ropigop rkrpok erpogk reopg k regopker gpork pogk popo kvopdfkv odkpokrepo fkerpogk ergpoerk goperk gpeor gkeropgk erop gkj
// this is long line with comments  .dfc ropigop rkrpok erpogk reopg k regopker gpork pogk popo kvopdfkv odkpokrepo fkerpogk ergpoerk goperk gpeor gkeropgk erop gkj
// this is long line with comments  .dfc ropigop rkrpok erpogk reopg k regopker gpork pogk popo kvopdfkv odkpokrepo fkerpogk ergpoerk goperk gpeor gkeropgk erop gkj
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

void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode) {
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
            } else if (*ptr != '\r' && *ptr != '\n') {
                waddch(win, '.'); // If not, print a "."
            } else {
                // do not actually print CR or LF at all
            }
        }
    }
    wrefresh(win);
}


int view_file(char *filename, int editor_mode) {
    int input;
    int max_y, max_x;
    int screen_start_line = 0;
    int screen_start_col = 0; // Add a variable to keep track of the current column offset
    int cursor_row = 0;
    int cursor_col = 0;
    int skip_refresh = 0;

    // Get the screen dimensions
    getmaxyx(stdscr, max_y, max_x);

    // Top line on screen
    WINDOW *toprow_win = newwin(1, max_x, 0, 0);
    wbkgd(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
    wattron(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));

    // Create a new window for displaying the file content
    WINDOW *content_win = newwin(max_y - 2, max_x, 1, 0);

    werase(content_win); // Clear the window
    wbkgd(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE)); // Set the background color
    wrefresh(content_win); // Refresh the window to apply the changes
    wattron(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    // Build the linked list of line pointers
    int num_lines;
    file_lines *lines = read_file_lines(filename, &num_lines);

    // Initial display
    file_lines *current = lines;
    for (int i = 0; i < max_y - 2 && current != NULL; i++) {
        wmove(content_win, i, 0);
        display_line(content_win, current, max_x, screen_start_col, editor_mode); // Pass screen_start_col to display_line
        current = current->next;
    }

    current = lines;

    // Handle user input for scrolling
    while(1) {

        int shown_line_max = screen_start_line + max_y - 2;
        if (shown_line_max > num_lines) shown_line_max = num_lines;

        // Initial top row stats
        mvwprintw(toprow_win, 0, 0, "%s", filename);
        int num_width = snprintf(NULL, 0, "   %d/%d   %d%%", shown_line_max, num_lines, num_lines > 0 ? 100 * shown_line_max / num_lines : 100);
        mvwprintw(toprow_win, 0, max_x - num_width, "   %d/%d   %d%%", shown_line_max, num_lines, num_lines > 0 ? 100 * shown_line_max / num_lines : 100);
        wrefresh(toprow_win);

        if (editor_mode) {
            curs_set(1); // Make cursor visible
        } else {
            curs_set(0); // Hide cursor
        }

        move(cursor_row + 1, cursor_col);
        skip_refresh = 0;

        // globally get current line, since it may be used on many places later
        file_lines *current_line = lines;
        for (int i = 0; i < screen_start_line + cursor_row; i++) {
            current_line = current_line->next;
        }

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
                if (editor_mode && cursor_row > 0) {
                    cursor_row--;
                    skip_refresh = 1;
                } else {
                    if (screen_start_line > 0) {
                        screen_start_line--;
                    }
                }
                break;
            case KEY_DOWN:
                if (editor_mode && cursor_row < LINES - 3) {
                    cursor_row++;
                    skip_refresh = 1;
                } else {
                    if (screen_start_line < num_lines - (max_y - 2)) {
                        screen_start_line++;
                    }
                }
                break;
case KEY_LEFT:
    if (editor_mode) {
        if (cursor_col > 0) {
            cursor_col--;
            skip_refresh = 1;
        } else if (screen_start_col > 0) {
            screen_start_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            file_lines *prev_line = lines;
            for (int i = 0; i < screen_start_line + cursor_row; i++) {
                prev_line = prev_line->next;
            }
            cursor_col = prev_line->line_length;
            if (prev_line->line[prev_line->line_length - 1] == '\n') cursor_col--;
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= max_x) {
                screen_start_col = cursor_col - max_x + 1;
                cursor_col = max_x - 1;
            } else {
                screen_start_col = 0;
            }
        } else if (screen_start_line > 0) {
            screen_start_line--;
            file_lines *prev_line = lines;
            for (int i = 0; i < screen_start_line; i++) {
                prev_line = prev_line->next;
            }
            cursor_col = prev_line->line_length;
            if (prev_line->line[prev_line->line_length - 1] == '\n') cursor_col--;
            if (cursor_col < 0) cursor_col = 0;
            if (cursor_col >= max_x) {
                screen_start_col = cursor_col - max_x + 1;
                cursor_col = max_x - 1;
            } else {
                screen_start_col = 0;
            }
        }

    } else {
        if (screen_start_col > 0) screen_start_col -= 10;
        if (screen_start_col < 0) screen_start_col = 0;
    }
    break;


case KEY_RIGHT:
    if (editor_mode) {
        int absolute_cursor_col = cursor_col + screen_start_col;
        if (absolute_cursor_col < current_line->line_length - 1) {
            if (cursor_col < max_x - 1) {
                cursor_col++;
            } else {
                screen_start_col++;
            }
        } else if (current_line->next) {
            cursor_col = 0;
            if (cursor_row < max_y - 3) {
                cursor_row++;
            } else {
                screen_start_line++;
            }
            screen_start_col = 0; // Reset horizontal scrolling when moving to a new line
        }
    } else {
        screen_start_col += 10;
    }
    break;


            case KEY_PPAGE: // PgUp
                screen_start_line -= max_y - 2;
                if (screen_start_line < 0) screen_start_line = 0;
                break;

            case KEY_NPAGE: // PgDn
                screen_start_line += max_y - 2;
                if (screen_start_line > num_lines - (max_y - 2)) {
                    screen_start_line = num_lines - (max_y - 2);
                }
                break;

            case KEY_HOME: // Handle Home key
                if (editor_mode) {
                    cursor_col = 0;
                    screen_start_col = 0;
                } else {
                    screen_start_line = 0;
                }
                break;

            case KEY_END: // Handle End key
                if (editor_mode) {
                    cursor_col = current_line->line_length;
                } else {
                    screen_start_line = num_lines - (max_y - 2);
                    if (screen_start_line < 0) screen_start_line = 0;
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

            case '\n': // Enter key
            {

                int absolute_cursor_col = cursor_col + screen_start_col;

                // Split the current line into two at the cursor's position
                char *first_half = malloc(absolute_cursor_col + 1);
                char *second_half = malloc(current_line->line_length - absolute_cursor_col + 1);

                memcpy(first_half, current_line->line, absolute_cursor_col);
                first_half[absolute_cursor_col] = '\0';

                memcpy(second_half, &current_line->line[absolute_cursor_col], current_line->line_length - absolute_cursor_col);
                second_half[current_line->line_length - absolute_cursor_col] = '\0';

                // Free the original line memory
                free(current_line->line);

                // Adjust the linked list to accommodate the new line
                file_lines *new_line = malloc(sizeof(file_lines));
                new_line->line = second_half;
                new_line->line_length = current_line->line_length - absolute_cursor_col;
                new_line->next = current_line->next;

                current_line->next = new_line;
                current_line->line = first_half;
                current_line->line_length = absolute_cursor_col;

                num_lines++;

                // Move the cursor to the beginning of the next line
                if (cursor_row < max_y - 3) {
                    cursor_row++;
                    cursor_col = 0;
                    screen_start_col = 0; // Reset horizontal scrolling when moving to a new line
                } else {
                    screen_start_line++;
                    cursor_col = 0;
                    screen_start_col = 0; // Reset horizontal scrolling when moving to a new line
                }
            }
            break;


            case KEY_BACKSPACE: // Handle Backspace key
            {
                int absolute_cursor_col = cursor_col + screen_start_col;

                if (absolute_cursor_col > 0) {
                    // Remove the character to the left of the cursor
                    memmove(&current_line->line[absolute_cursor_col - 1], &current_line->line[absolute_cursor_col], current_line->line_length - absolute_cursor_col);
                    current_line->line_length--;
                    char *new_line = realloc(current_line->line, current_line->line_length);
                    current_line->line = new_line;

                    // Move the cursor to the left
                    if (cursor_col > 0) {
                        cursor_col--;
                    } else {
                        screen_start_col--;
                    }
                } else if (cursor_row > 0) {
                    // Merge the current line with the previous line
                    file_lines *prev_line = lines;
                    for (int i = 0; i < screen_start_line + cursor_row - 1; i++) {
                        prev_line = prev_line->next;
                    }

                    int original_prev_line_length = prev_line->line_length; // Store the original length before the merge

                    int new_length = prev_line->line_length + current_line->line_length;
                    char *merged_line = realloc(prev_line->line, new_length);
                    memcpy(&merged_line[prev_line->line_length], current_line->line, current_line->line_length);
                    prev_line->line = merged_line;
                    prev_line->line_length = new_length;
                    prev_line->next = current_line->next;
                    free(current_line->line);
                    free(current_line);
                    current_line = prev_line;
                    num_lines--;

                    cursor_row--;
                    cursor_col = original_prev_line_length - screen_start_col;
                }
            }
            break;

            case KEY_DC: // Handle Delete key
            {
                int absolute_cursor_col = cursor_col + screen_start_col;

                if (absolute_cursor_col < current_line->line_length) {
                    // Remove the character at the cursor position
                    memmove(&current_line->line[absolute_cursor_col], &current_line->line[absolute_cursor_col + 1], current_line->line_length - absolute_cursor_col - 1);
                    current_line->line_length--;
                    char *new_line = realloc(current_line->line, current_line->line_length);
                    current_line->line = new_line;
                } else if (current_line->next) {
                    // Merge the current line with the next line
                    int new_length = current_line->line_length + current_line->next->line_length;
                    char *merged_line = realloc(current_line->line, new_length);
                    memcpy(&merged_line[current_line->line_length], current_line->next->line, current_line->next->line_length);
                    current_line->line = merged_line;
                    current_line->line_length = new_length;
                    file_lines *temp = current_line->next;
                    current_line->next = temp->next;
                    free(temp->line);
                    free(temp);
                    num_lines--;
                }
            }
            break;
        }


        // insert character where it belongs
        if (editor_mode && isprint(input)) {
            // Reallocate memory for the new character
            char *new_line = realloc(current_line->line, current_line->line_length + 1);
            current_line->line = new_line;
            memmove(&current_line->line[cursor_col + 1], &current_line->line[cursor_col], current_line->line_length - cursor_col);
            current_line->line[cursor_col] = input;
            current_line->line_length++;

            // Move the cursor to the right after inserting the character
            if (cursor_col < max_x - 1) {
                cursor_col++;
            } else {
                screen_start_col++;
            }
        }


        if (editor_mode) {


            // re-get current line, since it may have changed
            file_lines *current_line = lines;
            for (int i = 0; i < screen_start_line + cursor_row; i++) {
                current_line = current_line->next;
            }

            int absolute_cursor_col = cursor_col + screen_start_col;

            int visual_adjustment = 0;
            if (current_line->line_length > 1 &&
                current_line->line[current_line->line_length - 2] == '\r' &&
                current_line->line[current_line->line_length - 1] == '\n') {
                visual_adjustment = 2;
            } else if (current_line->line_length > 0 &&
                       current_line->line[current_line->line_length - 1] == '\n') {
                visual_adjustment = 1;
            }

            if (absolute_cursor_col > current_line->line_length - visual_adjustment) {
                cursor_col = current_line->line_length - visual_adjustment;
            }
        }

        if (!skip_refresh) {
            curs_set(0); // Hide cursor

            // Update the current pointer based on screen_start_line
            current = lines;
            for (int i = 0; i < screen_start_line; i++) {
                current = current->next;
            }

            // Redisplay the window content
            file_lines *temp = current;
            for (int i = 0; i < max_y - 2 && temp != NULL; i++) {
                wmove(content_win, i, 0);
                wclrtoeol(content_win);
                display_line(content_win, temp, max_x, screen_start_col, editor_mode);
                temp = temp->next;
            }
        }


        if (editor_mode) {
            wmove(content_win, cursor_row, cursor_col);
        }
    }

    return 0;
}

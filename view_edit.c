#include "includes.h"
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



int write_file_lines(const char *filename, file_lines *lines) {
    char temp_filename[strlen(filename) + 10];  // Space for ".tmpN\0"
    int counter = 0;

    do {
        snprintf(temp_filename, sizeof(temp_filename), "%s.tmp%d", filename, counter++);
    } while (access(temp_filename, F_OK) != -1);

    int fd = open(temp_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) return -1;

    file_lines *current = lines;
    while (current) {
        write(fd, current->line, current->line_length);
        if (current->next) write(fd, "\n", 1);
        current = current->next;
    }

    close(fd);
    return rename(temp_filename, filename) == 0 ? 0 : -1;
}



file_lines* read_file_lines(const char *filename, off_t *num_lines, off_t *num_bytes) {
    // Initialize linked list and counters
    file_lines *head = NULL, *tail = NULL;
    *num_lines = 0;

    // Open the file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    // Get the file size
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return NULL;
    }

    *num_bytes = sb.st_size;

    // Handle empty file scenario separately
    if (sb.st_size == 0) {
        head = malloc(sizeof(file_lines));
        head->line = malloc(0);
        head->line_length = 0;
        head->next = NULL;
        *num_lines = 1;
        return head;
    }


    // Memory map the file
    char *file_in_memory = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    char *line_start = file_in_memory;

    // Iterate through mapped memory

    // Iterate through mapped memory
    for (char *current = file_in_memory; current <= file_in_memory + sb.st_size; ++current) {
        // Check for end of file or newline character
        if (current == file_in_memory + sb.st_size || *current == '\n') {
            file_lines *new_node = malloc(sizeof(file_lines));

            // Allocate memory for the line data and copy it from the mapped memory
            int line_length = current - line_start;
            char *line_copy = malloc(line_length);
            memcpy(line_copy, line_start, line_length);

            // Fill in the new node
            new_node->line = line_copy;
            new_node->line_length = line_length;
            new_node->next = NULL;

            // Append to the linked list
            if (!head) {
                head = new_node;
            } else {
                tail->next = new_node;
            }
            tail = new_node;

            // Prepare for next block
            line_start = current + 1;
            (*num_lines)++;
        }
    }

    // Clean up
    munmap(file_in_memory, sb.st_size);
    close(fd);

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

void free_pattern_regexes(PatternColorPair* patterns, int num_patterns) {
    for (int i = 0; i < num_patterns; i++) {
        regfree(&patterns[i].regex);  // Free each compiled regex
    }
}

void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode, PatternColorPair* patterns, int num_patterns) {
    char *ptr = line->line;
    int offset = 0;
    regmatch_t pmatch[1];

    while (offset < line->line_length && offset < current_col) {
        ptr++;
        offset++;
    }

    if (offset < current_col) {
        wclrtoeol(win);
    } else {
        for (int x = 0; x < max_x && offset < line->line_length; x++, ptr++, offset++) {

            int matched = 0;
            // copy line to temporary buffer for matching, to make sure it is terminated by zero byte even if the original is not
            int remaining_length = line->line_length - offset;
            char *temp_str = strndup(ptr, remaining_length);

            for (int i = 0; i < num_patterns; i++) {
                if (regexec(&patterns[i].regex, temp_str, 1, pmatch, 0) == 0 && pmatch[0].rm_so == 0) {
                    matched = 1;
                    wattron(win, patterns[i].color_pair);
                    if (patterns[i].is_bold) {
                        wattron(win, A_BOLD);
                    }

                    for (int j = 0; j < pmatch[0].rm_eo; j++) {
                        waddch(win, ptr[j]);
                        if (j < pmatch[0].rm_eo - 1) {
                            x++;
                            offset++;
                        }
                    }

                    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
                    wattroff(win, A_BOLD);
                    ptr += pmatch[0].rm_eo - 1;
                    break;  // Stop checking other patterns after the first match
                }
            }

            free(temp_str);
            if (matched) continue;

            if (isprint((unsigned char)*ptr)) {
                waddch(win, *ptr);
            } else if (*ptr != '\n') {
                if (*ptr == 9) {
                    wattron(win, COLOR_PAIR(COLOR_CYAN_ON_BLUE));
                    waddch(win, '>');
                    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
                } else {
                    if (editor_mode) {
                        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
                        waddch(win, *ptr >= 0 && *ptr < 32 ? '@' + *ptr : '.');
                        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
                    } else {
                        waddch(win, '.');
                    }
                }
            }
        }
    }

    wrefresh(win);
}


int view_edit_file(char *filename, int editor_mode) {
    int input;
    int max_y, max_x;
    int screen_start_line = 0;
    int screen_start_col = 0; // Add a variable to keep track of the current column offset
    int cursor_row = 0;
    int cursor_col = 0;
    int skip_refresh = 0;
    int is_modified = 0;
    PatternColorPair patterns[100] = {0};
    int num_patterns = 0;
    char find_str[CMD_MAX] = {0};

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
    off_t num_lines, num_bytes;
    file_lines *lines = read_file_lines(filename, &num_lines, &num_bytes);

    // Extract file extension
    char *file_type = strrchr(filename, '.');  // find last '.' in filename

    if (editor_mode)
    {
        // Syntax highlighting for editor mode
        if (file_type && (strcmp(file_type, ".c") == 0 || strcmp(file_type, ".h") == 0)) {
            patterns[num_patterns++] = (PatternColorPair) {"\".*\"", COLOR_PAIR(COLOR_GREEN_ON_BLUE), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"^(#include|#define).*$", COLOR_PAIR(COLOR_RED_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"//.*$", COLOR_PAIR(COLOR_YELLOW_ON_BLUE), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\b(auto|break|case|char|const|continue|default|do|double|else|enum|extern|float|for|goto|if|int|long|register|return|short|signed|sizeof|static|struct|switch|typedef|union|unsigned|void|volatile|while|asm|inline|wchar_t|[.][.][.])\\b", COLOR_PAIR(COLOR_YELLOW_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"!|%|==|!=|&&|[*]|->|[+]|-|[|][|]|=|>|<|/", COLOR_PAIR(COLOR_YELLOW_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"[(){},:?]|\\[|\\]", COLOR_PAIR(COLOR_CYAN_ON_BLUE), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"[;&^~|]", COLOR_PAIR(COLOR_MAGENTA_ON_BLUE), 1, NULL};
        }

        if ((file_type && (strcmp(file_type, ".sh") == 0)) || (lines != NULL && lines->line_length > 3 && strncmp(lines->line, "#!/", 3) == 0)) {
            patterns[num_patterns++] = (PatternColorPair) {"^#!/.*", COLOR_PAIR(COLOR_CYAN_ON_BLACK), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"#.*$", COLOR_PAIR(COLOR_YELLOW_ON_BLUE), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"[;{}]", COLOR_PAIR(COLOR_CYAN_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\$[(].*[)]|\\$[{].*[}]", COLOR_PAIR(COLOR_GREEN_ON_BLUE), 0, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\$[*]|\\$@|\\$#|\\$[?]|\\$-|\\$\\$|\\$!|\\$_", COLOR_PAIR(COLOR_RED_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"2>&1|1>&2|2>|1>", COLOR_PAIR(COLOR_RED_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\$[0123456789]", COLOR_PAIR(COLOR_RED_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\$[a-zA-Z0-9_]+", COLOR_PAIR(COLOR_GREEN_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\$", COLOR_PAIR(COLOR_GREEN_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\bfunction\\b.*[(][)]", COLOR_PAIR(COLOR_MAGENTA_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"[a-zA-Z0-9_]+[(][)]", COLOR_PAIR(COLOR_MAGENTA_ON_BLUE), 1, NULL};
            patterns[num_patterns++] = (PatternColorPair) {"\\b(break|case|clear|continue|declare|done|do|echo|elif|else|esac|exit|export|fi|for|getopts|if|in|read|return|select|set|shift|source|then|trap|until|unset|wait|while)\\b", COLOR_PAIR(COLOR_YELLOW_ON_BLUE), 1, NULL};
        }

        for (int i = 0; i < num_patterns; i++) {
            regcomp(&patterns[i].regex, patterns[i].pattern, REG_EXTENDED);
    }
    }

    // Initial display
    file_lines *current = lines;
    for (int i = 0; i < max_y - 2 && current != NULL; i++) {
        wmove(content_win, i, 0);
        display_line(content_win, current, max_x, screen_start_col, editor_mode, patterns, num_patterns);
        current = current->next;
    }

    current = lines;

    // Handle user input for scrolling
    while(1) {

        int seek = 0;

        // globally get current line, since it may be used on many places later
        file_lines *current_line = lines;
        for (int i = 0; i < screen_start_line + cursor_row; i++) {
            seek += current_line->line_length + 1;
            current_line = current_line->next;
        }

        int shown_line_max = screen_start_line + max_y - 2;
        if (shown_line_max > num_lines) shown_line_max = num_lines;

        int absolute_cursor_col = cursor_col + screen_start_col;
        int absolute_cursor_row = cursor_row + screen_start_line;
        char charcode[10] = {0};

        // Initial top row stats

        if (editor_mode) {
            if (absolute_cursor_col < current_line->line_length) {
                unsigned char current_char = current_line->line[absolute_cursor_col];
                sprintf(charcode, "#%d", (int)current_char);
            } else if (seek + absolute_cursor_col >= num_bytes) {
                sprintf(charcode, "<EOF>");
            } else sprintf(charcode, "#10");
            mvwprintw(toprow_win, 0, 0, "%s   [-%s--] %3d L:[%3d+%3d %3d/%3lld] *(%4d/%lldb)   %s     ", filename, is_modified ? "M" : "-", absolute_cursor_col, screen_start_line + 1, cursor_row, absolute_cursor_row + 1, num_lines, seek + absolute_cursor_col, num_bytes, charcode);
        } else {
            mvwprintw(toprow_win, 0, 0, "%s", filename);
            int num_width = snprintf(NULL, 0, "        %d/%lld   %lld%%", shown_line_max, num_lines, num_lines > 0 ? 100 * shown_line_max / num_lines : 100);
            mvwprintw(toprow_win, 0, max_x - num_width, "        %d/%lld   %lld%%", shown_line_max, num_lines, num_lines > 0 ? 100 * shown_line_max / num_lines : 100);
        }

        wrefresh(toprow_win);

        if (editor_mode) {
            curs_set(1); // Make cursor visible
        } else {
            curs_set(0); // Hide cursor
        }

        move(cursor_row + 1, cursor_col);
        skip_refresh = 0;

        input = noesc(getch());
        switch (input) {
            case KEY_F(3):
                if (editor_mode) { 
                    // TODO: perhaps some Mark function
                } else {
                    delwin(content_win);
                    free_file_lines(lines);
                    free_pattern_regexes(patterns, num_patterns);
                    curs_set(1);
                    return 0;
                }
                break;
            case KEY_F(10):
            case 27:
            {
                if (is_modified) {
                    int btn = show_dialog(SPRINTF("File %s was modified.\nSave before close?", filename), (char *[]) {"Yes", "No", "Cancel", NULL}, 2, NULL, 0, 0);
                    if (btn == 1) {
                        write_file_lines(filename, lines);
                    }
                    if (btn != 1 && btn != 2) { // continue editing
                        break;
                    }
                }
                delwin(content_win);
                free_file_lines(lines);
                free_pattern_regexes(patterns, num_patterns);
                curs_set(1);
                return 0;
            }
            case KEY_F(2):
            {
                int btn = show_dialog(SPRINTF("Confirm save file:\n%s", filename), (char *[]) {"Save", "Cancel", NULL}, 0, NULL, 0, 0);
                if (btn == 1) {
                    write_file_lines(filename, lines);
                    is_modified = 0;
                }
                break;
            }

            case KEY_F(7): // F7 search
            case KEY_SHIFT_F7: // Shift+F7 search
            {
                int ret;
                if (strlen(find_str)==0 || input == KEY_F(7)) {
                    ret = show_dialog("Enter search string:", (char *[]) {"Find", "Cancel", NULL}, 0, find_str, 0, 0);
                } else ret = 1;

                if (ret == 1) {
                    if (strlen(find_str) == 0) break;
                    file_lines *search_line = current_line;
                    int start_column = absolute_cursor_col + 1;
                    int found_line = absolute_cursor_row;
                    int found_column = -1;

                    while (search_line) {
                        if (search_line->line_length >= strlen(find_str)) {
                            // Find the find_str in the current line starting from start_column
                            for (int pos = start_column; pos < search_line->line_length - strlen(find_str); pos++) {
                                if (strncasecmp(search_line->line + pos, find_str, strlen(find_str)) == 0) {
                                      found_column = pos;
                                      search_line = NULL;
                                      break;
                                }
                            }
                        }
                        start_column = 0;
                        if (search_line == NULL) break; // found it, stop
                        found_line++;
                        search_line = search_line->next;
                    }
                    if (found_column < 0) {
                        show_dialog("Search string not found", (char *[]) {"OK", NULL}, 0, NULL, 0, 0);
                    } else {
                        cursor_row = found_line;
                        cursor_col = found_column;
                    }
                }
                break;
            }

            case KEY_UP:
                if (editor_mode && cursor_row > 0) {
                    cursor_row--;
                    // TODO fix cursor_col position
                    skip_refresh = 1;
                } else {
                    if (screen_start_line > 0) {
                        screen_start_line--;
                    }
                }
                break;
            case KEY_DOWN:
                if (editor_mode && cursor_row < max_y - 3 && absolute_cursor_row < num_lines - 1) {
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
                    if (absolute_cursor_col < current_line->line_length) {
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
                if (editor_mode && screen_start_line == 0) {
                    cursor_row = 0;  // Move cursor to the first line
                }
                screen_start_line -= max_y - 2;
                if (screen_start_line < 0) screen_start_line = 0;
                break;

            case KEY_NPAGE: // PgDn
                if (editor_mode && screen_start_line + max_y > num_lines - 2) {
                    cursor_row = num_lines - screen_start_line - 1;  // Move cursor to the last line
                    if (cursor_row > max_y - 3) cursor_row = max_y - 3;
                }

                screen_start_line += max_y - 2;
                if (screen_start_line > num_lines - (max_y - 2)) {
                    screen_start_line = num_lines - (max_y - 2);
                }
                if (screen_start_line < 0) screen_start_line = 0;

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
                    if (cursor_col >= max_x) {
                        screen_start_col = cursor_col - max_x + 1;
                        cursor_col = max_x - 1;
                    }
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
                if (editor_mode) {
                    is_modified = 1;
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
                    num_bytes++;

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
            }
            break;


            case KEY_BACKSPACE: // Handle Backspace key
            {
                if (editor_mode) {
                    is_modified = 1;
                    if (absolute_cursor_col > 0) {
                        // Remove the character to the left of the cursor
                        memmove(&current_line->line[absolute_cursor_col - 1], &current_line->line[absolute_cursor_col], current_line->line_length - absolute_cursor_col);
                        current_line->line_length--;
                        char *new_line = realloc(current_line->line, current_line->line_length);
                        current_line->line = new_line;
                        num_bytes--;

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
                        num_bytes--;

                        cursor_row--;
                        cursor_col = original_prev_line_length - screen_start_col;
                    } else {
                        is_modified = 0;
                    }
                }
            }
            break;

            case KEY_DC: // Handle Delete key
            {
                if (editor_mode) {
                    is_modified = 1;
                    if (absolute_cursor_col < current_line->line_length) {
                        // Remove the character at the cursor position
                        memmove(&current_line->line[absolute_cursor_col], &current_line->line[absolute_cursor_col + 1], current_line->line_length - absolute_cursor_col - 1);
                        current_line->line_length--;
                        char *new_line = realloc(current_line->line, current_line->line_length);
                        current_line->line = new_line;
                        num_bytes--;
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
                        num_bytes--;
                    } else {
                        is_modified = 0;
                    }
                }
            }
            break;
        }


        if (input == KEY_MOUSE && editor_mode) {
            MEVENT event;
            if (getmouse(&event) == OK) {
                // Mouse clicked at event.y and event.x

                // Adjust based on screen start lines and columns due to scrolling
                int target_line = event.y - 1 + screen_start_line; // -1 to account for top row
                int target_col = event.x + screen_start_col;

                // Update cursor_row and cursor_col with the target values.
                cursor_row = event.y - 1;
                cursor_col = event.x;

                // Don't move cursor beyond the last line or beyond the line length
                if (target_line >= num_lines) {
                    cursor_row = num_lines - screen_start_line - 1;
                }
            }
        }


        // insert character where it belongs
        if (editor_mode && (isprint(input) || input == 9)) {
            is_modified = 1; num_bytes++;
            // Reallocate memory for the new character
            char *new_line = realloc(current_line->line, current_line->line_length + 1);
            current_line->line = new_line;
            memmove(&current_line->line[absolute_cursor_col + 1], &current_line->line[absolute_cursor_col], current_line->line_length - absolute_cursor_col);
            current_line->line[absolute_cursor_col] = input;
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

            // recalculate absolutes
            absolute_cursor_col = cursor_col + screen_start_col;

            // Don't move cursor beyond the end of the line
            if (absolute_cursor_col > current_line->line_length) {
                cursor_col = current_line->line_length - screen_start_col;
                if (cursor_col < 0) {
                    screen_start_col = current_line->line_length;
                    cursor_col = 0;
                }
                absolute_cursor_col = cursor_col + screen_start_col;
                skip_refresh = 0; // Force a refresh to update cursor position
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
            int i;
            for (i = 0; i < max_y - 2 && temp != NULL; i++) {
                wmove(content_win, i, 0);
                wclrtoeol(content_win);
                display_line(content_win, temp, max_x, screen_start_col, editor_mode, patterns, num_patterns);
                temp = temp->next;
            }

            // Clear any remaining lines on the screen
            for (; i < max_y - 2; i++) {
                wmove(content_win, i, 0);
                wclrtoeol(content_win);
            }
        }

        if (editor_mode) {
            wmove(content_win, cursor_row, cursor_col);
        }
    }

    return 0;
}

int view_file(char *filename) {
    return view_edit_file(filename, 0);
}

int edit_file(char *filename) {
    return view_edit_file(filename, 1);
}

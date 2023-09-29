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

int lines(char * title)
{
    int newlines = 0;
    for (int i = 0; title[i]; i++) {
        if (title[i] == '\n') {
            newlines++;
        }
    }
    return newlines;
}


// Function to create a dialog window with a title, buttons, and an optional text prompt
WINDOW *create_dialog(char *title, char *buttons[], int prompt_is_present) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int total_buttons = 0;
    int total_button_width = 6;
    while (buttons[total_buttons] != NULL) {
        total_button_width += strlen(buttons[total_buttons]) + 4;
        total_buttons++;
    }
    total_button_width += 2 * (total_buttons - 1);

    // Calculate the width of the longest line in the title
    int title_width = 0;
    char *title_copy = strdup(title);
    char *line = strtok(title_copy, "\n");
    while (line) {
        int line_length = strlen(line) + 4; // adding 4 for padding
        title_width = line_length > title_width ? line_length : title_width;
        line = strtok(NULL, "\n");
    }
    free(title_copy);

    int width = total_button_width > title_width ? total_button_width : title_width;

    // Ensure minimum width is half of the screen width
    width = width < max_x / 2 ? max_x / 2 : width;

    // Increase the height of the window if a prompt is present
    int height = (prompt_is_present ? 6 : 5) + lines(title);
    int start_y = (max_y - height) / 2;
    int start_x = (max_x - width) / 2;

    WINDOW *win = newwin(height, width, start_y, start_x);
    wbkgd(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));

    wborder(win, '|', '|', '-', '-', '+','+','+','+');
    mvwhline(win, height - 3, 0, '-', width);
    mvwaddch(win, height - 3, 0, '+');
    mvwaddch(win, height - 3, width - 1, '+');

    int title_line = 1;
    title_copy = strdup(title);
    line = strtok(title_copy, "\n");
    while (line) {
        mvwprintw(win, title_line, 2, "%s", line);
        line = strtok(NULL, "\n");
        title_line++;
    }
    free(title_copy);

    return win;
}

void update_dialog_buttons(WINDOW *win, char * title, char *buttons[], int selected, int prompt_present, int editing_prompt) {
    int width, height;
    getmaxyx(win, height, width);

    int total_buttons_width = 0;
    int i = 0;
    while (buttons[i] != NULL) {
        total_buttons_width += strlen(buttons[i]) + 4;
        i++;
    }
    total_buttons_width += 2 * (i - 1);

    int cursor_pos = (width - total_buttons_width) / 2;
    int move_cursor_pos = 0;

    // Adjust the y position based on whether a prompt is present
    int y_pos = prompt_present ? 4 : 3;
    i = 0;
    while (buttons[i] != NULL) {
        if (i == selected && !editing_prompt) {
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
            move_cursor_pos = cursor_pos + 2;
        }
        mvwprintw(win, y_pos + lines(title), cursor_pos, "[ %s ]", buttons[i]);
        if (i == selected && !editing_prompt) {
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        }
        cursor_pos += strlen(buttons[i]) + 6;
        i++;
    }

    if (move_cursor_pos > 0) {
        wmove(win, y_pos + lines(title), move_cursor_pos);
    }

    wrefresh(win);
}

int show_dialog(char *title, char *buttons[], char *prompt) {
    int selected = 0;
    int prompt_is_present = prompt ? 1 : 0;
    int editing_prompt = prompt ? 1 : 0;

    if (!prompt_is_present) {
        prompt = "";
    }

    WINDOW *win = create_dialog(title, buttons, prompt_is_present);
    update_dialog_buttons(win, title, buttons, selected, prompt_is_present, editing_prompt);

    int buttons_count = 0;
    while (buttons[buttons_count] != NULL) {
        buttons_count++;
    }

    int ch;
    int cursor_position = strlen(prompt);
    int prompt_offset = 0;
    int width, height;
    getmaxyx(win, height, width);
    int max_prompt_display = width - 4;
    int prompt_modified = 0;

    while (1) {
        if (editing_prompt) {
            if (cursor_position - prompt_offset >= max_prompt_display) {
                prompt_offset = cursor_position - max_prompt_display + 1;
            } else if (cursor_position < prompt_offset) {
                prompt_offset = cursor_position;
            }
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
            if (!prompt_modified) wattron(win, A_BOLD);
            mvwprintw(win, 2 + lines(title), 2, "%-*.*s", max_prompt_display, max_prompt_display, prompt + prompt_offset);
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
            if (!prompt_modified) wattroff(win, A_BOLD);
            wmove(win, 2 + lines(title), 2 + cursor_position - prompt_offset);
        }
        wrefresh(win);

        ch = getch();
        switch (ch) {
            case KEY_LEFT:
                if (editing_prompt && cursor_position > 0) {
                    cursor_position--;
                    prompt_modified = 1;
                } else if (!editing_prompt) {
                    if (selected > 0) {
                        selected--;
                    } else {
                        selected = buttons_count - 1;
                    }
                }
                break;
            case KEY_RIGHT:
                if (editing_prompt && cursor_position < strlen(prompt)) {
                    cursor_position++;
                    prompt_modified = 1;
                } else if (!editing_prompt) {
                    if (buttons[selected + 1] != NULL) {
                        selected++;
                    } else {
                        selected = 0;
                    }
                }
                break;
            case KEY_BACKSPACE:
                if (editing_prompt && cursor_position > 0) {
                    memmove(&prompt[cursor_position - 1], &prompt[cursor_position], strlen(prompt) - cursor_position + 1);
                    cursor_position--;
                    prompt_modified = 1;
                }
                break;
            case KEY_DC: // Handling the Del key
               if (editing_prompt && cursor_position < strlen(prompt)) {
                   memmove(&prompt[cursor_position], &prompt[cursor_position + 1], strlen(prompt) - cursor_position);
                   prompt_modified = 1;
               }
               break;
            case KEY_F(10):
            case 27:
                return -1;
                break;
            case KEY_UP:
            case KEY_BTAB:
                if (editing_prompt) {
                    editing_prompt = 0;
                    selected = buttons_count - 1;
                } else {
                    if (selected > 0) {
                        selected --;
                    } else {
                        if (prompt_is_present) {
                            editing_prompt = 1;
                        } else {
                            selected = buttons_count - 1;
                        }
                    }
                }
                break;
            case KEY_DOWN:
            case '\t':
                if (editing_prompt) {
                    editing_prompt = 0;
                    selected = 0;
                } else {
                    if (buttons[selected + 1] != NULL) {
                        selected++;
                    } else {
                        if (prompt_is_present) {
                            editing_prompt = 1;
                        } else {
                            selected = 0;
                        }
                    }
                }
                break;
            case '\n':
                if (editing_prompt) {
                    selected = 0;
                }
                delwin(win);
                return selected;
                break;
            default:
                if (editing_prompt && isprint(ch) && strlen(prompt) < CMD_MAX) {
                    if (!prompt_modified) { // Check if the prompt is not modified
                        strcpy(prompt, ""); // Clear the prompt
                        cursor_position = 0; // Reset the cursor position
                        prompt_modified = 1; // Set the flag to indicate the prompt is modified
                    }
                    memmove(&prompt[cursor_position + 1], &prompt[cursor_position], strlen(prompt) - cursor_position + 1);
                    prompt[cursor_position] = ch;
                    cursor_position++;
                    prompt_modified = 1;
                }
                break;
        }
        update_dialog_buttons(win, title, buttons, selected, prompt_is_present, editing_prompt);
    }
}

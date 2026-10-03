#include "includes.h"
#include "types.h"
#include "globals.h"

// Format text into a caller-owned CMD_MAX buffer without overflowing it.
char *format_text(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, CMD_MAX, format, args);
    va_end(args);
    return buffer;
}

int lines(char * title)
{
    if (title == NULL) return 0;
    int newlines = 0;
    for (int i = 0; title[i]; i++) {
        if (title[i] == '\n') {
            newlines++;
        }
    }
    if (strlen(title) != 0) newlines++;
    return newlines;
}

void show_shadow(WINDOW *win) {
    int start_y, start_x, height, width;

    int cur_y, cur_x;
    getyx(win, cur_y, cur_x);

    // Get the position and size of the window
    getbegyx(win, start_y, start_x);
    getmaxyx(win, height, width);

    WINDOW *wholescreen = dupwin(newscr);

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < 2; j++) {
            int x = start_x + width + j;
            int y = start_y + i + 1;
            chtype ch = mvwinch(wholescreen, y, x);
            mvaddch(y, x, (ch & A_CHARTEXT));
        }
    }

    for (int i = 2; i < width + 2; i++) {
        int x = start_x + i;
        int y = start_y + height;
        chtype ch = mvwinch(wholescreen, y, x);
        mvaddch(y, x, (ch & A_CHARTEXT));
    }

    delwin(wholescreen);
    wmove(win, cur_y, cur_x);
    refresh();
}


// Function to create a dialog window with a title, buttons, and an optional text prompt
WINDOW *create_dialog(char *title, char *buttons[], int prompt_is_present, int is_danger, int vertical_buttons) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int total_buttons = 0;
    int total_button_width = 6;
    while (buttons[total_buttons] != NULL) {
        total_button_width += strlen(buttons[total_buttons]) + 4;
        total_buttons++;
    }
    total_button_width += 2 * (total_buttons - 1);

    int title_width = 0;
    char *title_copy = strdup(title);
    char *line = strtok(title_copy, "\n");
    while (line) {
        int line_length = strlen(line) + 4;
        title_width = line_length > title_width ? line_length : title_width;
        line = strtok(NULL, "\n");
    }
    free(title_copy);

    int width;
    if (vertical_buttons) {
        int longest_button_width = 0;
        for (int i = 0; buttons[i] != NULL; i++) {
            int button_width = strlen(buttons[i]) + 10;
            if (button_width > longest_button_width) {
                longest_button_width = button_width;
            }
        }
        width = title_width > longest_button_width ? title_width: longest_button_width;
    } else {
        width = total_button_width > title_width ? total_button_width : title_width;
        if (!is_danger) {
            width = width < max_x / 3 ? max_x / 3 : width;
        }
    }

    width = width + 2;

    int height = (prompt_is_present ? 6 : 5) + lines(title) + 1;
    if (vertical_buttons) {
        height += total_buttons - 1; // Increase height by total_buttons - 1 if vertical layout
    }

    int start_y = (max_y - height) / 2 - (is_danger ? 8 : 0);
    int start_x = (max_x - width) / 2;

    // Increase the size of the window by 2 in both dimensions
    WINDOW *win = newwin(height, width, start_y, start_x);

    if (is_danger) {
        wbkgd(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
        wattron(win, A_BOLD);
    } else {
        wbkgd(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    }

    show_shadow(win);

    mvwaddch(win, 1, 1, '+'); // Top left corner
    mvwaddch(win, 1, width - 2, '+'); // Top right corner
    mvwaddch(win, height - 2, 1, '+'); // Bottom left corner
    mvwaddch(win, height - 2, width - 2, '+'); // Bottom right corner
    mvwhline(win, 1, 2, '-', width - 4); // Top border
    mvwhline(win, height - 2, 2, '-', width - 4); // Bottom border
    mvwvline(win, 2, 1, '|', height - 4); // Left border
    mvwvline(win, 2, width - 2, '|', height - 4); // Right border
    mvwhline(win, height - 4 - (vertical_buttons ? total_buttons - 1: 0), 2, '-', width - 4); // Horizontal line above buttons
    mvwaddch(win, height - 4 - (vertical_buttons ? total_buttons - 1: 0), 1, '+'); // Left intersection
    mvwaddch(win, height - 4 - (vertical_buttons ? total_buttons - 1: 0), width - 2, '+'); // Right intersection

    int title_line = 2;
    title_copy = strdup(title);
    line = strtok(title_copy, "\n");
    while (line) {
        mvwprintw(win, title_line, 3, "%s", line);
        line = strtok(NULL, "\n");
        title_line++;
    }
    free(title_copy);

    return win;
}


void update_dialog_buttons(WINDOW *win, char * title, char *buttons[], int selected, int prompt_present, int editing_prompt, int is_danger, int vertical_buttons) {
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
    if (vertical_buttons) cursor_pos = 3;
    int move_cursor_pos_x = 0;
    int move_cursor_pos_y = 0;

    // Adjust the y position based on whether a prompt is present
    int y_pos = prompt_present ? 4 : 3;
    i = 0;
    while (buttons[i] != NULL) {
        if (i == selected && !editing_prompt) {
            if (is_danger) {
                wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
                wattroff(win, A_BOLD);
            } else {
                wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN_BTN));
            }
            move_cursor_pos_x = cursor_pos + 2;
            move_cursor_pos_y = y_pos;
        }
        if (vertical_buttons) {
            mvwprintw(win, y_pos + lines(title), cursor_pos, " [%s] %-*s ", i == selected && !editing_prompt ? "x" : " ", width - 12, buttons[i]);
        } else {
            mvwprintw(win, y_pos + lines(title), cursor_pos, "[ %s ]", buttons[i]);
        }
        if (i == selected && !editing_prompt) {
            if (is_danger) {
                wattron(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
                wattron(win, A_BOLD);
            } else {
                wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
            }
        }

        if (vertical_buttons) {
            y_pos += 1; // Move to the next line for vertical layout
        } else {
            cursor_pos += strlen(buttons[i]) + 6;
        }
        i++;
    }

    if (move_cursor_pos_x > 0 || move_cursor_pos_y > 0) {
        wmove(win, move_cursor_pos_y + lines(title), move_cursor_pos_x);
    }

    wrefresh(win);
}

WINDOW *dialog_saved_screen;

void dialog_save_screen() {
   dialog_saved_screen = dupwin(newscr);
}

void dialog_restore_screen() {
   overwrite(dialog_saved_screen, newscr);
   wrefresh(newscr);
   delwin(dialog_saved_screen);
}


int show_dialog(char *title, char *buttons[], int selected, char *prompt, int is_danger, int vertical_buttons, int edit_prompt) {
    int prompt_is_present = prompt ? 1 : 0;
    int editing_prompt = prompt ? 1 : 0;

    if (!prompt_is_present) {
        prompt = "";
    }

    dialog_save_screen();

    WINDOW *win = create_dialog(title, buttons, prompt_is_present, is_danger, vertical_buttons);
    keypad(win, TRUE);
    update_dialog_buttons(win, title, buttons, selected, prompt_is_present, editing_prompt, is_danger, vertical_buttons);

    int buttons_count = 0;
    while (buttons[buttons_count] != NULL) {
        buttons_count++;
    }

    int ch;
    int cursor_position = strlen(prompt);
    int prompt_offset = 0;
    int width, height;
    getmaxyx(win, height, width);
    int max_prompt_display = width - 6;
    // Shifted file actions start with an editable name instead of a selected default.
    int prompt_modified=edit_prompt;

    while (1) {
        if (editing_prompt) {
            int column=text_column(prompt, cursor_position);
            cursor_position=text_offset(prompt, strlen(prompt), column);
            if (column-prompt_offset >= max_prompt_display) prompt_offset=column-max_prompt_display+1;
            if (column < prompt_offset) prompt_offset=column;
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN_PMPT));
            if (!prompt_modified) wattron(win, A_BOLD);
            mvwhline(win, 2+lines(title), 3, ' ', max_prompt_display);
            int length=strlen(prompt), x=0, cell_width;
            for (int offset=0; offset < length && x < prompt_offset+max_prompt_display;)
            {
                wchar_t chars[CCHARW_MAX];
                offset+=text_cell(prompt+offset, length-offset, chars, &cell_width);
                if (!iswprint(chars[0])) chars[0]=L'.';
                if (x >= prompt_offset && x+cell_width <= prompt_offset+max_prompt_display)
                {
                    cchar_t cell;
                    setcchar(&cell, chars, prompt_modified ? 0 : A_BOLD, COLOR_BLACK_ON_CYAN_PMPT, NULL);
                    mvwadd_wchnstr(win, 2+lines(title), 3+x-prompt_offset, &cell, 1);
                }
                x+=cell_width;
            }
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
            if (!prompt_modified) wattroff(win, A_BOLD);
            wmove(win, 2+lines(title), 3+column-prompt_offset);
        }
        wrefresh(win);

        char input_text[MB_LEN_MAX+1];
        ch=read_text_key(win, input_text);
        switch (ch) {
            case KEY_LEFT:
                if (editing_prompt && cursor_position > 0) {
                    cursor_position=text_previous(prompt, cursor_position);
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
                if (editing_prompt) {
                    if (cursor_position < strlen(prompt)) {
                        wchar_t chars[CCHARW_MAX];
                        int cell_width;
                        cursor_position+=text_cell(prompt+cursor_position, strlen(prompt)-cursor_position, chars, &cell_width);
                    }
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
                    int previous=text_previous(prompt, cursor_position);
                    memmove(prompt+previous, prompt+cursor_position, strlen(prompt)-cursor_position+1);
                    cursor_position=previous;
                    prompt_modified = 1;
                }
                break;
            case KEY_DC: // Handling the Del key
               if (editing_prompt && cursor_position < strlen(prompt)) {
                   wchar_t chars[CCHARW_MAX];
                   int cell_width, bytes=text_cell(prompt+cursor_position, strlen(prompt)-cursor_position, chars, &cell_width);
                   memmove(prompt+cursor_position, prompt+cursor_position+bytes, strlen(prompt)-cursor_position-bytes+1);
                   prompt_modified = 1;
               }
               break;
            case KEY_F(10):
            case 27:
                delwin(win);
                dialog_restore_screen();
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
            case KEY_HOME:
                if (editing_prompt) {
                    cursor_position = 0;
                    prompt_modified = 1;
                }
                break;
            case KEY_END:
                if (editing_prompt) {
                    cursor_position = strlen(prompt);
                    prompt_modified = 1;
                }
                break;
            case '\n':
                if (editing_prompt) {
                    selected = 0;
                }
                delwin(win);
                dialog_restore_screen();
                return selected + 1;
                break;
            default:
                if (editing_prompt && input_text[0]) {
                    if (!prompt_modified) { // Check if the prompt is not modified
                        strcpy(prompt, ""); // Clear the prompt
                        cursor_position = 0; // Reset the cursor position
                        prompt_modified = 1; // Set the flag to indicate the prompt is modified
                    }
                    int bytes=strlen(input_text);
                    if (strlen(prompt)+bytes >= CMD_MAX) break;
                    memmove(prompt+cursor_position+bytes, prompt+cursor_position, strlen(prompt)-cursor_position+1);
                    memcpy(prompt+cursor_position, input_text, bytes);
                    cursor_position+=bytes;
                    prompt_modified = 1;
                }
                break;
        }
        update_dialog_buttons(win, title, buttons, selected, prompt_is_present, editing_prompt, is_danger, vertical_buttons);
    }
}


void show_errormsg(char * msg) {
    show_dialog(msg, (char *[]) {"OK", NULL}, 0, NULL, 1, 0, 0);
}

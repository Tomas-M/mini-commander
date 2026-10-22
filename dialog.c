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
    if (!win) return;
    int start_y, start_x, height, width;

    int cur_y, cur_x;
    getyx(win, cur_y, cur_x);

    // Get the position and size of the window
    getbegyx(win, start_y, start_x);
    getmaxyx(win, height, width);

    WINDOW *wholescreen = dupwin(newscr);
    if (!wholescreen) return;

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
        total_button_width += text_column(buttons[total_buttons], strlen(buttons[total_buttons]), 1) + 4;
        total_buttons++;
    }
    total_button_width += 2 * (total_buttons - 1);

    int title_width = 0;
    char *title_copy = strdup(title);
    if (!title_copy) return NULL;
    char *line = strtok(title_copy, "\n");
    while (line) {
        int line_length = text_column(line, strlen(line), 1) + 4;
        title_width = line_length > title_width ? line_length : title_width;
        line = strtok(NULL, "\n");
    }
    free(title_copy);

    int width;
    if (vertical_buttons) {
        int longest_button_width = 0;
        for (int i = 0; buttons[i] != NULL; i++) {
            int button_width = text_column(buttons[i], strlen(buttons[i]), 1) + 10;
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

    if (max_x < 10 || height > max_y) return NULL;
    if (width > max_x) width=max_x;

    int start_y = (max_y - height) / 2 - (is_danger ? 8 : 0);
    if (start_y < 0) start_y=0;
    int start_x = (max_x - width) / 2;

    // Increase the size of the window by 2 in both dimensions
    WINDOW *win = newwin(height, width, start_y, start_x);
    if (!win) return NULL;

    if (is_danger) {
        wbkgd(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_RED));
        wattron(win, A_BOLD);
    } else {
        wbkgd(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    }

    show_shadow(win);

    draw_dialog_frame(win, height-4-(vertical_buttons ? total_buttons-1 : 0));
    draw_dialog_text(win, 2, title);

    return win;
}


typedef struct { int x, y, width; } dialog_button;

// Use the same terminal-cell bounds for drawing buttons and handling mouse clicks.
static dialog_button dialog_button_bounds(WINDOW *win, char *title, char *buttons[], int index, int prompt, int vertical)
{
    int y=(prompt ? 4 : 3)+lines(title);
    if (vertical) return (dialog_button){3, y+index, getmaxx(win)-6};
    int total=-2, before=0;
    for (int i=0; buttons[i]; i++)
    {
        int width=text_column(buttons[i], strlen(buttons[i]), 1)+6;
        total+=width;
        if (i < index) before+=width;
    }
    return (dialog_button){(getmaxx(win)-total)/2+before, y, text_column(buttons[index], strlen(buttons[index]), 1)+4};
}

void update_dialog_buttons(WINDOW *win, char * title, char *buttons[], int selected, int prompt_present, int editing_prompt, int is_danger, int vertical_buttons) {
    int move_cursor_pos_x = 0;
    int move_cursor_pos_y = 0;

    for (int i=0; buttons[i]; i++) {
        dialog_button button=dialog_button_bounds(win, title, buttons, i, prompt_present, vertical_buttons);
        attr_t attributes;
        short color;
        wattr_get(win, &attributes, &color, NULL);
        if (i == selected && !editing_prompt) {
            wattr_set(win, is_danger ? attributes & ~A_BOLD : attributes,
                is_danger ? COLOR_BLACK_ON_WHITE : COLOR_BLACK_ON_CYAN_BTN, NULL);
            move_cursor_pos_x=button.x+2;
            move_cursor_pos_y=button.y;
        }
        if (vertical_buttons) {
            mvwprintw(win, button.y, button.x, " [%s] ", i == selected && !editing_prompt ? "x" : " ");
            mvwhline(win, button.y, button.x+5, ' ', button.width-5);
            draw_text(win, button.y, button.x+5, buttons[i], 0, button.width-6);
        } else {
            mvwprintw(win, button.y, button.x, "[ %s ]", buttons[i]);
        }
        wattr_set(win, attributes, color, NULL);
    }

    if (move_cursor_pos_x > 0 || move_cursor_pos_y > 0) {
        wmove(win, move_cursor_pos_y, move_cursor_pos_x);
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
    // Combine quick press/release pairs even when the editor requests raw drag events.
    int saved_mouseinterval=mouseinterval(50);

    if (!prompt_is_present) {
        prompt = "";
    }

    dialog_save_screen();

    WINDOW *win = create_dialog(title, buttons, prompt_is_present, is_danger, vertical_buttons);
    if (!win) {
        mouseinterval(saved_mouseinterval);
        dialog_restore_screen();
        return -1;
    }
    keypad(win, TRUE);
    update_dialog_buttons(win, title, buttons, selected, prompt_is_present, editing_prompt, is_danger, vertical_buttons);

    int buttons_count = 0;
    while (buttons[buttons_count] != NULL) {
        buttons_count++;
    }

    int ch, pressed_button=-1;
    int cursor_position = strlen(prompt);
    int prompt_offset = 0;
    int width=getmaxx(win);
    int max_prompt_display = width - 6;
    // Shifted file actions start with an editable name instead of a selected default.
    int prompt_modified=edit_prompt;

    while (1) {
        if (editing_prompt) {
            int column=text_column(prompt, cursor_position, 1);
            cursor_position=text_offset(prompt, strlen(prompt), column, 1);
            if (column-prompt_offset >= max_prompt_display) prompt_offset=column-max_prompt_display+1;
            if (column < prompt_offset) prompt_offset=column;
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN_PMPT));
            if (!prompt_modified) wattron(win, A_BOLD);
            mvwhline(win, 2+lines(title), 3, ' ', max_prompt_display);
            draw_text(win, 2+lines(title), 3, prompt, prompt_offset, max_prompt_display);
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
            if (!prompt_modified) wattroff(win, A_BOLD);
            wmove(win, 2+lines(title), 3+column-prompt_offset);
        }
        wrefresh(win);

        char input_text[MB_LEN_MAX+1];
        MEVENT event;
        ch=read_text_key(win, input_text, &event);
        if (ch != KEY_MOUSE) pressed_button=-1;
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
                        cursor_position=text_next(prompt, strlen(prompt), cursor_position);
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
                   int bytes=text_next(prompt, strlen(prompt), cursor_position)-cursor_position;
                   memmove(prompt+cursor_position, prompt+cursor_position+bytes, strlen(prompt)-cursor_position-bytes+1);
                   prompt_modified = 1;
               }
               break;
            case KEY_F(10):
            case 27:
                mouseinterval(saved_mouseinterval);
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
            case KEY_MOUSE:
            {
                int clicked=event.bstate & (BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED);
                if (!clicked && !(event.bstate & (BUTTON1_PRESSED|BUTTON1_RELEASED))) break;
                int hit=-1, x=event.x-getbegx(win), y=event.y-getbegy(win);
                if (prompt_is_present && y == 2+lines(title) && x >= 3 && x < width-3)
                {
                    editing_prompt=prompt_modified=1;
                    cursor_position=text_offset(prompt, strlen(prompt), prompt_offset+x-3, 1);
                    pressed_button=-1;
                    break;
                }
                for (int i=0; i < buttons_count; i++)
                {
                    dialog_button button=dialog_button_bounds(win, title, buttons, i, prompt_is_present, vertical_buttons);
                    if (y == button.y && x >= button.x && x < button.x+button.width) { hit=i; break; }
                }
                if (event.bstate & BUTTON1_PRESSED)
                {
                    pressed_button=hit;
                    if (hit >= 0) { selected=hit; editing_prompt=0; }
                }
                if (event.bstate & BUTTON1_RELEASED)
                {
                    clicked=hit >= 0 && hit == pressed_button;
                    pressed_button=-1;
                }
                if (!clicked || hit < 0) break;
                selected=hit;
                editing_prompt=0;
                // Fall through to the same action and cleanup as Enter.
            }
            case '\n':
                if (editing_prompt) {
                    selected = 0;
                }
                mouseinterval(saved_mouseinterval);
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

// A mark selects a bit for batch changes; the checkbox is that bit's desired value.
static const struct { mode_t bit; const char *label; int hotkey; } chmod_permissions[] = {
    {S_ISUID, "set user ID on execution", 4},
    {S_ISGID, "set group ID on execution", 4},
    {S_ISVTX, "sticky bit", 5},
    {S_IRUSR, "read by owner", 0},
    {S_IWUSR, "write by owner", 0},
    {S_IXUSR, "execute/search by owner", 1},
    {S_IRGRP, "read by group", 3},
    {S_IWGRP, "write by group", 13},
    {S_IXGRP, "execute/search by group", 5},
    {S_IROTH, "read by others", 5},
    {S_IWOTH, "write by others", 2},
    {S_IXOTH, "execute/search by others", 13}
};

typedef struct {
    const char *label;
    int action, hotkey;
    dialog_button bounds;
} chmod_button;

// Align both columns of actions across rows, including the wider default button.
static void chmod_button_pair(chmod_button *buttons, int width, int row)
{
    int first=strlen(buttons[0].label)+(buttons[0].action == CHMOD_SET ? 6 : 4);
    int second=strlen(buttons[1].label)+4;
    buttons[0].bounds=(dialog_button){width/2-first, row, first};
    buttons[1].bounds=(dialog_button){width/2+1, row, second};
}

static void draw_framed_box(WINDOW *win, int y, int x, int height, int width)
{
    mvwhline(win, y, x+1, ACS_HLINE, width-2);
    mvwhline(win, y+height-1, x+1, ACS_HLINE, width-2);
    mvwvline(win, y+1, x, ACS_VLINE, height-2);
    mvwvline(win, y+1, x+width-1, ACS_VLINE, height-2);
    mvwaddch(win, y, x, ACS_ULCORNER);
    mvwaddch(win, y, x+width-1, ACS_URCORNER);
    mvwaddch(win, y+height-1, x, ACS_LLCORNER);
    mvwaddch(win, y+height-1, x+width-1, ACS_LRCORNER);
}

static void draw_dialog_separator(WINDOW *win, int row)
{
    mvwhline(win, row, 2, ACS_HLINE, getmaxx(win)-4);
    mvwaddch(win, row, 1, ACS_LTEE);
    mvwaddch(win, row, getmaxx(win)-2, ACS_RTEE);
}

static void draw_dialog_hotkey(WINDOW *win, int row, int x, const char *label, int hotkey, int focused)
{
    int normal=COLOR_PAIR(focused ? COLOR_BLACK_ON_CYAN_BTN : COLOR_BLACK_ON_WHITE);
    wattrset(win, normal);
    mvwaddstr(win, row, x, label);
    wattrset(win, A_BOLD | COLOR_PAIR(focused ? COLOR_BLUE_ON_CYAN : COLOR_BLUE_ON_WHITE));
    mvwaddch(win, row, x+hotkey, label[hotkey]);
    wattrset(win, normal);
}

int show_chmod_dialog(const char *name, const struct stat *info, mode_t *mode, mode_t *marked, int multiple)
{
    int name_width=text_column(name, strlen(name), 1);
    int width=32+(name_width+3 > 24 ? name_width+3 : 24)+7;
    int height=multiple ? 23 : 20;
    if (width > getmaxx(stdscr)) width=getmaxx(stdscr);
    if (width < 63 || getmaxy(stdscr) < height) {
        show_errormsg(SPRINTF("Chmod needs a terminal of at least 63 x %d cells", height));
        return CHMOD_CANCEL;
    }

    dialog_save_screen();
    WINDOW *win=newwin(height, width, (getmaxy(stdscr)-height)/2, (getmaxx(stdscr)-width)/2);
    if (!win) { dialog_restore_screen(); return CHMOD_CANCEL; }
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    show_shadow(win);
    int saved_mouseinterval=mouseinterval(50);
    int saved_cursor=curs_set(0);

    chmod_button buttons[6];
    int count=0;
    if (multiple) {
        buttons[count++]=(chmod_button){"Set all", CHMOD_SET_ALL, 4, {0}};
        buttons[count++]=(chmod_button){"Marked all", CHMOD_MARKED_ALL, 0, {0}};
        buttons[count++]=(chmod_button){"Set marked", CHMOD_SET_MARKED, 1, {0}};
        buttons[count++]=(chmod_button){"Clear marked", CHMOD_CLEAR_MARKED, 1, {0}};
        chmod_button_pair(buttons, width, 17);
        chmod_button_pair(buttons+2, width, 18);
    }
    buttons[count++]=(chmod_button){"Set", CHMOD_SET, 0, {0}};
    buttons[count++]=(chmod_button){"Cancel", CHMOD_CANCEL, 0, {0}};
    chmod_button_pair(buttons+count-2, width, height-3);

    // Keep the login name held by main() intact: getpwuid() shares static storage.
    char owner[256], group[256], lookup[CMD_MAX];
    struct passwd owner_entry, *owner_info=NULL;
    struct group group_entry, *group_info=NULL;
    if (getpwuid_r(info->st_uid, &owner_entry, lookup, sizeof(lookup), &owner_info) == 0 && owner_info)
        snprintf(owner, sizeof(owner), "%s", owner_info->pw_name);
    else snprintf(owner, sizeof(owner), "%lu", (unsigned long)info->st_uid);
    if (getgrgid_r(info->st_gid, &group_entry, lookup, sizeof(lookup), &group_info) == 0 && group_info)
        snprintf(group, sizeof(group), "%s", group_info->gr_name);
    else snprintf(group, sizeof(group), "%lu", (unsigned long)info->st_gid);

    int focus=0, pressed=-1, action=CHMOD_CANCEL;
    while (1) {
        wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        werase(win);
        draw_framed_box(win, 1, 1, height-2, width-2);
        draw_framed_box(win, 2, 3, 14, 32);
        draw_framed_box(win, 2, 36, 14, width-39);
        if (multiple) draw_dialog_separator(win, 16);
        draw_dialog_separator(win, height-4);
        wattrset(win, A_BOLD | COLOR_PAIR(COLOR_BLUE_ON_WHITE));
        mvwaddstr(win, 1, (width-15)/2, " Chmod command ");
        mvwaddstr(win, 2, 4, " Permission ");
        mvwaddstr(win, 2, 37, " File ");
        for (int i=0; i < 12; i++) {
            wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
            mvwaddch(win, 3+i, 4, *marked & chmod_permissions[i].bit ? '*' : ' ');
            wattrset(win, COLOR_PAIR(i == focus ? COLOR_BLACK_ON_CYAN_BTN : COLOR_BLACK_ON_WHITE));
            mvwprintw(win, 3+i, 5, "[%c] ", *mode & chmod_permissions[i].bit ? 'x' : ' ');
            draw_dialog_hotkey(win, 3+i, 9, chmod_permissions[i].label, chmod_permissions[i].hotkey, i == focus);
        }
        wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        mvwaddstr(win, 3, 38, "Name:");
        draw_text(win, 4, 38, SHORTEN((char *)name, width-42), 0, width-42);
        mvwaddstr(win, 5, 38, "Permissions (octal):");
        mvwprintw(win, 6, 38, "%04o", (unsigned int)(*mode & 07777));
        mvwaddstr(win, 7, 38, "Owner name:");
        draw_text(win, 8, 38, owner, 0, width-42);
        mvwaddstr(win, 9, 38, "Group name:");
        draw_text(win, 10, 38, group, 0, width-42);
        for (int i=0; i < count; i++) {
            wattrset(win, COLOR_PAIR(focus == 12+i ? COLOR_BLACK_ON_CYAN_BTN : COLOR_BLACK_ON_WHITE));
            int is_default=buttons[i].action == CHMOD_SET;
            dialog_button button=buttons[i].bounds;
            mvwprintw(win, button.y, button.x, is_default ? "[< %s >]" : "[ %s ]", buttons[i].label);
            draw_dialog_hotkey(win, button.y, button.x+(is_default ? 3 : 2), buttons[i].label,
                buttons[i].hotkey, focus == 12+i);
        }
        wnoutrefresh(win);
        refresh_screen(0);

        char input_text[MB_LEN_MAX+1];
        MEVENT event;
        int ch=read_text_key(win, input_text, &event);
        if (ch != KEY_MOUSE && ch != ERR) pressed=-1;
        if (ch == 27 || ch == KEY_F(10) || ch == KEY_RESIZE) break;
        if (ch == KEY_UP || ch == KEY_LEFT || ch == KEY_BTAB) focus=(focus+12+count-1)%(12+count);
        if (ch == KEY_DOWN || ch == KEY_RIGHT || ch == '\t') focus=(focus+1)%(12+count);
        if (ch == KEY_HOME) focus=0;
        if (ch == KEY_END) focus=12+count-1;
        if (ch == '\n') { action=focus < 12 ? CHMOD_SET : buttons[focus-12].action; break; }
        // T marks the focused permission; Insert also moves on to the next control.
        if (focus < 12 && (ch == KEY_IC || !strcmp(input_text, "t") || !strcmp(input_text, "T"))) {
            *marked^=chmod_permissions[focus].bit;
            if (ch == KEY_IC) focus=(focus+1)%(12+count);
            continue;
        }
        int activate=!strcmp(input_text, " ");
        if (input_text[0] && !input_text[1] && !activate) {
            int key=tolower((unsigned char)input_text[0]), button=-1;
            for (int i=0; i < count; i++)
                if (key == tolower((unsigned char)buttons[i].label[buttons[i].hotkey])) { button=i; break; }
            if (button >= 0) { action=buttons[button].action; break; }
            for (int i=0; i < 12; i++)
                if (key == chmod_permissions[i].label[chmod_permissions[i].hotkey]) { focus=i; activate=1; break; }
        }
        if (ch == KEY_MOUSE) {
            int x=event.x-getbegx(win), y=event.y-getbegy(win), hit=-1;
            if (y >= 3 && y < 15 && x >= 5 && x < 9+(int)strlen(chmod_permissions[y-3].label)) hit=y-3;
            for (int i=0; i < count; i++) {
                dialog_button button=buttons[i].bounds;
                if (y == button.y && x >= button.x && x < button.x+button.width) hit=12+i;
            }
            int clicked=event.bstate & (BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED);
            if (event.bstate & BUTTON1_PRESSED) {
                pressed=hit;
                if (hit >= 0) focus=hit;
            }
            if (event.bstate & BUTTON1_RELEASED) {
                clicked=hit >= 0 && pressed == hit;
                pressed=-1;
            }
            if (clicked && hit >= 0) { focus=hit; activate=1; }
        }
        if (activate) {
            if (focus < 12) {
                *mode^=chmod_permissions[focus].bit;
                *marked|=chmod_permissions[focus].bit;
            } else { action=buttons[focus-12].action; break; }
        }
    }

    mouseinterval(saved_mouseinterval);
    delwin(win);
    dialog_restore_screen();
    if (saved_cursor != ERR) curs_set(saved_cursor);
    return action;
}

// Keep both paths visible while reusing the standard UTF-8 editing/navigation rules.
int show_symlink_dialog(char *target, char *link_name)
{
    int width=getmaxx(stdscr) < 72 ? getmaxx(stdscr) : 72, height=13;
    if (width < 34 || getmaxy(stdscr) < height) {
        show_errormsg("Symbolic link dialog needs a terminal of at least 34 x 13 cells");
        return 0;
    }
    dialog_save_screen();
    WINDOW *win=newwin(height, width, (getmaxy(stdscr)-height)/2, (getmaxx(stdscr)-width)/2);
    if (!win) { dialog_restore_screen(); return 0; }
    wbkgd(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    keypad(win, TRUE);
    show_shadow(win);
    int saved_mouseinterval=mouseinterval(50), saved_cursor=curs_set(1);

    char *fields[]={target, link_name};
    int positions[]={strlen(target), strlen(link_name)}, offsets[]={0, 0}, modified[]={0, 0};
    int rows[]={4, 7}, field_width=width-8, focus=0, pressed=-1, accepted=0;
    dialog_button buttons[]={{width/2-8, 10, 8}, {width/2+1, 10, 10}};
    while (1) {
        wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        werase(win);
        draw_framed_box(win, 1, 1, height-2, width-2);
        draw_dialog_separator(win, 9);
        wattrset(win, A_BOLD | COLOR_PAIR(COLOR_BLUE_ON_WHITE));
        mvwaddstr(win, 1, (width-15)/2, " Symbolic link ");
        wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
        mvwaddstr(win, 3, 4, "Existing filename (target):");
        mvwaddstr(win, 6, 4, "Symbolic link filename:");
        for (int i=0; i < 2; i++) {
            int column=text_column(fields[i], positions[i], 1);
            positions[i]=text_offset(fields[i], strlen(fields[i]), column, 1);
            if (column-offsets[i] >= field_width) offsets[i]=column-field_width+1;
            if (column < offsets[i]) offsets[i]=column;
            wattrset(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN_PMPT) | (focus == i && !modified[i] ? A_BOLD : 0));
            mvwhline(win, rows[i], 4, ' ', field_width);
            draw_text(win, rows[i], 4, fields[i], offsets[i], field_width);
        }
        for (int i=0; i < 2; i++) {
            wattrset(win, COLOR_PAIR(focus == i+2 ? COLOR_BLACK_ON_CYAN_BTN : COLOR_BLACK_ON_WHITE));
            mvwaddstr(win, buttons[i].y, buttons[i].x, i ? "[ Cancel ]" : "[< OK >]");
            draw_dialog_hotkey(win, buttons[i].y, buttons[i].x+(i ? 2 : 3), i ? "Cancel" : "OK", 0, focus == i+2);
        }
        if (focus < 2) wmove(win, rows[focus], 4+text_column(fields[focus], positions[focus], 1)-offsets[focus]);
        wnoutrefresh(win);
        refresh_screen(focus < 2);

        char input_text[MB_LEN_MAX+1];
        MEVENT event;
        int ch=read_text_key(win, input_text, &event);
        if (ch != KEY_MOUSE && ch != ERR) pressed=-1;
        if (ch == 27 || ch == KEY_F(10) || ch == KEY_RESIZE) break;
        if (ch == '\n') { accepted=focus != 3; break; }
        if (ch == KEY_UP || ch == KEY_BTAB) focus=(focus+3)%4;
        if (ch == KEY_DOWN || ch == '\t') focus=(focus+1)%4;
        if ((ch == KEY_LEFT || ch == KEY_RIGHT) && focus >= 2) focus=5-focus;
        if (focus < 2) {
            char *field=fields[focus];
            int *position=&positions[focus];
            if (ch == KEY_LEFT || ch == KEY_RIGHT || ch == KEY_HOME || ch == KEY_END) {
                if (ch == KEY_LEFT && *position > 0) *position=text_previous(field, *position);
                if (ch == KEY_RIGHT && *position < strlen(field)) *position=text_next(field, strlen(field), *position);
                if (ch == KEY_HOME) *position=0;
                if (ch == KEY_END) *position=strlen(field);
                modified[focus]=1;
            }
            if (ch == KEY_BACKSPACE && *position > 0) {
                int previous=text_previous(field, *position);
                memmove(field+previous, field+*position, strlen(field)-*position+1);
                *position=previous;
                modified[focus]=1;
            }
            if (ch == KEY_DC && *position < strlen(field)) {
                int next=text_next(field, strlen(field), *position);
                memmove(field+*position, field+next, strlen(field)-next+1);
                modified[focus]=1;
            }
            if (input_text[0]) {
                if (!modified[focus]) { field[0]='\0'; *position=0; modified[focus]=1; }
                int length=strlen(field), bytes=strlen(input_text);
                if (length+bytes < CMD_MAX) {
                    memmove(field+*position+bytes, field+*position, length-*position+1);
                    memcpy(field+*position, input_text, bytes);
                    *position+=bytes;
                }
            }
        } else {
            if (!strcmp(input_text, " ")) { accepted=focus == 2; break; }
            if (!strcmp(input_text, "o") || !strcmp(input_text, "O")) { accepted=1; break; }
            if (!strcmp(input_text, "c") || !strcmp(input_text, "C")) break;
        }
        if (ch == KEY_MOUSE) {
            int clicked=event.bstate & (BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED);
            if (!clicked && !(event.bstate & (BUTTON1_PRESSED|BUTTON1_RELEASED))) continue;
            int x=event.x-getbegx(win), y=event.y-getbegy(win), hit=-1;
            for (int i=0; i < 2; i++) {
                if (y == rows[i] && x >= 4 && x < width-4) {
                    focus=i;
                    modified[i]=1;
                    positions[i]=text_offset(fields[i], strlen(fields[i]), offsets[i]+x-4, 1);
                    pressed=-1;
                    hit=i;
                }
                if (y == buttons[i].y && x >= buttons[i].x && x < buttons[i].x+buttons[i].width) hit=i+2;
            }
            if (event.bstate & BUTTON1_PRESSED) {
                pressed=hit;
                if (hit >= 0) focus=hit;
            }
            if (event.bstate & BUTTON1_RELEASED) { clicked=hit >= 2 && hit == pressed; pressed=-1; }
            if (clicked && hit >= 2) { accepted=hit == 2; break; }
        }
    }
    mouseinterval(saved_mouseinterval);
    delwin(win);
    dialog_restore_screen();
    if (saved_cursor != ERR) curs_set(saved_cursor);
    return accepted;
}

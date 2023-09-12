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

#define CMD_MAX 16384

typedef enum {
    SORT_BY_NAME_ASC = 0,
    SORT_BY_SIZE_ASC,
    SORT_BY_TIME_ASC,
    SORT_BY_NAME_DESC,
    SORT_BY_SIZE_DESC,
    SORT_BY_TIME_DESC,
    SORT_BY_NAME_DIRSFIRST_ASC,
    SORT_BY_SIZE_DIRSFIRST_ASC,
    SORT_BY_TIME_DIRSFIRST_ASC,
    SORT_BY_NAME_DIRSFIRST_DESC,
    SORT_BY_SIZE_DIRSFIRST_DESC,
    SORT_BY_TIME_DIRSFIRST_DESC
} SortOrders;

typedef struct FileNode {
    char* name;
    time_t mtime;
    off_t size;
    mode_t chmod;
    uid_t chown;
    int is_dir;
    int is_executable;
    int is_link;
    int is_link_broken; // invalid link
    char* link_target;
    int is_selected; // with Insert key
    struct FileNode *next;
} FileNode;

typedef struct PanelProp {
    int selected_index;
    int scroll_index;
    SortOrders sort_order;
    char path[CMD_MAX];
    int files_count;
    FileNode *files;
} PanelProp;

PanelProp left_panel = {0};
PanelProp right_panel = {0};
PanelProp* active_panel = &left_panel;

// Global windows
WINDOW *win1;
WINDOW *win2;

struct utsname unameData;
struct passwd *pw;
const char *username;

int cursor_pos = 0;
int cmd_offset = 0;
int prompt_length = 0;

char cmd[CMD_MAX] = {0};
int cmd_len = 0;

enum {
    COLOR_WHITE_ON_BLACK = 1,
    COLOR_WHITE_ON_BLUE,
    COLOR_BLACK_ON_CYAN,
    COLOR_YELLOW_ON_BLUE,
    COLOR_GREEN_ON_BLUE,
    COLOR_RED_ON_BLUE
};


int compare_nodes(FileNode *a, FileNode *b, SortOrders sort_order) {
    int result = 0;
    int dirs_first = sort_order >= SORT_BY_NAME_DIRSFIRST_ASC;

    if (dirs_first && (a->is_dir != b->is_dir)) {
        return a->is_dir ? -1 : 1;
    }

    switch (sort_order % 6) {  // 6 because there are 6 basic sort types
        case SORT_BY_NAME_ASC:
        case SORT_BY_NAME_DESC:
            result = strcmp(a->name, b->name);
            break;
        case SORT_BY_SIZE_ASC:
        case SORT_BY_SIZE_DESC:
            result = (a->size > b->size) - (a->size < b->size);
            break;
        case SORT_BY_TIME_ASC:
        case SORT_BY_TIME_DESC:
            result = (a->mtime > b->mtime) - (a->mtime < b->mtime);
            break;
    }

    // DESC sort? revert result
    if (sort_order == SORT_BY_NAME_DESC || sort_order == SORT_BY_SIZE_DESC || sort_order == SORT_BY_TIME_DESC ||
        sort_order == SORT_BY_NAME_DIRSFIRST_DESC || sort_order == SORT_BY_SIZE_DIRSFIRST_DESC || sort_order == SORT_BY_TIME_DIRSFIRST_DESC) {
        result = -result;
    }

    return result;
}


void sort_file_nodes(FileNode **head_ref, SortOrders sort_order) {
    FileNode *sorted = NULL;
    FileNode *current = *head_ref;

    while (current != NULL) {
        FileNode *next = current->next;

        if (sorted == NULL || compare_nodes(current, sorted, sort_order) <= 0) {
            current->next = sorted;
            sorted = current;
        } else {
            FileNode *temp = sorted;
            while (temp->next != NULL && compare_nodes(current, temp->next, sort_order) > 0) {
                temp = temp->next;
            }
            current->next = temp->next;
            temp->next = current;
        }

        current = next;
    }

    *head_ref = sorted;
}


int update_panel_files(PanelProp *panel) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    struct stat link_stat;
    FileNode *head = NULL, *current = NULL;

    panel->files = NULL;
    panel->files_count = 0;

    if ((dir = opendir(panel->path)) == NULL) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0 && strcmp(panel->path, "/") == 0) continue;

        char full_path[CMD_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", panel->path, entry->d_name);
        lstat(full_path, &file_stat);

        FileNode *new_node = (FileNode*) malloc(sizeof(FileNode));

        panel->files_count++;
        new_node->next = NULL;
        new_node->name = strdup(entry->d_name);

        new_node->mtime = file_stat.st_mtime;
        new_node->size = file_stat.st_size;
        new_node->chmod = file_stat.st_mode;
        new_node->chown = file_stat.st_uid;
        new_node->is_dir = S_ISDIR(file_stat.st_mode);
        new_node->is_executable = (file_stat.st_mode & S_IXUSR) || (file_stat.st_mode & S_IXGRP) || (file_stat.st_mode & S_IXOTH);
        new_node->is_selected = false;
        new_node->is_link = S_ISLNK(file_stat.st_mode);
        new_node->is_link_broken = 0;

        if (new_node->is_link) {
            new_node->link_target = NULL;
            char target[CMD_MAX];
            ssize_t len = readlink(full_path, target, sizeof(target) - 1);
            if (len != -1) {
                target[len] = '\0';
                new_node->link_target = strdup(target);
            }

            if (stat(full_path, &link_stat) != 0) {
                new_node->is_link_broken = 1;  // Link is broken
            }
        }

        if (head == NULL) {
            head = new_node;
            current = head;
        } else {
            current->next = new_node;
            current = new_node;
        }
    }

    closedir(dir);
    panel->files = head;
    return panel->files_count;
}

void free_file_nodes(FileNode *head) {
    FileNode *tmp;
    while (head != NULL) {
        free(head->name);
        free(head->link_target);
        tmp = head;
        head = head->next;
        free(tmp);
    }
}



void draw_buttons(int maxY, int maxX) {
    move(maxY - 1, 0);
    clrtoeol();

    char *buttons[] = {"Sort", "View", "Edit", "Copy", "Move", "Mkdir", "Del", "Refresh", "Quit"};
    int num_buttons = sizeof(buttons) / sizeof(char *);

    int total_width = maxX - (num_buttons - 1);  // Subtract (num_buttons - 1) to account for spaces between buttons
    int button_width = (total_width - 1) / num_buttons;  // -1 to account for the extra character in "F10"

    int extra_space = total_width - (button_width * num_buttons) - 1;  // -1 to account for the extra character in "F10"

    int x = 0;
    for (int i = 0; i < num_buttons; ++i) {
        int extra = 0;
        if (extra_space > 0) {
            extra = 1;
            extra_space--;
        }

        attrset(A_NORMAL);
        if (i == num_buttons - 1) {  // Last button (F10)
            mvprintw(maxY - 1, x, "F%d ", i + 2);
        } else {
            mvprintw(maxY - 1, x, "F%d", i + 2);
        }

        attron(COLOR_PAIR(COLOR_BLACK_ON_CYAN));
        mvprintw(maxY - 1, x + 2 + (i == num_buttons - 1), "%-*s", button_width - 2 + extra, buttons[i]);

        x += button_width + extra + 1 + (i == num_buttons - 1);  // +1 spacer between buttons, +1 for the last button (F10)
    }
}

void draw_windows(int maxY, int maxX) {
    // Refresh stdscr to ensure it's updated
    refresh();

    // Calculate window dimensions
    int winHeight = maxY - 2;
    int winWidth1 = maxX / 2;
    int winWidth2 = maxX / 2;

    // Adjust for odd COLS
    if (maxX % 2 != 0) {
        winWidth2 += 1;
    }

    // Delete old windows
    delwin(win1);
    delwin(win2);

    // Create new windows
    win1 = newwin(winHeight, winWidth1, 0, 0);
    win2 = newwin(winHeight, winWidth2, 0, winWidth1);

    // Apply the color pair to the window
    wbkgd(win1, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    wbkgd(win2, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    // Add borders to windows using wborder()
    wborder(win1, '|', '|', '-', '-', '+', '+', '+', '+');
    wborder(win2, '|', '|', '-', '-', '+', '+', '+', '+');

    // Refresh windows to make borders visible
    wrefresh(win1);
    wrefresh(win2);
}

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


void update_panel(WINDOW *win, PanelProp *panel) {
    FileNode *current = panel->files;
    int line = 1;  // Start from the second row to avoid the border
    int width = getmaxx(win) - 2;
    int height = getmaxy(win);
    int visible_items = height - 4;

    // Get the current year
    time_t now = time(NULL);
    struct tm *current_tm = localtime(&now);
    int current_year = current_tm->tm_year;

    // Fill separator columns
    mvwvline(win, 1, width - 12, '|', height -3);
    mvwvline(win, 1, width - 7 - 12 - 1, '|', height -3);
    mvwhline(win, height - 3, 1, '-', width);

    // Header of the file list
    wattron(win,A_BOLD);
    wattron(win, COLOR_PAIR(COLOR_YELLOW_ON_BLUE));
    mvwprintw(win, line, 1, "%*s%s", ((width - 12 - 7 - 2) / 2) - (strlen("Name") / 2), "", "Name");
    mvwprintw(win, line, width - 12 - 7 + 1, "%s", "Size");
    mvwprintw(win, line, width - 7 - 4, "%s", "Modify time");
    wattroff(win,A_BOLD);

    line++;

    // Ignore first items based on the scroll index
    for (int i = 0; i < panel->scroll_index && current != NULL; i++) {
        current = current->next;
    }

    int index = panel->scroll_index;
    while (current != NULL && line < height - 3) {
        int is_selected_item = (index == panel->selected_index && panel == active_panel);
        char prefix = ' ';

        // reset default color
        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
        wattroff(win,A_BOLD);

        if (current->is_link_broken) {
            prefix = '!';
            wattron(win, COLOR_PAIR(COLOR_RED_ON_BLUE));
            wattron(win,A_BOLD);
        } else if (current->is_dir && current->is_link) {
            prefix = '~';
            wattron(win,A_BOLD);
        } else if (current->is_dir) {
            prefix = '/';
            wattron(win,A_BOLD);
        } else if (current->is_link) {
            prefix = '@';
        } else if (current->is_executable) {
            prefix = '*';
            wattron(win, COLOR_PAIR(COLOR_GREEN_ON_BLUE));
            wattron(win,A_BOLD);
        }


        char date_str[13];
        struct tm *tm = localtime(&current->mtime);

        if (tm->tm_year != current_year) {
            strftime(date_str, sizeof(date_str), "%b %d  %Y", tm); // Display year if different
        } else {
            strftime(date_str, sizeof(date_str), "%b %d %H:%M", tm); // Display time if same year
        }

        long long size = current->size;
        char size_str[50];  // Buffer to hold the size and suffix
        snprintf(size_str, sizeof(size_str), "%lld", size);

        if (strlen(size_str) > 7) {
            size /= 1024;
            snprintf(size_str, sizeof(size_str), "%lldK", size);
            if (strlen(size_str) > 7) {
                size /= 1024;
                snprintf(size_str, sizeof(size_str), "%lldM", size);
                if (strlen(size_str) > 7) {
                    size /= 1024;
                    snprintf(size_str, sizeof(size_str), "%lldG", size);
                    if (strlen(size_str) > 7) {
                       size /= 1024;
                       snprintf(size_str, sizeof(size_str), "%lldT", size);
                    }
                }
            }
        }

        if (current->is_dir && strcmp(current->name, "..") == 0) {
           snprintf(size_str, sizeof(size_str), "UP--DIR");
        }

        if (is_selected_item) {
            wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
            wattroff(win, A_BOLD);
            mvwprintw(win, line, width - 7 - 12 - 1, "|");
            mvwprintw(win, line, width - 12, "|");
        }

        int name_width = width - 12 - 7 - 3;

        mvwprintw(win, line, 1, "%c%-*s", prefix, name_width, current->name);
        mvwprintw(win, line, width - 7 - 12, "%7s", size_str);
        mvwprintw(win, line, width - 12 + 1, "%12s", date_str);

        line++;
        index++;
        current = current->next;
    }

    // reset color to default
    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    wattroff(win,A_BOLD);

    wrefresh(win);
    cursor_to_cmd();
}


void init_screen() {
    initscr();
    start_color();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);

    init_pair(COLOR_WHITE_ON_BLACK, COLOR_WHITE, COLOR_BLACK);

    init_pair(COLOR_WHITE_ON_BLUE, COLOR_WHITE, COLOR_BLUE);
    init_pair(COLOR_YELLOW_ON_BLUE, COLOR_YELLOW, COLOR_BLUE);
    init_pair(COLOR_GREEN_ON_BLUE, COLOR_GREEN, COLOR_BLUE);
    init_pair(COLOR_RED_ON_BLUE, COLOR_RED, COLOR_BLUE);

    init_pair(COLOR_BLACK_ON_CYAN, COLOR_BLACK, COLOR_CYAN);
}

void cleanup() {
    delwin(win1);
    delwin(win2);
    endwin();
}

void redraw_ui() {
   // Get screen dimensions
   int maxY, maxX;
   getmaxyx(stdscr, maxY, maxX);

   draw_windows(maxY, maxX);
   draw_buttons(maxY, maxX);
   update_cmd();
   refresh();
}

int main() {

    getcwd(left_panel.path, sizeof(left_panel.path));
    strcpy(right_panel.path,"/root");

    left_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;
    right_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;

    update_panel_files(&left_panel);
    update_panel_files(&right_panel);

    sort_file_nodes(&left_panel.files, left_panel.sort_order);
    sort_file_nodes(&right_panel.files, right_panel.sort_order);

    init_screen();

    MEVENT event;

    uname(&unameData);
    pw = getpwuid(getuid());
    username = pw->pw_name;

    mousemask(ALL_MOUSE_EVENTS, NULL);
    redraw_ui(); // initial screen

    while (1) {
        update_cmd();

        // Print file names in left and right windows
        update_panel(win1, &left_panel);
        update_panel(win2, &right_panel);
        int visible_items = getmaxy(win1) - 5;

        int ch = getch();

        if (ch == KEY_F(10)) {
            break;
        }


        if (ch == 27) {  // Escape character
            getch();  // Discard the '[' character

            int num = 0;
            ch = getch();
            while (ch == '[') {
                ch = getch();
            }
            while (ch >= '0' && ch <= '9') {  // Read numbers
                num = num * 10 + (ch - '0');
                ch = getch();
            }

            if (ch == '~') {
                switch (num) {
                    case 1:
                        ch = KEY_HOME;
                        break;
                    case 2:
                        ch = KEY_IC;
                        break;
                    case 4:
                        ch = KEY_END;
                        break;
                    default:
                        break;
                }
            }
        }

        if (ch == KEY_RESIZE) {  // Handle terminal resize
            endwin();
            init_screen();
            redraw_ui();
        }

        if (ch == KEY_MOUSE) { // handle mouse events
            if (getmouse(&event) == OK) {
                if (event.bstate & BUTTON1_CLICKED) {
                    mvprintw(0, 0, "BUTTON1_PRESSED at (%d, %d)", event.x, event.y);
                }

            }
        }

        if (ch == '\n' && cmd_len > 0) {
            endwin();  // End ncurses mode
            if (strcmp(cmd, "exit") == 0) exit(0);
            printf("%s@%s:%s# %s\n", username, unameData.nodename, active_panel->path, cmd);
            system(cmd);  // Execute the command
            init_screen();
            memset(cmd, 0, CMD_MAX);
            cmd_len = cursor_pos = cmd_offset = prompt_length = 0;
        }

        if (ch == 12) {  // Ctrl+L
            endwin();
            init_screen();
        }

        if (ch == 15) {  // Ctrl+O
            endwin();
            initscr();
            raw();
            getch();
            init_screen();
        }

        if (ch == KEY_BACKSPACE && cursor_pos > 0) {
            memmove(cmd + cursor_pos - 1, cmd + cursor_pos, cmd_len - cursor_pos);
            cmd[--cmd_len] = '\0';
            cursor_pos--;
        }

        if (ch == KEY_DC && cursor_pos < cmd_len) {
            memmove(cmd + cursor_pos, cmd + cursor_pos + 1, cmd_len - cursor_pos - 1);
            cmd[--cmd_len] = '\0';
        }

        if (ch == KEY_LEFT) {
            if (cursor_pos > 0) {
                cursor_pos--;
            } else if (cmd_offset > 0) {
                cmd_offset--;
            }
        }

        if (ch == KEY_RIGHT && cursor_pos < cmd_len) {
            cursor_pos++;
        }

        if (ch == KEY_UP) {
            active_panel->selected_index--;
        }

        if (ch == KEY_DOWN) {
            active_panel->selected_index++;
        }

        if (ch == KEY_PPAGE) {  // Handle PgUp key
            active_panel->selected_index -= visible_items;
            if (active_panel->selected_index < 0) {
                active_panel->selected_index = 0;
            }
        }

        if (ch == KEY_NPAGE) {  // Handle PgDn key
            active_panel->selected_index += visible_items;
            if (active_panel->selected_index > active_panel->files_count - 1) {
                active_panel->selected_index = active_panel->files_count - 1;
            }
        }

        if (ch == KEY_HOME) {  // Handle Home key
            active_panel->selected_index = 0;
        }

        if (ch == KEY_END) {  // Handle End key
            active_panel->selected_index = active_panel->files_count - 1;
        }

        if (ch == KEY_IC) {  // Insert key
            FileNode *current = active_panel->files;
            for (int i = 0; i < active_panel->selected_index && current != NULL; i++) {
                current = current->next;
            }
            if (current) {
                current->is_selected = true;
            }
            active_panel->selected_index++;
        }


        if (ch >= 32 && ch <= 126 && cmd_len < CMD_MAX - 1) {
            memmove(cmd + cursor_pos + 1, cmd + cursor_pos, cmd_len - cursor_pos);
            cmd[cursor_pos] = ch;
            cmd[++cmd_len] = '\0';
            cursor_pos++;
        }

        if (ch == '\t') {
            if (active_panel == &left_panel) {
                active_panel = &right_panel;
            } else {
                active_panel = &left_panel;
            }
        }

        // make sure scroll does not overflow
        if (active_panel->selected_index < 0) {
                active_panel->selected_index = 0;
        }
        if (active_panel->selected_index > active_panel->files_count - 1) {
                active_panel->selected_index = active_panel->files_count - 1;
        }

        // Handle scrolling in files
        if (active_panel->selected_index < active_panel->scroll_index) {
            // Scroll up to place the selected item in the middle or at the top if near the start
            active_panel->scroll_index = active_panel->selected_index - visible_items / 2;
            if (active_panel->scroll_index < 0) {
                active_panel->scroll_index = 0;
            }
        } else if (active_panel->selected_index > active_panel->scroll_index + visible_items - 1) {
            // Scroll down to place the selected item in the middle or at the bottom if near the end
            active_panel->scroll_index = active_panel->selected_index - visible_items / 2;
            if (active_panel->selected_index > active_panel->files_count - visible_items / 2) {
                active_panel->scroll_index = active_panel->files_count - visible_items;
            }
        }

        // Handle scrolling in command line
        int max_cmd_display = COLS - (strlen(username) + strlen(unameData.nodename) + strlen(active_panel->path) + 6) - 1;
        if (cursor_pos - cmd_offset >= max_cmd_display) {
            cmd_offset++;
        } else if (cursor_pos - cmd_offset < 0 && cmd_offset > 0) {
            cmd_offset--;
        }

        if (cmd_offset < 0) {
                cmd_offset = 0;
        }
    }

    cleanup();
    return 0;
}


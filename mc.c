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

PanelProp left_panel = {0};
PanelProp right_panel = {0};
PanelProp* active_panel = &left_panel;

// Global windows
WINDOW *win1;
WINDOW *win2;

struct utsname unameData;
struct passwd *pw;
const char *username;

struct timeval last_click_time = {0};
struct timeval current_time = {0};
struct timeval diff_time = {0};

int cursor_pos = 0;
int cmd_offset = 0;
int prompt_length = 0;

char cmd[CMD_MAX] = {0};
int cmd_len = 0;

int noesc(int ch) {
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
                case 13:
                    ch = KEY_F(3);
                    break;
                default:
                    break;
            }
        }
    }

    return ch;
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

        // get current file under cursor
        FileNode *current = active_panel->files;
        int index = 0;
        while (current != NULL && index < active_panel->selected_index) {
            current = current->next;
            index++;
        }

        memset(active_panel->file_under_cursor, 0, CMD_MAX);
        strncpy(active_panel->file_under_cursor, current->name, strlen(current->name));
        chdir(active_panel->path);

        int ch = noesc(getch());

        if (ch == KEY_F(10)) {
            break;
        }

        if (ch == KEY_F(8)) {
            char title[CMD_MAX] = {};
            char prompt[CMD_MAX] = {};
            sprintf(title, "Delete file or directory\n%s/%s?", active_panel->path, active_panel->file_under_cursor);
            int doit = show_dialog(title, (char *[]) {"Yes", "No", "Maybe", NULL}, prompt);
            redraw_ui();
        }

        if (ch == KEY_F(3)) {
            if (current) {
                if (current->is_dir) {
                    dive_into_directory(current);
                } else {
                    char file[CMD_MAX] = {};
                    sprintf(file, "%s/%s", active_panel->path, active_panel->file_under_cursor);
                    view_file(file);
                    redraw_ui();
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
                if (event.bstate & BUTTON1_PRESSED) {
                    // Determine which window was clicked and set the active panel
                    if (wenclose(win1, event.y, event.x)) {
                        active_panel = &left_panel;
                    } else if (wenclose(win2, event.y, event.x)) {
                        active_panel = &right_panel;
                    }

                    // Select item by mouse click
                    int index = active_panel->scroll_index + event.y - 2;
                    if (index >= 0 && index < active_panel->files_count) {
                        active_panel->selected_index = index;
                    }
                }

                if (event.bstate & BUTTON1_RELEASED || event.bstate & BUTTON1_CLICKED || event.bstate & BUTTON1_DOUBLE_CLICKED) {
                   gettimeofday(&current_time, NULL);
                   timersub(&current_time, &last_click_time, &diff_time);
                   if (diff_time.tv_sec == 0 && diff_time.tv_usec < 300000) {
                       // Double click finished
                       ch = '\n';
                   }
                   last_click_time = current_time;
                }

                // Handle mouse wheel scrolling
                if (event.bstate & BUTTON4_PRESSED) {
                    active_panel->selected_index--;
                } else if (event.bstate & BUTTON5_PRESSED) {
                    active_panel->selected_index++;
                }
            }
        }

        if (ch == '\n')
        {
            if (cmd_len == 0) {
                if (current) {
                   if (current->is_dir) {
                       dive_into_directory(current);
                   } else if (current->is_executable && cmd_len == 0) {
                       snprintf(cmd, CMD_MAX, "%s/%s", active_panel->path, current->name);
                       cmd_len = strlen(cmd);
                   }
                }
            }

            if (cmd_len > 0) {
                if (strcmp(cmd, "exit") == 0) exit(0);

                endwin();  // End ncurses mode
                printf("%s@%s:%s# %s\n", username, unameData.nodename, active_panel->path, cmd);
                system(cmd);  // Execute the command
                init_screen();
                memset(cmd, 0, CMD_MAX);
                cmd_len = cursor_pos = cmd_offset = prompt_length = 0;

                update_panel_files(active_panel);
                sort_file_nodes(&active_panel->files, active_panel->sort_order);
                update_panel_cursor();
            }
        }

        if (ch == 12) {  // Ctrl+L
            endwin();
            init_screen();
            redraw_ui();
        }

        if (ch == 15) {  // Ctrl+O
            endwin();
            initscr();
            raw();
            getch();
            init_screen();
            redraw_ui();
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
            if (current && strcmp(current->name, "..") != 0) {
                current->is_selected = !current->is_selected;
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


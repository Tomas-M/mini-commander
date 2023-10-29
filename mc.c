#include <ncurses.h>
#include <getopt.h>
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

int color_enabled = 1;

int noesc(int ch) {
    int num = 0;
    if (ch == 27) {  // Escape character
        ch = getch();
        while (ch == '[') {  // Discard the '[' character
            ch = getch();
        }

        if (ch == 10) return KEY_ALT_ENTER;
        if (ch == 'a') return KEY_ALT_a;
        if (ch == 's') return KEY_ALT_s;

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
                case 12:
                    ch = KEY_F(2);
                    break;
                case 13:
                    ch = KEY_F(3);
                    break;
                case 14:
                    ch = KEY_F(4);
                    break;
                default:
                    break;
            }
        }
    }

    return ch;
}


int main(int argc, char *argv[]) {

    // Define the long options
    static struct option long_options[] = {
        {"nocolor", no_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    // parse commandline arguments
    while ((opt = getopt_long(argc, argv, "b", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                color_enabled = 0;
                break;
            case 'h':
                fprintf(stderr, "Usage: %s [-b|--nocolor] [-h|--help]\n", argv[0]);
                return 1;
                break;
        }
    }


    getcwd(left_panel.path, sizeof(left_panel.path));
    strcpy(right_panel.path,"/root");

    left_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;
    right_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;

    update_files_in_both_panels();

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

        if (ch == 0) { // Ctrl+Space
            // TODO: fix when files are selected
            // TODO: fix when cursor is at ..
            operationContext stats = {0};
            panel_mass_action(countstats_operation, "", &stats);
            if (stats.abort != 1) {
                current->size = stats.total_size;
            }
        }

        if (ch == KEY_F(2)) { // F2
            int sort = show_dialog("Sort files and directories by:", (char *[]) {
            "Sort by name, from a to z, mix dirs",
            "Sort by size, from small to big, mix dirs",
            "Sort by modify time, from old to new, mix dirs",
            "Sort by name, from z to a, mix dirs",
            "Sort by size, from big to small, mix dirs",
            "Sort by modify time, from new to old, mix dirs",
            "Sort by name, from a to z, dirs first",
            "Sort by size, from small to big, dirs first",
            "Sort by modify time, from old to new, dirs first",
            "Sort by name, from z to a, dirs first",
            "Sort by size, from big to small, dirs first",
            "Sort by modify time, from new to old, dirs first", NULL}, active_panel->sort_order, NULL, 0, 1);
            if (sort != -1) active_panel->sort_order = sort - 1;
            update_files_in_both_panels();
        }

        if (ch == KEY_F(3)) { // F3
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


        if (ch == KEY_F(4)) { // F4
            if (current) {
                if (current->is_dir) {
                    dive_into_directory(current);
                } else {
                    char file[CMD_MAX] = {};
                    sprintf(file, "%s/%s", active_panel->path, active_panel->file_under_cursor);
                    edit_file(file);
                    redraw_ui();
                    update_files_in_both_panels();
               }
           }
        }

        if (ch == KEY_F(5)) { // F5
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            sprintf(prompt, active_panel == &left_panel ? right_panel.path : left_panel.path);
            sprintf(title, "Copy %d file%s/director%s to:", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0);
            if (btn == 1) {
                operationContext stats = {0};
                operationContext context = {0};
                panel_mass_action(countstats_operation, "", &stats);
                if (stats.abort != 1) {
                    context.total_items = stats.total_items;
                    context.total_size =  stats.total_size;
                    panel_mass_action(copy_operation, prompt, &context);
                }
            }
            update_files_in_both_panels();
        }

        if (ch == KEY_F(6)) { // F6
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            sprintf(prompt, active_panel == &left_panel ? right_panel.path : left_panel.path);
            sprintf(title, "Move %d file%s/director%s to:", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0);
            if (btn == 1) {
                operationContext stats = {0};
                operationContext context = {0};
                panel_mass_action(countstats_operation, "", &stats);
                if (stats.abort != 1) {
                    context.total_items = stats.total_items;
                    context.total_size =  stats.total_size;
                    panel_mass_action(move_operation, prompt, &context);
                }
            }
            update_files_in_both_panels();
        }

        if (ch == KEY_F(7)) { // F7
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            if (strcmp(active_panel->file_under_cursor, "..") != 0) {
                sprintf(prompt, active_panel->file_under_cursor);
            }
            sprintf(title, "Enter directory name to create:");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0);
            if (btn == 1 && strlen(prompt) > 0) {
                int err = mkdir_recursive(prompt, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
                if (!err) {
                    // Check if prompt is relative (doesn't start with '/')
                    if (prompt[0] != '/') {
                        sprintf(active_panel->file_under_cursor, "%s", prompt);
                    }
                    // Check if prompt starts with active_panel->path
                    else if (strncmp(prompt, active_panel->path, strlen(active_panel->path)) == 0) {
                        // Fill only the remaining part
                        sprintf(active_panel->file_under_cursor, "%s", prompt + strlen(active_panel->path) + 1);
                    }

                    char *slash_position = strchr(active_panel->file_under_cursor, '/');
                    if (slash_position) {
                        *slash_position = '\0';
                    }
                } else {
                    show_errormsg(SPRINTF("Operation failed\n%s (%d)", strerror(err), err));
                }
                update_files_in_both_panels();
            }
        }

        if (ch == KEY_F(8)) {
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }

            char title[CMD_MAX] = {};
            sprintf(title, "Delete %d file%s/director%s?", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"Yes", "No", NULL}, 0, NULL, 1, 0);

            if (btn == 1) {
                operationContext stats = {0};
                operationContext context = {0};
                panel_mass_action(countstats_operation, "", &stats);
                if (stats.abort != 1) {
                    context.total_items = stats.total_items;
                    context.total_size =  stats.total_size;
                    panel_mass_action(delete_operation, "", &context);
                }
            }
            update_files_in_both_panels();
            redraw_ui();
        }

        if (ch == KEY_F(10)) {
            break;
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


        if (ch == KEY_ALT_ENTER) { // Check for Enter key after Alt
            char *filename = active_panel->file_under_cursor;
            int filename_len = strlen(filename);

            // Check if there's enough space for the filename and the space character
            if (cmd_len + filename_len + 1 < CMD_MAX) {
                // Move the existing command to make space for the filename and space
                memmove(cmd + cursor_pos + filename_len + 1, cmd + cursor_pos, cmd_len - cursor_pos);

                // Copy the filename to the command buffer
                memcpy(cmd + cursor_pos, filename, filename_len);

                // Add a space after the filename
                cmd[cursor_pos + filename_len] = ' ';

                // Update the command length and cursor position
                cmd_len += filename_len + 1;
                cursor_pos += filename_len + 1;
            }
        }


        if (ch == KEY_ALT_a) {
            char *path = active_panel->path;
            int path_len = strlen(path);

            // Check if there's enough space for the path and the / character
            if (cmd_len + path_len + 1 < CMD_MAX) {
                // Move the existing command to make space for the path and /
                memmove(cmd + cursor_pos + path_len + 1, cmd + cursor_pos, cmd_len - cursor_pos);

                // Copy the path to the command buffer
                memcpy(cmd + cursor_pos, path, path_len);

                // Add a / after the path
                cmd[cursor_pos + path_len] = '/';

                // Update the command length and cursor position
                cmd_len += path_len + 1;
                cursor_pos += path_len + 1;
            }
        }


        int continue_search_mode = 0;
        int search_skip_current = 0;

        if (ch == KEY_ALT_s) {
            if (active_panel->search_mode == 1) { // already searching
                memcpy(active_panel->search_text, active_panel->prev_search_text, sizeof(active_panel->search_text));
                search_skip_current = 1;
            } else { // new search
                memset(active_panel->search_text, 0, sizeof(active_panel->search_text));
                active_panel->search_mode = 1;
            }
            continue_search_mode = 1;
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
                update_files_in_both_panels();
            }
        }

        if (ch == 12) {  // Ctrl+L
            endwin();
            init_screen();
            redraw_ui();
        }

        if (ch == 15) {  // Ctrl+O
            endwin();
            initialize_ncurses();
            raw();
            getch();
            init_screen();
            redraw_ui();
        }


        if (ch == 18 || ch == KEY_F(9)) { // Ctrl+R
            update_files_in_both_panels();
        }


        if (ch == KEY_BACKSPACE) {
            if (active_panel->search_mode == 1) {
                continue_search_mode = 1;
                if (strlen(active_panel->search_text) > 0) {
                    active_panel->search_text[strlen(active_panel->search_text) - 1] = '\0';
                    memcpy(active_panel->prev_search_text, active_panel->search_text, sizeof(active_panel->search_text));
                }
            } else if (cursor_pos > 0) {
                memmove(cmd + cursor_pos - 1, cmd + cursor_pos, cmd_len - cursor_pos);
                cmd[--cmd_len] = '\0';
                cursor_pos--;
            }
        }


        if (ch == KEY_DC) {
            if (cursor_pos < cmd_len) {
                memmove(cmd + cursor_pos, cmd + cursor_pos + 1, cmd_len - cursor_pos - 1);
                cmd[--cmd_len] = '\0';
            }
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
            if (current && strcmp(current->name, "..") != 0) {
                current->is_selected = !current->is_selected;
                if (!current->is_dir) active_panel->bytes_selected_files += current->is_selected ? current->size : -1 * current->size;
                active_panel->num_selected_files += current->is_selected ? 1 : -1;
            }
            active_panel->selected_index++;
        }


        if (isprint(ch)) {
            if (active_panel->search_mode == 1) {
                continue_search_mode = 1;
                if (strlen(active_panel->search_text) < CMD_MAX - 1) {
                    active_panel->search_text[strlen(active_panel->search_text) + 1] = 0;
                    active_panel->search_text[strlen(active_panel->search_text)] = ch;
                }
                memcpy(active_panel->prev_search_text, active_panel->search_text, sizeof(active_panel->search_text));
            } else if (cmd_len < CMD_MAX - 1) {
                memmove(cmd + cursor_pos + 1, cmd + cursor_pos, cmd_len - cursor_pos);
                cmd[cursor_pos] = ch;
                cmd[++cmd_len] = '\0';
                cursor_pos++;
            }
        }

        if (ch == '\t') {
            if (active_panel == &left_panel) {
                active_panel = &right_panel;
            } else {
                active_panel = &left_panel;
            }
        }


        if (!continue_search_mode) {
            active_panel->search_mode = 0;
        }


        if (active_panel->search_mode) {
            FileNode *files = active_panel->files;
            int index = 0;
            int found = -1;

            // search from beginning while we get to current item anyway
            while (files != NULL && index < active_panel->selected_index) {
                if (strncmp(active_panel->search_text, files->name, strlen(active_panel->search_text)) == 0) {
                    if (found == -1) found = index;
                }
                index++;
                files = files->next;
            }

            if (search_skip_current) { index++; files = files->next; }

            while (files != NULL) {
                if (strncmp(active_panel->search_text, files->name, strlen(active_panel->search_text)) == 0) {
                    found = index;
                    break;
                }
                index++;
                files = files->next;
            }

            if (found >= 0) active_panel->selected_index = found;
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


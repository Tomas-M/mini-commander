#include "includes.h"
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
int cmd_offset = 0; // Horizontal scroll in terminal columns; cursor_pos stays a byte offset.
int prompt_length = 0;

char cmd[CMD_MAX] = {0};
int cmd_len = 0;

int color_enabled = 1;

// Normalize alternate Home and End key codes reported by some terminals.
int noesc(int ch) {
    if (ch == 362) return KEY_HOME;
    if (ch == 385) return KEY_END;
    return ch;
}


int main(int argc, char *argv[]) {

    // Define the long options
    static struct option long_options[] = {
        {"nocolor", no_argument, 0, 'b'},
        {"version", no_argument, 0, 'v'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    // parse commandline arguments
    while ((opt = getopt_long(argc, argv, "bhv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                color_enabled = 0;
                break;
            case 'h':
                fprintf(stderr, "Mini Commander (c) 2023 Tomas Matejicek + ChatGPT\n", argv[0]);
                fprintf(stderr, "Usage: %s [-b|--nocolor] [-h|--help]\n", argv[0]);
                return 1;
                break;
            case 'v':
                fprintf(stderr, "Version 2.0\n", argv[0]);
                return 1;
                break;
        }
    }


    getcwd(left_panel.path, sizeof(left_panel.path));
    strcpy(right_panel.path, left_panel.path);

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
        // Present both panels together, with the cursor on the command line.
        wnoutrefresh(stdscr);
        refresh_screen(1);
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

        char input_text[MB_LEN_MAX+1];
        int ch=read_text_key(stdscr, input_text);

        if (ch == 0 && !input_text[0]) { // Ctrl+Space, not printable Unicode input.
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
            "Sort by modify time, from new to old, dirs first", NULL}, active_panel->sort_order, NULL, 0, 1, 0);
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

        if (ch == KEY_F(5) || ch == KEY_SHIFT_F5) { // Copy
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            snprintf(prompt, sizeof(prompt), "%s", active_panel == &left_panel ? right_panel.path : left_panel.path);
            if (ch == KEY_SHIFT_F5) snprintf(prompt, sizeof(prompt), "%s", active_panel->file_under_cursor);
            sprintf(title, "Copy %d file%s/director%s to:", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0, ch == KEY_SHIFT_F5);
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

        if (ch == KEY_F(6) || ch == KEY_SHIFT_F6) { // Move / rename
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            snprintf(prompt, sizeof(prompt), "%s", active_panel == &left_panel ? right_panel.path : left_panel.path);
            if (ch == KEY_SHIFT_F6) snprintf(prompt, sizeof(prompt), "%s", active_panel->file_under_cursor);
            sprintf(title, "Move %d file%s/director%s to:", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0, ch == KEY_SHIFT_F6);
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

        if (ch == KEY_F(7) || ch == KEY_SHIFT_F7) { // Create directory
            char title[CMD_MAX] = {0};
            char prompt[CMD_MAX] = {0};
            if (strcmp(active_panel->file_under_cursor, "..") != 0) {
                snprintf(prompt, sizeof(prompt), "%s", active_panel->file_under_cursor);
            }
            sprintf(title, "Enter directory name to create:");
            int btn = show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0, ch == KEY_SHIFT_F7);
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
            int btn = show_dialog(title, (char *[]) {"Yes", "No", NULL}, 0, NULL, 1, 0, 0);

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
                }
                // Older ncurses mouse interfaces have no fifth-button event.
#ifdef BUTTON5_PRESSED
                else if (event.bstate & BUTTON5_PRESSED) {
                    active_panel->selected_index++;
                }
#endif
            }
        }


        // Insert complete filename/path bytes and keep the trailing command terminated.
        if (ch == KEY_ALT_ENTER || ch == KEY_ALT_a)
        {
            const char *text=ch == KEY_ALT_ENTER ? active_panel->file_under_cursor : active_panel->path;
            int length=strlen(text);
            if (cmd_len+length+1 < CMD_MAX)
            {
                memmove(cmd+cursor_pos+length+1, cmd+cursor_pos, cmd_len-cursor_pos+1);
                memcpy(cmd+cursor_pos, text, length);
                cmd[cursor_pos+length]=ch == KEY_ALT_ENTER ? ' ' : '/';
                cmd_len+=length+1;
                cursor_pos+=length+1;
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
            int run_selected_file=0;
            if (cmd_len == 0) {
                if (current) {
                   if (current->is_dir) {
                       dive_into_directory(current);
                   } else if (current->is_executable && cmd_len == 0) {
                       int length=snprintf(cmd, sizeof(cmd), "%s/%s", active_panel->path, current->name);
                       if (length < 0 || length >= sizeof(cmd))
                       {
                           cmd[0]='\0';
                           show_errormsg("Executable path is too long");
                           continue;
                       }
                       cmd_len=length;
                       run_selected_file=1;
                   }
                }
            }

            if (cmd_len > 0) {
                if (strcmp(cmd, "exit") == 0) exit(0);

                endwin();  // End ncurses mode
                printf("%s@%s:%s# %s\n", username, unameData.nodename, active_panel->path, cmd);
                if (run_selected_file)
                {
                    if (execute_file(cmd) < 0) perror(cmd);
                }
                else system(cmd); // Explicitly typed commands retain shell syntax.
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
            wint_t key;
            get_wch(&key);
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
                    active_panel->search_text[text_previous(active_panel->search_text, strlen(active_panel->search_text))]='\0';
                    memcpy(active_panel->prev_search_text, active_panel->search_text, sizeof(active_panel->search_text));
                }
            } else if (cursor_pos > 0) {
                int previous=text_previous(cmd, cursor_pos);
                memmove(cmd+previous, cmd+cursor_pos, cmd_len-cursor_pos+1);
                cmd_len-=cursor_pos-previous;
                cursor_pos=previous;
            }
        }


        if (ch == KEY_DC) {
            if (cursor_pos < cmd_len) {
                wchar_t chars[CCHARW_MAX];
                int width, bytes=text_cell(cmd+cursor_pos, cmd_len-cursor_pos, chars, &width);
                memmove(cmd+cursor_pos, cmd+cursor_pos+bytes, cmd_len-cursor_pos-bytes+1);
                cmd_len-=bytes;
            }
        }


        if (ch == KEY_LEFT && cursor_pos > 0) cursor_pos=text_previous(cmd, cursor_pos);

        if (ch == KEY_RIGHT && cursor_pos < cmd_len) {
            wchar_t chars[CCHARW_MAX];
            int width;
            cursor_pos+=text_cell(cmd+cursor_pos, cmd_len-cursor_pos, chars, &width);
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


        if (input_text[0]) {
            int bytes=strlen(input_text);
            if (active_panel->search_mode == 1) {
                continue_search_mode = 1;
                int length=strlen(active_panel->search_text);
                if (length+bytes < CMD_MAX) memcpy(active_panel->search_text+length, input_text, bytes+1);
                memcpy(active_panel->prev_search_text, active_panel->search_text, sizeof(active_panel->search_text));
            } else if (cmd_len+bytes < CMD_MAX) {
                memmove(cmd+cursor_pos+bytes, cmd+cursor_pos, cmd_len-cursor_pos+1);
                memcpy(cmd+cursor_pos, input_text, bytes);
                cmd_len+=bytes;
                cursor_pos+=bytes;
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

    }

    cleanup();
    return 0;
}


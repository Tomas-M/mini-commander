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
                fprintf(stderr, "Mini Commander (c) 2023 Tomas Matejicek + ChatGPT\n");
                fprintf(stderr, "Usage: %s [-b|--nocolor] [left-directory [right-directory]]\n", argv[0]);
                return 0;
                break;
            case 'v':
                fprintf(stderr, "Version 2.0\n");
                return 1;
                break;
            default:
                return 1;
        }
    }


    if (!getcwd(left_panel.path, sizeof(left_panel.path))) { perror("getcwd"); return 1; }
    strcpy(right_panel.path, left_panel.path);

    left_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;
    right_panel.sort_order = SORT_BY_NAME_DIRSFIRST_ASC;

    if (argc-optind > 2) { fprintf(stderr, "Expected at most two directory paths\n"); return 1; }
    PanelProp *panels[]={&left_panel, &right_panel};
    for (int i=0; optind+i < argc; i++)
        if (change_panel_directory(panels[i], argv[optind+i]) != 0)
        { perror(argv[optind+i]); return 1; }
    update_files_in_both_panels();

    init_screen();

    MEVENT event;
    PanelMouse mouse={0};
    int pressed_button=0;

    uname(&unameData);
    pw = getpwuid(getuid());
    username = pw->pw_name;

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

        char input_text[MB_LEN_MAX+1];
        int ch=read_text_key(stdscr, input_text, &event);
        if (ch == KEY_MOUSE)
        {
            ch=button_key(&event, panel_buttons, &pressed_button);
            if (mouse.button || ch == KEY_MOUSE) ch=panel_mouse(&event, &mouse);
        }
        if (ch != KEY_MOUSE && ch != ERR) { pressed_button=0; mouse=(PanelMouse){0}; }

        // get current file under cursor
        FileNode *current = active_panel->files;
        int index = 0;
        while (current != NULL && index < active_panel->selected_index) {
            current = current->next;
            index++;
        }

        memset(active_panel->file_under_cursor, 0, CMD_MAX);
        if (current) snprintf(active_panel->file_under_cursor, CMD_MAX, "%s", current->name);
        chdir(active_panel->path);

        if (ch == 21) { swap_panels(); continue; } // Ctrl+U
        if (ch == KEY_ALT_g)
        {
            char path[CMD_MAX]="";
            if (show_dialog("Change directory:", (char *[]) {"Go", "Cancel", NULL}, 0, path, 0, 0, 1) == 1 && path[0])
                if (change_panel_directory(active_panel, path) != 0)
                    show_errormsg(SPRINTF("Cannot open directory:\n%s\n%s", path, strerror(errno)));
            continue;
        }
        if (!cmd_len && !active_panel->search_mode && input_text[0] && !input_text[1] && strchr("+-*", input_text[0]))
        {
            int action=input_text[0] == '*' ? -1 : input_text[0] == '+';
            char pattern[CMD_MAX]="*";
            if (action < 0 || show_dialog(action ? "Select files matching (* and ?):" : "Unselect files matching (* and ?):",
                (char *[]) {"OK", "Cancel", NULL}, 0, pattern, 0, 0, 1) == 1)
                select_pattern(active_panel, pattern, action);
            continue;
        }

        if (ch == 0 && !input_text[0] && current) { // Ctrl+Space, not printable Unicode input.
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

        if ((ch == KEY_F(3) || ch == KEY_F(4)) && current) {
            if (current->is_dir) dive_into_directory(current);
            else {
                view_edit_file(SPRINTF("%s/%s", active_panel->path, active_panel->file_under_cursor), ch == KEY_F(4));
                redraw_ui();
                if (ch == KEY_F(4)) update_files_in_both_panels();
            }
        }

        int copying=ch == KEY_F(5) || ch == KEY_SHIFT_F5;
        int moving=ch == KEY_F(6) || ch == KEY_SHIFT_F6;
        if ((copying || moving) && (current || active_panel->num_selected_files)) {
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }
            int edit_name=ch == KEY_SHIFT_F5 || ch == KEY_SHIFT_F6;
            int count=active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1;
            char title[CMD_MAX], prompt[CMD_MAX];
            const char *target=active_panel == &left_panel ? right_panel.path : left_panel.path;
            snprintf(prompt, sizeof(prompt), "%s", edit_name ? active_panel->file_under_cursor : target);
            snprintf(title, sizeof(title), "%s %d file%s/director%s to:", copying ? "Copy" : "Move", count, count > 1 ? "s" : "", count > 1 ? "ies" : "y");
            if (show_dialog(title, (char *[]) {"OK", "Cancel", NULL}, 0, prompt, 0, 0, edit_name) == 1)
                run_file_operation(copying ? copy_operation : move_operation, prompt);
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
                char created[CMD_MAX];
                strcpy(created, active_panel->file_under_cursor);
                update_files_in_both_panels();
                if (!err) { strcpy(active_panel->file_under_cursor, created); restore_panel_position(active_panel); }
            }
        }

        if (ch == KEY_F(8) && (current || active_panel->num_selected_files)) {
            if (active_panel->num_selected_files == 0 && strcmp(active_panel->file_under_cursor, "..") == 0) {
                show_errormsg("Cannot operate on \"..\"");
                continue;
            }

            char title[CMD_MAX] = {};
            sprintf(title, "Delete %d file%s/director%s?", active_panel->num_selected_files > 0 ? active_panel->num_selected_files : 1, active_panel->num_selected_files > 1 ? "s" : "", active_panel->num_selected_files > 1 ? "ies" : "y");
            int btn = show_dialog(title, (char *[]) {"Yes", "No", NULL}, 0, NULL, 1, 0, 0);

            if (btn == 1) run_file_operation(delete_operation, "");
            update_files_in_both_panels();
            redraw_ui();
        }

        if (ch == KEY_F(10)) {
            break;
        }


        if (ch == KEY_RESIZE || ch == 12) {  // Resize or Ctrl+L
            endwin();
            init_screen();
            redraw_ui();
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
                if (strcmp(cmd, "exit") == 0) break;

                if (!run_selected_file && command_cd(cmd))
                {
                    cmd[0]=0;
                    cmd_len=cursor_pos=cmd_offset=0;
                    continue;
                }

                mouse_tracking(0);
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

        if (ch == 15) {  // Ctrl+O
            mouse_tracking(0);
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
                int bytes=text_next(cmd, cmd_len, cursor_pos)-cursor_pos;
                memmove(cmd+cursor_pos, cmd+cursor_pos+bytes, cmd_len-cursor_pos-bytes+1);
                cmd_len-=bytes;
            }
        }


        if (ch == KEY_LEFT && cursor_pos > 0) cursor_pos=text_previous(cmd, cursor_pos);

        if (ch == KEY_RIGHT && cursor_pos < cmd_len) cursor_pos=text_next(cmd, cmd_len, cursor_pos);

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
            if (current) select_file(active_panel, current, !current->is_selected);
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
            int found=-1, index=0, length=strlen(active_panel->search_text);
            for (FileNode *file=active_panel->files; file; file=file->next, index++) {
                if (search_skip_current && index == active_panel->selected_index) continue;
                if (strncmp(active_panel->search_text, file->name, length) != 0) continue;
                // Keep the first match as a wraparound fallback until we reach the cursor.
                if (found < 0) found=index;
                if (index >= active_panel->selected_index) { found=index; break; }
            }
            if (found >= 0) active_panel->selected_index=found;
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


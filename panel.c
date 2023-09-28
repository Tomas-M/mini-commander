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

void shorten(char *name, int width, char *result) {
    int length = strlen(name);
    if (length <= width) {
        strcpy(result, name);
    } else {
        int halfWidth = (width - 1) / 2;
        strncpy(result, name, halfWidth);
        result[halfWidth] = '~';
        strncpy(result + halfWidth + 1, name + length - (width - 1 - halfWidth), width - 1 - halfWidth);
        result[width] = '\0';
    }
}

int file_has_extension(const char *filename, const char *extensions[]) {
    for (int i = 0; extensions[i]; i++) {
        if (strcmp(filename + strlen(filename) - strlen(extensions[i]), extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}


void update_panel(WINDOW *win, PanelProp *panel) {
    FileNode *current = panel->files;
    int line = 1;  // Start from the second row to avoid the border
    int width = getmaxx(win) - 2;
    int height = getmaxy(win);
    int visible_items = height - 4;
    int name_width = width - 12 - 7 - 3;
    char info[CMD_MAX];

    // Get the current year
    time_t now = time(NULL);
    struct tm *current_tm = localtime(&now);
    int current_year = current_tm->tm_year;

    // Fill separator columns
    mvwvline(win, 1, width - 12, '|', height -3);
    mvwvline(win, 1, width - 7 - 12 - 1, '|', height -3);
    mvwhline(win, height - 3, 1, '-', width);

    // Header of the file list
    wattron(win, A_BOLD);
    wattron(win, COLOR_PAIR(COLOR_YELLOW_ON_BLUE));
    mvwprintw(win, line, 1, "%*s%s", ((width - 12 - 7 - 2) / 2) - (strlen("Name") / 2), "", "Name");
    mvwprintw(win, line, width - 12 - 7 + 1, "%s", "Size");
    mvwprintw(win, line, width - 7 - 4, "%s", "Modify time");
    wattroff(win, A_BOLD);

    line++;

    // Ignore first items based on the scroll index
    for (int i = 0; i < panel->scroll_index && current != NULL; i++) {
        current = current->next;
    }

    FileNode * active_item = NULL;
    int index = panel->scroll_index;
    while (current != NULL && line < height - 3) {
        int is_active_item = (index == panel->selected_index);
        char prefix = ' ';

        // reset default color
        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
        wattroff(win, A_BOLD);

        // use some colors for regular files based by extension
        if (!current->is_dir)
        {
           if (file_has_extension(current->name, (const char*[]){".gz",".tar",".xz",NULL})) {
              wattron(win, COLOR_PAIR(COLOR_MAGENTA_ON_BLUE));
              wattron(win, A_BOLD);
           }

           if (file_has_extension(current->name, (const char*[]){".c",".php",".sh",".h",NULL})) {
              wattron(win, COLOR_PAIR(COLOR_CYAN_ON_BLUE));
              wattron(win, A_BOLD);
           }
        }


        if (current->is_link_broken) {
            prefix = '!';
            wattron(win, COLOR_PAIR(COLOR_RED_ON_BLUE));
            wattron(win, A_BOLD);
        } else if (current->is_link_to_dir) {
            prefix = '~';
            wattron(win, A_BOLD);
        } else if (current->is_dir) {
            prefix = '/';
            wattron(win, A_BOLD);
        } else if (current->is_link) {
            prefix = '@';
        } else if (current->is_executable) {
            prefix = '*';
            wattron(win, COLOR_PAIR(COLOR_GREEN_ON_BLUE));
            wattron(win, A_BOLD);
        }

        if (current->is_selected) {
            wattron(win, COLOR_PAIR(COLOR_YELLOW_ON_BLUE));
            wattron(win, A_BOLD);
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

        int updir = (current->is_dir && strcmp(current->name, "..") == 0);
        if (updir) {
           snprintf(size_str, sizeof(size_str), "UP--DIR");
        }

        if (is_active_item) {
            if (updir) {
                snprintf(info, sizeof(info), "UP--DIR");
            } else if (current->is_link) {
                snprintf(info, sizeof(info), "-> %s", current->link_target);
            } else {
                snprintf(info, sizeof(info), "%c%s", prefix, current->name);
            }

            if (panel == active_panel) {
                wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
                wattroff(win, A_BOLD);

                mvwprintw(win, line, width - 7 - 12 - 1, "|");
                mvwprintw(win, line, width - 12, "|");

                if (current->is_selected) {
                   wattron(win, COLOR_PAIR(COLOR_YELLOW_ON_CYAN));
                   wattron(win, A_BOLD);
                }
           }
        }

        mvwhline(win, line, 1, ' ', name_width + 1);
        mvwprintw(win, line, 1, "%c", prefix);

        mvwaddnstr(win, line, 2, SHORTEN(current->name, name_width), name_width);

        mvwprintw(win, line, width - 7 - 12, "%7s", size_str);
        mvwprintw(win, line, width - 12 + 1, "%12s", date_str);

        line++;
        index++;
        current = current->next;
    }

    // path goes to window title
    if ((win == win1 && active_panel == &left_panel) || (win == win2 && active_panel == &right_panel)) {
       wattron(win, COLOR_PAIR(COLOR_BLACK_ON_WHITE));
    } else {
       wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    }
    wattroff(win, A_BOLD);
    mvwprintw(win, 0, 3, " %s ", SHORTEN(panel->path, name_width + 12 + 7 - 2));

    // reset color to default
    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    mvwhline(win, 0, strlen(panel->path) + 5, '-', width - strlen(panel->path) - 4);

    while(line < height - 3) {
        mvwhline(win, line, 1, ' ', name_width + 1);
        mvwprintw(win, line, width - 7 - 12, "       ");
        mvwprintw(win, line, width - 12 + 1, "            ");
        line++;
    }

    // print info for active file
    mvwprintw(win, height - 2, 1, "%-*s", width, SHORTEN(info,width));

    wrefresh(win);
    cursor_to_cmd();
}


void update_panel_cursor() {
   if (strlen(active_panel->file_under_cursor) >0) {
       // Search for the last selected item and set it as the active item
       FileNode *node = active_panel->files;
       int index = 0;
       while (node) {
           if (strcmp(node->name, active_panel->file_under_cursor) == 0) {
               active_panel->selected_index = index;
               break;
           }
           node = node->next;
           index++;
       }
   } else {
       active_panel->selected_index = 0;
   }
   active_panel->scroll_index = 0;
}

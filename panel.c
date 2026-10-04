#include "includes.h"
#include "types.h"
#include "globals.h"

// Keep Insert and mouse marking consistent, including the panel's selection totals.
void select_file(PanelProp *panel, FileNode *file, int selected)
{
    if (!strcmp(file->name, "..") || file->is_selected == selected) return;
    file->is_selected=selected;
    panel->num_selected_files+=selected ? 1 : -1;
    if (!file->is_dir) panel->bytes_selected_files+=selected ? file->size : -file->size;
}

// A drag stays in its starting panel; right drags paint a fixed selection state.
int panel_mouse(MEVENT *event, PanelMouse *mouse)
{
    WINDOW *win=NULL;
    PanelProp *panel=NULL;
    if (wenclose(win1, event->y, event->x)) { win=win1; panel=&left_panel; }
    if (wenclose(win2, event->y, event->x)) { win=win2; panel=&right_panel; }
    int index=-1;
    if (win)
    {
        int y=event->y-getbegy(win), x=event->x-getbegx(win);
        if (x > 0 && x < getmaxx(win)-1 && y >= 2 && y < getmaxy(win)-3)
            index=panel->scroll_index+y-2;
        if (index >= panel->files_count) index=-1;
    }
    int left=event->bstate & (BUTTON1_PRESSED|BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED);
    int right=event->bstate & (BUTTON3_PRESSED|BUTTON3_CLICKED|BUTTON3_DOUBLE_CLICKED|BUTTON3_TRIPLE_CLICKED);
    int released=event->bstate & (BUTTON1_RELEASED|BUTTON3_RELEASED);
    int clicked=event->bstate & (BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED|BUTTON3_CLICKED|BUTTON3_DOUBLE_CLICKED|BUTTON3_TRIPLE_CLICKED);
    int motion=event->bstate & REPORT_MOUSE_POSITION;
    if (!mouse->button && !motion && (left || right) && index >= 0)
    {
        mouse->drag_panel=panel;
        mouse->button=right ? 3 : 1;
        mouse->last_index=index;
        mouse->dragged=0;
        FileNode *file=panel->files;
        for (int i=0; i < index; i++) file=file->next;
        mouse->mark=!file->is_selected;
        active_panel=panel;
    }
    int action=KEY_MOUSE;
    if (mouse->button)
    {
        if (index < 0 || panel != mouse->drag_panel) mouse->dragged=1;
        else
        {
            if (index != mouse->last_index) mouse->dragged=1;
            panel->selected_index=index;
            if (mouse->button == 3)
            {
                // Fill skipped rows too, so fast motion cannot leave holes in a drag.
                int start=index < mouse->last_index ? index : mouse->last_index;
                int end=index > mouse->last_index ? index : mouse->last_index;
                FileNode *file=panel->files;
                for (int i=0; file && i <= end; i++, file=file->next)
                    if (i >= start) select_file(panel, file, mouse->mark);
            }
            mouse->last_index=index;
        }
        if (released || clicked)
        {
            if (mouse->button == 1 && !mouse->dragged)
            {
                struct timeval now, elapsed;
                gettimeofday(&now, NULL);
                timersub(&now, &mouse->click_time, &elapsed);
                int same_file=mouse->click_panel == panel && mouse->click_index == index;
                int double_click=same_file && elapsed.tv_sec == 0 && elapsed.tv_usec < 300000;
                if (double_click || (event->bstate & BUTTON1_DOUBLE_CLICKED)) action='\n';
                mouse->click_panel=action == '\n' ? NULL : panel;
                mouse->click_index=index;
                mouse->click_time=now;
            }
            else mouse->click_panel=NULL;
            mouse->button=0;
        }
    }
    if (event->bstate & BUTTON4_PRESSED) active_panel->selected_index--;
#ifdef BUTTON5_PRESSED
    if (event->bstate & BUTTON5_PRESSED) active_panel->selected_index++;
#endif
    if (active_panel->selected_index < 0) active_panel->selected_index=0;
    if (active_panel->selected_index >= active_panel->files_count) active_panel->selected_index=active_panel->files_count-1;
    return action;
}

// Shorten by terminal columns, keeping multibyte characters and their accents intact.
void shorten(char *name, int width, char *result) {
    result[0]='\0';
    if (width <= 0) return;
    wchar_t text[CMD_MAX];
    mbstate_t state={0};
    size_t count=0;
    while (*name && count < CMD_MAX - 1) {
        size_t bytes=mbrtowc(&text[count], name, MB_CUR_MAX, &state);
        // Invalid filename bytes and control characters get a display-only placeholder.
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            memset(&state, 0, sizeof(state));
            text[count]=L'?';
            bytes=1;
        }
        if (wcwidth(text[count]) < 0) text[count]=L'?';
        name+=bytes;
        count++;
    }
    text[count]=L'\0';
    if (wcswidth(text, count) > width) {
        size_t left=0, right=count;
        int left_width=0, right_width=0, half=(width - 1) / 2;
        while (left < count && left_width + wcwidth(text[left]) <= half)
            left_width+=wcwidth(text[left++]);
        while (right > left && right_width + wcwidth(text[right - 1]) <= width - 1 - half)
            right_width+=wcwidth(text[--right]);
        // Do not attach a suffix's orphaned combining marks to the truncation marker.
        while (right < count && wcwidth(text[right]) == 0) right++;
        memmove(text + left + 1, text + right, (count - right + 1) * sizeof(*text));
        text[left]=L'~';
    }
    wcstombs(result, text, CMD_MAX - 1);
    result[CMD_MAX - 1]='\0';
}

int file_has_extension(const char *filename, const char *extensions[]) {
    for (int i = 0; extensions[i]; i++) {
        if (strcmp(filename + strlen(filename) - strlen(extensions[i]), extensions[i]) == 0) {
            return 1;
        }
    }
    return 0;
}


int format_number(off_t num, char *str) {
    static char buf[20]; // Assuming number won't exceed 20 characters with commas
    char rev[20], *p = rev;
    int count = 0;

    do {
        if (count++ % 3 == 0 && count > 1) *p++ = ',';
        *p++ = '0' + num % 10;
        num /= 10;
    } while (num);

    *p = '\0';
    for (int i = 0, j = strlen(rev) - 1; j >= 0; j--, i++) {
        buf[i] = rev[j];
    }
    buf[strlen(rev)] = '\0';
    sprintf(str, "%s", buf);
    return 0;
}


void format_size_with_units(off_t size, char *size_str, size_t len, int maxlen) {
    snprintf(size_str, len, "%lld", size);

    if (strlen(size_str) > maxlen) {
        size /= 1024;
        snprintf(size_str, len, "%lldK", size);
        if (strlen(size_str) > maxlen) {
            size /= 1024;
            snprintf(size_str, len, "%lldM", size);
            if (strlen(size_str) > maxlen) {
                size /= 1024;
                snprintf(size_str, len, "%lldG", size);
                if (strlen(size_str) > maxlen) {
                    size /= 1024;
                    snprintf(size_str, len, "%lldT", size);
                }
            }
        }
    }
}



void update_panel(WINDOW *win, PanelProp *panel) {
    FileNode *current = panel->files;
    int line = 1;  // Start from the second row to avoid the border
    int width = getmaxx(win) - 2;
    int height = getmaxy(win);
    int name_width = width - 12 - 7 - 3;
    char info[CMD_MAX];

    // Get the current year
    time_t now = time(NULL);
    struct tm *current_tm = localtime(&now);
    int current_year = current_tm->tm_year;

    // reset color to default
    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    wattroff(win, A_BOLD);

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
        } else if (current->is_device) {
            prefix = '-';
            wattron(win, COLOR_PAIR(COLOR_MAGENTA_ON_BLUE));
            wattron(win, A_BOLD);
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

        char size_str[50];  // Buffer to hold the size and suffix
        format_size_with_units(current->size, size_str, sizeof(size_str), 7);

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

        mvwaddstr(win, line, 2, SHORTEN(current->name, name_width));

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
    int title_end=getcurx(win);

    // reset color to default
    wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    mvwhline(win, 0, title_end, '-', width - title_end + 1);

    while(line < height - 3) {
        mvwhline(win, line, 1, ' ', name_width + 1);
        mvwprintw(win, line, width - 7 - 12, "       ");
        mvwprintw(win, line, width - 12 + 1, "            ");
        line++;
    }

    if (panel->search_mode == 1) {
        // Keep the end of the UTF-8 search text visible inside the panel.
        wattron(win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
        mvwhline(win, height-2, 1, ' ', width);
        mvwaddch(win, height-2, 1, '/');
        int start=text_column(panel->search_text, strlen(panel->search_text), 1)-width+1;
        draw_text(win, height-2, 2, panel->search_text, start > 0 ? start : 0, width-1);
        wattron(win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    } else {
        // print info for active file
        mvwhline(win, height - 2, 1, ' ', width);
        mvwaddstr(win, height - 2, 1, SHORTEN(info,width));
    }


    // print selected
    wattron(win, COLOR_PAIR(COLOR_YELLOW_ON_BLUE));
    wattron(win, A_BOLD);
    char num[20];
    format_number(panel->bytes_selected_files, num);
    sprintf(info," %s B in %d file%s ", num, panel->num_selected_files, panel->num_selected_files == 1 ? "" : "s");
    if (panel->num_selected_files > 0) mvwprintw(win, height - 3, width - strlen(info) - 3, "%s", info);
    wattroff(win, A_BOLD);

    wnoutrefresh(win);
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


void update_files_in_both_panels() {
    update_panel_files(&left_panel);
    update_panel_files(&right_panel);
    sort_file_nodes(&left_panel.files, left_panel.sort_order);
    sort_file_nodes(&right_panel.files, right_panel.sort_order);
    update_panel_cursor();
}

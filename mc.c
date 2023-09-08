#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <time.h>
#include <stdlib.h>

#define CMD_MAX 16384

struct utsname unameData;
struct passwd *pw;
const char *username;

int cursor_pos = 0;
int cmd_offset = 0;
int prompt_length = 0;

char left_path[CMD_MAX] = "/";
char right_path[CMD_MAX] = "/";
char *current_path = left_path;

void init_ui() {
    // Draw external borders, ending two lines from the bottom
    mvhline(0, 0, '-', COLS);
    mvhline(LINES - 3, 0, '-', COLS);
    mvvline(1, 0, '|', LINES - 4);
    mvvline(1, COLS - 1, '|', LINES - 4);
    mvaddch(0, 0, '+');
    mvaddch(0, COLS - 1, '+');
    mvaddch(LINES - 3, 0, '+');
    mvaddch(LINES - 3, COLS - 1, '+');

    // Draw internal vertical border to separate panels
    mvvline(1, COLS / 2, '|', LINES - 4);

    // Unchanged F keys
    mvprintw(LINES - 1, 1, "F1");
    mvprintw(LINES - 1, 9, "F2");
    mvprintw(LINES - 1, 17, "F3");
    mvprintw(LINES - 1, 25, "F4");
    mvprintw(LINES - 1, 33, "F5");
    mvprintw(LINES - 1, 41, "F6");
    mvprintw(LINES - 1, 49, "F7");
    mvprintw(LINES - 1, 57, "F8");
    mvprintw(LINES - 1, 65, "F9");
    mvprintw(LINES - 1, 74, "F10");

    // Highlighted labels for F keys
    attron(A_REVERSE);
    mvprintw(LINES - 1, 3, "Help ");
    mvprintw(LINES - 1, 11, "Menu ");
    mvprintw(LINES - 1, 19, "View ");
    mvprintw(LINES - 1, 27, "Edit ");
    mvprintw(LINES - 1, 35, "Copy ");
    mvprintw(LINES - 1, 43, "Move ");
    mvprintw(LINES - 1, 51, "Mkdir");
    mvprintw(LINES - 1, 59, "Del  ");
    mvprintw(LINES - 1, 67, "Rfrsh");
    mvprintw(LINES - 1, 77, "Quit ");
    attroff(A_REVERSE);


    // Refresh screen
    refresh();
}

void draw_panel(int y_start, int x_start, int height, int width, const char *path) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char filepath[CMD_MAX];
    int line = 0;

    dir = opendir(path);
    if (dir == NULL) {
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        snprintf(filepath, sizeof(filepath), "%s/%s", path, entry->d_name);
        stat(filepath, &file_stat);

        if (strcmp(entry->d_name, ".") == 0 || (strcmp(entry->d_name, "..") == 0 && strcmp(path, "/") == 0)) {
            continue;
        }

        char prefix = ' ';
        if (S_ISDIR(file_stat.st_mode)) {
            prefix = '/';
        } else if (S_ISLNK(file_stat.st_mode)) {
            prefix = '@';
        }

        struct tm *tm = localtime(&file_stat.st_mtime);
        char date_str[13];
        strftime(date_str, sizeof(date_str), "%b %d %H:%M", tm);

        long long size = file_stat.st_size;
        char *suffix = "";
        if (size >= 1024) {
            size /= 1024;
            suffix = "K";
            if (size >= 1024) {
                size /= 1024;
                suffix = "M";
                if (size >= 1024) {
                    size /= 1024;
                    suffix = "G";
                }
            }
        }

        mvprintw(y_start + line, x_start, "%c%-*s|%12s|%7lld%s", prefix, width - 22, entry->d_name, date_str, size, suffix);
        line++;
    }

    // Fill remaining lines with empty strings to maintain columns
    for (; line < height; ++line) {
        mvprintw(y_start + line, x_start, "%-*s|%12s|%7s", width - 20 - 1, "", "", "");
    }

    closedir(dir);

    // Ensure cursor is back to the command line
//    move(LINES - 2, strlen(username) + strlen(unameData.nodename) + strlen(current_path) + 6 + cursor_pos - cmd_offset);
    move(LINES - 2, prompt_length + cursor_pos - cmd_offset);
    curs_set(1);
}


int update_cmd(const char *current_path, const char *cmd) {

    // Print username, hostname, and current directory path
    move(LINES - 2, 1);
    clrtoeol();
    printw("%s@%s:%s# ", username, unameData.nodename, current_path);

    // Calculate max command display length
    prompt_length = strlen(username) + strlen(unameData.nodename) + strlen(current_path)
     + 5;  // 6 accounts for '@', ':', '#', and spaces.
    int max_cmd_display = COLS - prompt_length;

    // Print the visible part of the command, limited to max_cmd_display characters
    printw("%.*s", max_cmd_display, cmd + cmd_offset);

    // Move the cursor to the desired position and make it visible
    move(LINES - 2, prompt_length + cursor_pos - cmd_offset);
    curs_set(1);

    // Refresh only the changed parts
    refresh();
    return prompt_length;
}


void init_all() {
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);
    init_ui();
}


int main() {
    init_all();

    char cmd[CMD_MAX] = {0};
    int cmd_len = 0;

    uname(&unameData);
    pw = getpwuid(getuid());
    username = pw->pw_name;

    int current_panel = 0; // 0 for left, 1 for right

    while (1) {
        update_cmd(current_path, cmd);
        draw_panel(1, 1, LINES - 5, COLS / 2 - 1, left_path);
        draw_panel(1, COLS / 2 + 1, LINES - 5, COLS / 2 - 1, right_path);
        refresh();

        int ch = getch();
        if (ch == KEY_F(10)) {
            break;
        } else if (ch == '\n' && cmd_len > 0) {
            endwin();  // End ncurses mode
            printf("%s@%s:%s# %s\n", username, unameData.nodename, current_path, cmd);
            system(cmd);  // Execute the command
            init_all();
            memset(cmd, 0, CMD_MAX);
            cmd_len = cursor_pos = cmd_offset = prompt_length = 0;
        } else if (ch == 12) {  // Ctrl+L
            endwin();
            init_all();
        } else if (ch == 15) {  // Ctrl+O
            endwin();
            initscr();
            raw();
            getch();
            init_all();
        } else if (ch == KEY_BACKSPACE && cursor_pos > 0) {
            memmove(cmd + cursor_pos - 1, cmd + cursor_pos, cmd_len - cursor_pos);
            cmd[--cmd_len] = '\0';
            cursor_pos--;
        } else if (ch == KEY_DC && cursor_pos < cmd_len) {
            memmove(cmd + cursor_pos, cmd + cursor_pos + 1, cmd_len - cursor_pos - 1);
            cmd[--cmd_len] = '\0';
        } else if (ch == KEY_LEFT) {
            if (cursor_pos > 0) {
                cursor_pos--;
            } else if (cmd_offset > 0) {
                cmd_offset--;
            }
        } else if (ch == KEY_RIGHT && cursor_pos < cmd_len) {
            cursor_pos++;
        } else if (ch >= 32 && ch <= 126 && cmd_len < CMD_MAX - 1) {
            memmove(cmd + cursor_pos + 1, cmd + cursor_pos, cmd_len - cursor_pos);
            cmd[cursor_pos] = ch;
            cmd[++cmd_len] = '\0';
            cursor_pos++;
        } else if (ch == '\t') {
            current_panel = 1 - current_panel;
            current_path = current_panel == 0 ? left_path : right_path;
        }


        // Handle scrolling
        int max_cmd_display = COLS - (strlen(username) + strlen(unameData.nodename)
             + strlen(current_path) + 6) - 1;
        if (cursor_pos - cmd_offset >= max_cmd_display) {
            cmd_offset++;
        } else if (cursor_pos - cmd_offset < 0 && cmd_offset > 0) {
            cmd_offset--;
        }

        if (cmd_len - cmd_offset < max_cmd_display) {
            cmd_offset = cmd_len - max_cmd_display;
            if (cmd_offset < 0) {
                cmd_offset = 0;
            }
        }
    }

    endwin();
    return 0;
}



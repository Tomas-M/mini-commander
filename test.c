#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <stdlib.h>

#define CMD_MAX 16384

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

char left_path[CMD_MAX] = "/home";
char right_path[CMD_MAX] = "/bin";
char *current_path = left_path;


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

        if (i == num_buttons - 1) {  // Last button (F10)
            mvprintw(maxY - 1, x, "F%d ", i + 2);
        } else {
            mvprintw(maxY - 1, x, "F%d", i + 2);
        }

        attron(A_REVERSE);
        mvprintw(maxY - 1, x + 2 + (i == num_buttons - 1), "%-*s", button_width - 2 + extra, buttons[i]);
        attroff(A_REVERSE);

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
        winWidth1 += 1;
    }

    // Delete old windows
    delwin(win1);
    delwin(win2);

    // Create new windows
    win1 = newwin(winHeight, winWidth1, 0, 0);
    win2 = newwin(winHeight, winWidth2, 0, winWidth1);

    // Add borders to windows using wborder()
    wborder(win1, '|', '|', '-', '-', '+', '+', '+', '+');
    wborder(win2, '|', '|', '-', '-', '+', '+', '+', '+');

    // Refresh windows to make borders visible
    wrefresh(win1);
    wrefresh(win2);
}


void update_cmd() {

    // Print username, hostname, and current directory path
    move(LINES - 2, 0);
    clrtoeol();
    printw("%s@%s:%s# ", username, unameData.nodename, current_path);

    // Calculate max command display length
    prompt_length = strlen(username) + strlen(unameData.nodename) + strlen(current_path) + 4;  // 5 accounts for '@', ':', '#', and spaces.
    int max_cmd_display = COLS - prompt_length;

    // Print the visible part of the command, limited to max_cmd_display characters
    printw("%.*s", max_cmd_display, cmd + cmd_offset);

    // Refresh only the changed parts
    refresh();

    // move cursor where it belongs
    move(LINES - 2, prompt_length + cursor_pos - cmd_offset);
    curs_set(1);

    return;
}


void init_all() {
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(1);
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

   refresh();
   draw_windows(maxY, maxX);
   draw_buttons(maxY, maxX);
   update_cmd();
   refresh();
}

int main() {
    init_all();

    uname(&unameData);
    pw = getpwuid(getuid());
    username = pw->pw_name;

    while (1) {
        redraw_ui();

        int ch = getch();
        if (ch == KEY_F(10)) {
            break;
        } else if (ch == KEY_RESIZE) {  // Handle terminal resize
            endwin();
            init_all();
            update_cmd();
        } else if (ch == '\n' && cmd_len > 0) {
            endwin();  // End ncurses mode
            if (strcmp(cmd, "exit") == 0) exit(0);
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
            /// TBD
        }


        // Handle scrolling in command line
        int max_cmd_display = COLS - (strlen(username) + strlen(unameData.nodename) + strlen(current_path) + 6) - 1;
        if (cursor_pos - cmd_offset >= max_cmd_display) {
            cmd_offset++;
        } else if (cursor_pos - cmd_offset < 0 && cmd_offset > 0) {
            cmd_offset--;
        }

        if (cmd_offset < 0) {
                cmd_offset = 0;
        }
    }

    endwin();
    cleanup();
    return 0;
}

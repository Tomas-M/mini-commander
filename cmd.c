#include "includes.h"
#include "types.h"
#include "globals.h"

// Execute a selected path literally, without passing its name through a shell.
int execute_file(const char *filename)
{
    // Interrupt the child without terminating the file manager waiting for it.
    void (*interrupt_handler)(int)=signal(SIGINT, SIG_IGN);
    void (*quit_handler)(int)=signal(SIGQUIT, SIG_IGN);
    pid_t child=fork();
    if (child == 0)
    {
        signal(SIGINT, interrupt_handler);
        signal(SIGQUIT, quit_handler);
        execl(filename, filename, (char *)NULL);
        perror(filename);
        _exit(127);
    }
    int status=-1;
    if (child > 0)
        while (waitpid(child, &status, 0) < 0)
            if (errno != EINTR) { status=-1; break; }
    int error=errno;
    signal(SIGINT, interrupt_handler);
    signal(SIGQUIT, quit_handler);
    errno=error;
    return status;
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
    prompt_length = getcurx(stdscr);
    int max_cmd_display = COLS - prompt_length;

    // Print the visible part of the command, limited to max_cmd_display characters
    printw("%.*s", max_cmd_display, cmd + cmd_offset);

    cursor_to_cmd();

    // Refresh only the changed parts
    refresh();

    return;
}


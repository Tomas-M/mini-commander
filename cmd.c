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

// Keep the prompt and command on one row, scrolling in terminal columns.
void update_cmd()
{
    attron(COLOR_PAIR(COLOR_WHITE_ON_BLACK));
    move(LINES-2, 0);
    clrtoeol();
    char prompt[CMD_MAX];
    snprintf(prompt, sizeof(prompt), "%s@%s:%s# ", username, unameData.nodename, active_panel->path);
    addstr(SHORTEN(prompt, COLS-4));
    prompt_length=getcurx(stdscr);

    int width=COLS-prompt_length, column=text_column(cmd, cursor_pos, 1);
    cursor_pos=text_offset(cmd, cmd_len, column, 1);
    if (column-cmd_offset >= width) cmd_offset=column-width+1;
    if (column < cmd_offset) cmd_offset=column;
    draw_text(stdscr, LINES-2, prompt_length, cmd, cmd_offset, width);
    move(LINES-2, prompt_length+column-cmd_offset);
    wnoutrefresh(stdscr);
}


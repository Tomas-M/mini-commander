#include "includes.h"
#include "types.h"
#include "globals.h"

// Handle a simple quoted/escaped cd path; compound commands retain shell semantics.
int command_cd(const char *command)
{
    while (*command == ' ' || *command == '\t') command++;
    if (strncmp(command, "cd", 2) || (command[2] && command[2] != ' ' && command[2] != '\t')) return 0;
    command+=2;
    while (*command == ' ' || *command == '\t') command++;
    char path[CMD_MAX];
    int length=0, quote=0, finished=0;
    for (; *command; command++)
    {
        int ch=(unsigned char)*command;
        if (!quote && strchr(";&|<>\n", ch)) return 0;
        if (!quote && (ch == ' ' || ch == '\t')) { finished=1; continue; }
        if (finished) { show_errormsg("cd accepts one directory path"); return 1; }
        if (ch == quote) { quote=0; continue; }
        if (!quote && (ch == '\'' || ch == '"')) { quote=ch; continue; }
        if (ch == '\\' && quote != '\'')
        {
            if (!command[1]) { quote=1; break; }
            ch=(unsigned char)*++command;
        }
        path[length++]=ch;
    }
    path[length]=0;
    if (quote) { show_errormsg("Unclosed quote or escape in cd path"); return 1; }
    if (change_panel_directory(active_panel, length ? path : "~") != 0)
        show_errormsg(SPRINTF("Cannot change directory:\n%s\n%s", path, strerror(errno)));
    return 1;
}

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


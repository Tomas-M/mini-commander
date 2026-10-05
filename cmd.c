#include "includes.h"
#include "types.h"
#include "globals.h"

typedef struct command_entry {
    struct command_entry *previous, *next;
    char text[];
} command_entry;
static command_entry *history_last, *history_cursor;
static char *history_draft;

void command_history_add(const char *command)
{
    history_cursor=NULL;
    free(history_draft);
    history_draft=NULL;
    if (!*command || (history_last && !strcmp(history_last->text, command))) return;
    command_entry *entry=malloc(sizeof(*entry)+strlen(command)+1);
    if (!entry) return;
    *entry=(command_entry){.previous=history_last};
    strcpy(entry->text, command);
    if (history_last) history_last->next=entry;
    history_last=entry;
}

void command_history_move(int direction)
{
    if (direction < 0)
    {
        if (!history_cursor)
        {
            if (!history_last) return;
            char *draft=strdup(cmd);
            if (!draft) return;
            free(history_draft);
            history_draft=draft;
            history_cursor=history_last;
        }
        else if (history_cursor->previous) history_cursor=history_cursor->previous;
    }
    else
    {
        if (!history_cursor) return;
        history_cursor=history_cursor->next;
    }
    snprintf(cmd, CMD_MAX, "%s", history_cursor ? history_cursor->text : history_draft);
    cmd_len=cursor_pos=strlen(cmd);
    cmd_offset=0;
}

void command_history_free(void)
{
    while (history_last)
    {
        command_entry *previous=history_last->previous;
        free(history_last);
        history_last=previous;
    }
    history_cursor=NULL;
    free(history_draft);
    history_draft=NULL;
}

// Insert one shell word, escaping apostrophes without expanding shell syntax.
int insert_shell_path(const char *path, int directory)
{
    size_t path_length=strlen(path);
    int slash=directory && (!path_length || path[path_length-1] != '/');
    size_t length=path_length+2+(directory ? slash : 1);
    for (const char *p=path; *p; p++) if (*p == '\'') length+=3;
    int relative_option=!directory && path[0] == '-';
    length+=relative_option ? 2 : 0;
    if (length >= CMD_MAX-cmd_len) { errno=ENAMETOOLONG; return -1; }
    char quoted[CMD_MAX], *out=quoted;
    *out++='\'';
    if (relative_option) { *out++='.'; *out++='/'; }
    for (const char *p=path; *p; p++)
        if (*p == '\'') { memcpy(out, "'\\''", 4); out+=4; }
        else *out++=*p;
    if (slash) *out++='/';
    *out++='\'';
    if (!directory) *out++=' ';
    // A directory word has no trailing space so a filename can be appended.
    memmove(cmd+cursor_pos+length, cmd+cursor_pos, cmd_len-cursor_pos+1);
    memcpy(cmd+cursor_pos, quoted, length);
    cmd_len+=length;
    cursor_pos+=length;
    return 0;
}

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


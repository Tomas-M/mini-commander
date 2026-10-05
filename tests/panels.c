// Run with: gcc -Os -ffunction-sections -fdata-sections tests/panels.c filelist.c panel.c cmd.c ui.c -Wl,--gc-sections -lncursesw -o /tmp/mc-panels-test && /tmp/mc-panels-test
#include "../includes.h"
#include "../types.h"
#include "../globals.h"
#include <assert.h>

PanelProp left_panel={0}, right_panel={0}, *active_panel=&left_panel;
char cmd[CMD_MAX];
int cmd_len, cursor_pos, cmd_offset, errors;

void show_errormsg(char *message) { errors++; }
char *format_text(char *buffer, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(buffer, CMD_MAX, format, arguments);
    va_end(arguments);
    return buffer;
}

static void command(const char *text)
{
    strcpy(cmd, text);
    cursor_pos=cmd_len=strlen(cmd);
    cmd_offset=0;
}

static FileNode *at(PanelProp *panel, int index)
{
    FileNode *file=panel->files;
    while (file && index--) file=file->next;
    return file;
}

int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    char directory[]="/tmp/mc-panels-XXXXXX";
    assert(mkdtemp(directory) && chdir(directory) == 0);
    for (int i=0; i < 100; i++)
    {
        char name[16];
        snprintf(name, sizeof(name), "file%03d.log", i);
        int fd=open(name, O_CREAT|O_WRONLY, 0600);
        assert(fd >= 0 && write(fd, "x", 1) == 1 && close(fd) == 0);
    }
    assert(mkdir("a 'b$中", 0700) == 0);
    strcpy(left_panel.path, directory);
    strcpy(right_panel.path, directory);
    left_panel.sort_order=right_panel.sort_order=SORT_BY_NAME_DIRSFIRST_ASC;
    update_files_in_both_panels();
    left_panel.selected_index=70; left_panel.scroll_index=61;
    right_panel.selected_index=42; right_panel.scroll_index=33;
    char left_name[32], right_name[32];
    strcpy(left_name, at(&left_panel, 70)->name);
    strcpy(right_name, at(&right_panel, 42)->name);
    select_pattern(&left_panel, "file0?0.log", 1);
    assert(left_panel.num_selected_files == 10 && left_panel.bytes_selected_files == 10);
    select_pattern(&left_panel, "*", -1);
    assert(left_panel.num_selected_files == 91 && left_panel.bytes_selected_files == 90);
    select_pattern(&left_panel, "*", 0);
    assert(!left_panel.num_selected_files && !left_panel.bytes_selected_files);
    update_files_in_both_panels();
    assert(!strcmp(at(&left_panel, left_panel.selected_index)->name, left_name) && left_panel.scroll_index == 61);
    assert(!strcmp(at(&right_panel, right_panel.selected_index)->name, right_name) && right_panel.scroll_index == 33);
    left_panel.sort_order=SORT_BY_NAME_DIRSFIRST_DESC;
    update_files_in_both_panels();
    assert(!strcmp(at(&left_panel, left_panel.selected_index)->name, left_name));
    swap_panels();
    assert(active_panel == &right_panel && right_panel.sort_order == SORT_BY_NAME_DIRSFIRST_DESC);
    assert(!strcmp(at(&right_panel, right_panel.selected_index)->name, left_name));
    assert(left_panel.scroll_index == 33);
    command_history_add("first"); command_history_add("second"); command_history_add("second");
    command("draft");
    command_history_move(-1); assert(!strcmp(cmd, "second"));
    command_history_move(-1); assert(!strcmp(cmd, "first"));
    command_history_move(-1); assert(!strcmp(cmd, "first"));
    command_history_move(1); assert(!strcmp(cmd, "second"));
    command_history_move(1); assert(!strcmp(cmd, "draft") && cursor_pos == 5);
    command_history_move(1); assert(!strcmp(cmd, "draft"));
    command_history_free();
    command("cat ");
    assert(insert_shell_path("a 'b$中", 0) == 0 && !strcmp(cmd, "cat 'a '\\''b$中' "));
    command(""); assert(insert_shell_path("-rf", 0) == 0 && !strcmp(cmd, "'./-rf' "));
    command("echo tail"); cursor_pos=5;
    assert(insert_shell_path("x y", 0) == 0 && !strcmp(cmd, "echo 'x y' tail"));
    command(""); assert(insert_shell_path("/", 1) == 0 && !strcmp(cmd, "'/'"));
    command(""); assert(insert_shell_path("/x y", 1) == 0 && !strcmp(cmd, "'/x y/'"));
    memset(cmd, 'x', CMD_MAX-2); cmd[CMD_MAX-2]=0; cursor_pos=cmd_len=CMD_MAX-2;
    assert(insert_shell_path("a", 0) == -1 && cmd_len == CMD_MAX-2 && cmd[CMD_MAX-2] == 0);
    assert(command_cd("cd 'a '\\''b$中'") == 1 && !errors);
    assert(strstr(active_panel->path, "/a 'b$中"));
    assert(command_cd("cd ..") == 1 && !strcmp(active_panel->path, directory));
    assert(command_cd("cd missing") == 1 && errors == 1 && !strcmp(active_panel->path, directory));
    assert(!command_cd("cd /tmp && pwd") && !command_cd("echo cd"));
    setenv("HOME", directory, 1);
    assert(command_cd("cd") == 1 && !strcmp(active_panel->path, directory));
    assert(rmdir("a 'b$中") == 0);
    for (int i=0; i < 100; i++) { char name[16]; snprintf(name, sizeof(name), "file%03d.log", i); assert(unlink(name) == 0); }
    free_file_nodes(left_panel.files); free_file_nodes(right_panel.files);
    assert(chdir("/") == 0 && rmdir(directory) == 0);
    puts("Panels: refresh positions, masks, swap, command history, shell quoting and cd passed.");
}

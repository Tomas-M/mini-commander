// Build with the normal application sources except mc.c, which is included below.
#define main commander_main
#include "../mc.c"
#undef main
#include <assert.h>

extern SCREEN *screen;

// Negative values represent function keys; positive values are Unicode characters.
static void queue_input(const int *input, int count)
{
    for (int i=count-1; i >= 0; i--)
        assert((input[i] < 0 ? ungetch(-input[i]) : unget_wch(input[i])) == OK);
}

// Exercise the real editor and save dialog, then compare the bytes written to disk.
static void edit_case(const char *initial, const char *expected, const int *input, int count)
{
    char path[]="mc-utf8-XXXXXX", result[4096];
    int fd=mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, initial, strlen(initial)) == strlen(initial));
    close(fd);
    int save[]={-KEY_F(2), '\n', -KEY_F(10)};
    queue_input(save, sizeof(save)/sizeof(*save));
    queue_input(input, count);
    assert(edit_file(path) == 0);
    fd=open(path, O_RDONLY);
    int bytes=read(fd, result, sizeof(result));
    assert(bytes == strlen(expected));
    assert(memcmp(result, expected, bytes) == 0);
    close(fd);
    unlink(path);
}

// Read a rendered terminal cell, including its combining marks and attributes.
static void check_cell(WINDOW *win, int row, int column, const wchar_t *expected, int pair)
{
    cchar_t cell;
    wchar_t chars[CCHARW_MAX];
    attr_t attributes;
    short color;
    assert(mvwin_wch(win, row, column, &cell) == OK);
    assert(getcchar(&cell, chars, &attributes, &color, NULL) == OK);
    assert(wcscmp(chars, expected) == 0);
    if (pair >= 0) assert(color == pair);
}

// Run without a physical terminal; ncurses still decodes input and renders real cells.
int main(void)
{
    alarm(20);
    assert(setlocale(LC_CTYPE, "C.UTF-8"));
    FILE *output=tmpfile(), *input=tmpfile();
    screen=newterm("xterm", output, input);
    assert(screen);
    init_screen();
    mousemask(ALL_MOUSE_EVENTS, NULL);
    resizeterm(24, 100);

    const char *mixed="ač中e\xcc\x81";
    assert(text_column(mixed, strlen(mixed)) == 5);
    assert(text_offset(mixed, strlen(mixed), 3) == 3);
    assert(text_offset(mixed, strlen(mixed), 4) == 6);
    assert(text_previous(mixed, strlen(mixed)) == 6);
    assert(text_previous(mixed, 6) == 3);
    assert(text_column("\xff\xc4", 2) == 2);
    assert(text_column("\0\t", 2) == 2);

    int typing[]={0x103, 0x169, 0x4e2d, 'e', 0x301};
    edit_case("", "ăũ中e\xcc\x81", typing, sizeof(typing)/sizeof(*typing));
    // Linux has 32-bit wchar_t; Windows ncurses builds cannot queue non-BMP characters.
    if (WCHAR_MAX >= 0x10ffff)
    {
        int emoji[]={0x1f600, -KEY_LEFT, -KEY_DC, 0x1f642};
        edit_case("", "🙂", emoji, sizeof(emoji)/sizeof(*emoji));
    }
    int deleting[]={-KEY_RIGHT, -KEY_DC, -KEY_END, -KEY_BACKSPACE};
    edit_case("ač中e\xcc\x81", "a中", deleting, sizeof(deleting)/sizeof(*deleting));
    int splitting[]={-KEY_RIGHT, '\n', -KEY_BACKSPACE, -KEY_RIGHT, '\n', 0x17e};
    edit_case("č中x", "č中\nžx", splitting, sizeof(splitting)/sizeof(*splitting));
    int merging[]={-KEY_END, -KEY_DC, -KEY_RIGHT, -KEY_BACKSPACE};
    edit_case("č\n中", "č", merging, sizeof(merging)/sizeof(*merging));
    int combining_join[]={-KEY_END, -KEY_DC, -KEY_BACKSPACE};
    edit_case("e\n\xcc\x81", "", combining_join, sizeof(combining_join)/sizeof(*combining_join));
    int vertical[]={-KEY_END, -KEY_DOWN, 'X', -KEY_UP, 'Y'};
    edit_case("č中\na中b", "č中Y\na中Xb", vertical, sizeof(vertical)/sizeof(*vertical));
    int previous[]={-KEY_DOWN, -KEY_LEFT, 'X'};
    edit_case("č\n中", "čX\n中", previous, sizeof(previous)/sizeof(*previous));
    int tabs[]={'\t', 0x10d};
    edit_case("", "\tč", tabs, sizeof(tabs)/sizeof(*tabs));
    edit_case("\xef\xbb\xbf" "č\r\n中\n", "\xef\xbb\xbf" "č\r\n中\n", NULL, 0);
    edit_case("a\xff\xc4", "a\xff\xc4", NULL, 0);
    int search[]={-KEY_F(7), 0x10d, '\n', 'X'};
    edit_case("a中Č", "a中XČ", search, sizeof(search)/sizeof(*search));
    int next_search[]={-KEY_F(7), 0x10d, '\n', -KEY_F(19), 'X'};
    edit_case("aČ\nč", "aČ\nXč", next_search, sizeof(next_search)/sizeof(*next_search));

    // Horizontal scrolling and page movement must keep byte offsets on character boundaries.
    resizeterm(10, 45);
    char long_line[256]="";
    for (int i=0; i < 40; i++) strcat(long_line, "中");
    char expected[256];
    strcpy(expected, long_line);
    strcat(expected, "č");
    int end[]={-KEY_END, 0x10d};
    edit_case(long_line, expected, end, sizeof(end)/sizeof(*end));
    int page[]={-KEY_NPAGE, -KEY_HOME, -KEY_BACKSPACE};
    edit_case("č\n中\nč\n中\nč\n中\nč\n中\nč\n中", "č\n中\nč\n中\nč\n中\nč\n中č\n中", page, sizeof(page)/sizeof(*page));
    resizeterm(24, 100);

    // Clicking either half of a wide character snaps to its start.
    MEVENT event={.x=2, .y=1, .bstate=BUTTON1_CLICKED};
    int save[]={-KEY_F(2), '\n', -KEY_F(10)};
    queue_input(save, sizeof(save)/sizeof(*save));
    assert(unget_wch('X') == OK);
    assert(ungetmouse(&event) == OK);
    char path[]="mc-mouse-XXXXXX", result[32]={0};
    int fd=mkstemp(path);
    assert(write(fd, "a中b", 5) == 5);
    close(fd);
    assert(edit_file(path) == 0);
    fd=open(path, O_RDONLY);
    assert(read(fd, result, sizeof(result)) == 6);
    assert(strcmp(result, "aX中b") == 0);
    close(fd);
    unlink(path);

    char prompt[CMD_MAX]="č中";
    int prompt_keys[]={-KEY_LEFT, -KEY_BACKSPACE, 0x103, '\n'};
    queue_input(prompt_keys, sizeof(prompt_keys)/sizeof(*prompt_keys));
    assert(show_dialog("Find", (char *[]) {"Find", "Cancel", NULL}, 0, prompt, 0, 0) == 1);
    assert(strcmp(prompt, "ă中") == 0);

    WINDOW *win=newwin(2, 6, 0, 0);
    file_lines line={.line="a中e\xcc\x81" "b", .line_length=8};
    werase(win);
    display_line(win, &line, 6, 0, 1, NULL, 0);
    check_cell(win, 0, 0, L"a", -1);
    check_cell(win, 0, 1, L"中", -1);
    check_cell(win, 0, 3, L"e\x301", -1);
    check_cell(win, 0, 4, L"b", -1);
    check_cell(win, 1, 0, L" ", -1);
    werase(win);
    display_line(win, &line, 6, 2, 1, NULL, 0);
    check_cell(win, 0, 0, L" ", -1);
    check_cell(win, 0, 1, L"e\x301", -1);
    werase(win);
    display_line(win, &line, 2, 0, 0, NULL, 0);
    check_cell(win, 0, 0, L"a", -1);
    check_cell(win, 0, 1, L" ", -1);

    PatternColorPair pattern={".*", COLOR_PAIR(COLOR_GREEN_ON_BLUE), 0, {0}};
    assert(regcomp(&pattern.regex, pattern.pattern, REG_EXTENDED) == 0);
    werase(win);
    display_line(win, &line, 6, 0, 1, &pattern, 1);
    check_cell(win, 0, 1, L"中", COLOR_GREEN_ON_BLUE);
    check_cell(win, 0, 3, L"e\x301", COLOR_GREEN_ON_BLUE);
    check_cell(win, 1, 0, L" ", -1);
    regfree(&pattern.regex);
    delwin(win);
    endwin();
    delscreen(screen);
    fclose(output);
    fclose(input);
    alarm(0);
    puts("UTF-8 editor, dialogs, search, saving and rendering: passed");
    return 0;
}

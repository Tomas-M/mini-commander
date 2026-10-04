// Run with: gcc -Os -ffunction-sections -fdata-sections tests/editor.c ui.c dialog.c init.c -Wl,--gc-sections -lncursesw -o /tmp/mc-editor-test && /tmp/mc-editor-test
#include "../includes.h"
#include <assert.h>

static void *test_malloc(size_t size);
#define malloc test_malloc
#define read_text_key test_read_key
#define show_dialog test_dialog
#define show_errormsg test_error
#include "../view_edit.c"
#undef malloc
#undef read_text_key
#undef show_dialog
#undef show_errormsg

int color_enabled=1;
extern SCREEN *screen;
static int allocation_failure=-1, event_index, editing;
static const char *test_name;
typedef struct { int key; const char *text; int x, y; mmask_t mouse; } Event;
static const Event *events;
#define K(key) {key, NULL}
#define T(text) {0, text}
#define M(state, x, y) {KEY_MOUSE, NULL, x, y, state}
#define AT(y, x) {-2, NULL, x, y}
#define TOP(text) {-3, text}
#define SAVE_QUIT K(KEY_F(2)), K(KEY_F(10)), K(-1)

// Inject allocation failures into the range replacement without affecting ncurses.
static void *test_malloc(size_t size)
{
    if (allocation_failure == 0) { errno=ENOMEM; return NULL; }
    if (allocation_failure > 0) allocation_failure--;
    return malloc(size);
}

// A failed editor operation is always a test failure.
void test_error(char *message)
{
    fprintf(stderr, "%s: %s\n", test_name, message);
    abort();
}

// Confirm saves; the file contents are checked after the editor closes.
int test_dialog(char *title, char *buttons[], int selected, char *prompt, int danger, int vertical, int edit)
{
    assert(!prompt);
    return 1;
}

// Verify the actual staged footer before delivering each scripted key.
int test_read_key(WINDOW *win, char *text)
{
    const char *labels[]={"Save", "Mark", "", "Copy", "Move", "Search", "Delete", "", "Quit"};
    int row=getmaxy(stdscr)-1, columns=getmaxx(stdscr);
    for (int i=0; i < 9; i++)
    {
        const char *label=labels[i];
        if (!editing) label=i == 1 || i == 8 ? "Quit" : i == 5 ? "Search" : "";
        int x=i*columns/9;
        assert((mvwinch(newscr, row, x) & A_CHARTEXT) == 'F');
        x+=i == 8 ? 3 : 2;
        for (int j=0; label[j]; j++) assert((mvwinch(newscr, row, x+j) & A_CHARTEXT) == (unsigned char)label[j]);
        for (int column=x; column < (i+1)*columns/9-1; column++)
        {
            chtype cell=mvwinch(newscr, row, column);
            assert(PAIR_NUMBER(cell) == COLOR_BLACK_ON_CYAN);
            if (column-x >= strlen(label)) assert((cell & A_CHARTEXT) == ' ');
        }
    }
    Event event=events[event_index++];
    while (event.key == -2 || event.key == -3)
    {
        int y=getcury(win), x=getcurx(win);
        if (event.key == -2) assert(y == event.y && x == event.x);
        else
        {
            char shown[80]={0};
            mvwinnstr(win, 0, 0, shown, strlen(event.text));
            assert(!strcmp(shown, event.text));
            wmove(win, y, x);
        }
        event=events[event_index++];
    }
    assert(event.key != -1);
    text[0]=0;
    if (event.text) strcpy(text, event.text);
    if (event.key == KEY_RESIZE) resizeterm(20, 100);
    if (event.key == KEY_MOUSE)
    {
        MEVENT mouse={0, event.x, event.y, 0, event.mouse};
        assert(ungetmouse(&mouse) == OK);
        // The scripted reader bypasses wget_wch, so consume ungetmouse's queued key.
        assert(wgetch(win) == KEY_MOUSE);
    }
    return event.key;
}

// Exercise the real editor event loop and check the saved bytes independently.
static void check_editor(const char *name, const char *original, const char *expected, const Event *keys)
{
    test_name=name;
    events=keys;
    event_index=0;
    resizeterm(24, 80);
    char path[]="/tmp/mc-editor-XXXXXX.c";
    int fd=mkstemps(path, 2);
    assert(fd >= 0);
    assert(write(fd, original, strlen(original)) == (ssize_t)strlen(original));
    close(fd);
    mmask_t mask=mousemask(ALL_MOUSE_EVENTS, NULL), restored;
    int interval=mouseinterval(-1);
    assert(view_edit_file(path, editing) == 0);
    mousemask(mask, &restored);
    assert(restored == mask && mouseinterval(-1) == interval);
    assert(events[event_index].key == -1);
    off_t rows, bytes;
    file_lines *lines=read_file_lines(path, &rows, &bytes);
    char actual[2048]={0};
    assert(bytes < sizeof(actual));
    copy_text_range(lines, 0, bytes, actual);
    if (bytes != strlen(expected) || memcmp(actual, expected, bytes))
    {
        fprintf(stderr, "%s: expected [%s], got [%s]\n", name, expected, actual);
        abort();
    }
    free_file_lines(lines);
    unlink(path);
}

// Compare byte-range edits to a flat byte-array model, including NULs and newlines.
static void check_ranges(void)
{
    file_lines *lines=malloc(sizeof(*lines));
    *lines=(file_lines){malloc(1), 0, NULL};
    unsigned char expected[1024]={0}, actual[1024], inserted[80];
    size_t length=0;
    off_t rows=1;
    srand(1);
    for (int trial=0; trial < 10000; trial++)
    {
        size_t start=rand()%(length+1), end=start+rand()%(length-start+1), added=rand()%80;
        for (size_t i=0; i < added; i++) inserted[i]=rand()%5 ? rand()%256 : '\n';
        assert(length+added < sizeof(expected));
        copy_text_range(lines, start, end, (char *)actual);
        assert(!memcmp(actual, expected+start, end-start));
        assert(replace_text_range(lines, start, end, (char *)inserted, added, &rows) == 0);
        memmove(expected+start+added, expected+end, length-end);
        memcpy(expected+start, inserted, added);
        length+=added-(end-start);
        copy_text_range(lines, 0, length, (char *)actual);
        assert(!memcmp(actual, expected, length));
        off_t count=1;
        for (size_t i=0; i < length; i++) if (expected[i] == '\n') count++;
        assert(rows == count);
    }
    // Every possible allocation failure must leave both content and line count intact.
    for (int failure=0; failure < 7; failure++)
    {
        off_t previous_rows=rows;
        allocation_failure=failure;
        assert(replace_text_range(lines, 0, length, "a\nb\nc", 5, &rows) == -1);
        allocation_failure=-1;
        copy_text_range(lines, 0, length, (char *)actual);
        assert(rows == previous_rows && !memcmp(actual, expected, length));
    }
    free_file_lines(lines);
}

// Selection overrides syntax/control colors without drawing a space for the newline.
static void check_rendering(void)
{
    file_lines next={"", 0, NULL}, line={"int\t\001中é", strlen("int\t\001中é"), &next};
    WINDOW *win=newwin(2, 20, 0, 0);
    for (int monochrome=0; monochrome < 2; monochrome++)
    {
        color_enabled=!monochrome;
        init_screen();
        werase(win);
        display_line(win, &line, 20, 0, 1, SYNTAX_C, 1, line.line_length+1);
        for (int column=1; column < 12; column++)
        {
            cchar_t cell;
            wchar_t chars[CCHARW_MAX];
            attr_t attributes;
            short pair;
            mvwin_wch(win, 0, column, &cell);
            getcchar(&cell, chars, &attributes, &pair, NULL);
            assert(pair == COLOR_BLACK_ON_CYAN);
            assert(!(attributes & (A_REVERSE|A_BOLD)));
            short foreground, background;
            pair_content(pair, &foreground, &background);
            assert(foreground == COLOR_BLACK && background == (monochrome ? COLOR_WHITE : COLOR_CYAN));
        }
        assert(PAIR_NUMBER(mvwinch(win, 0, 0)) != COLOR_BLACK_ON_CYAN);
        assert(PAIR_NUMBER(mvwinch(win, 0, 12)) != COLOR_BLACK_ON_CYAN);
        for (int column=0; column < 5; column++)
            assert((mvwinch(win, 0, 3+column) & A_CHARTEXT) == (unsigned char)"<--->"[column]);
        werase(win);
        display_line(win, &line, 3, 9, 1, SYNTAX_C, 5, line.line_length);
        cchar_t cell;
        wchar_t chars[CCHARW_MAX];
        attr_t attributes;
        short pair;
        mvwin_wch(win, 0, 0, &cell);
        getcchar(&cell, chars, &attributes, &pair, NULL);
        assert(pair == COLOR_BLACK_ON_CYAN);
        // A horizontally clipped marker keeps the tab's selection across every visible cell.
        werase(win);
        display_line(win, &line, 3, 5, 1, SYNTAX_C, 3, 4);
        for (int column=0; column < 3; column++)
        {
            chtype cell=mvwinch(win, 0, column);
            assert((cell & A_CHARTEXT) == (unsigned char)"-->"[column]);
            assert(PAIR_NUMBER(cell) == COLOR_BLACK_ON_CYAN);
        }
        werase(win);
        display_line(win, &line, 20, 0, 0, 0, -1, -1);
        for (int column=3; column < 8; column++)
            assert((mvwinch(win, 0, column) & A_CHARTEXT) == ' ');
        assert((mvwinch(win, 0, 8) & A_CHARTEXT) == '.');
    }
    color_enabled=1;
    init_screen();
    delwin(win);
}

// Cover both directions, repeated block commands, UTF-8, edits, EOF, resize, and viewer isolation.
int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    check_ranges();
    FILE *output=tmpfile(), *input=tmpfile();
    screen=newterm("xterm", output, input);
    assert(screen);
    check_rendering();
    assert(text_column("\t中", 4, 5) == 7);
    for (int column=0; column < 5; column++) assert(text_offset("\t中", 4, column, 5) == 0);
    assert(text_offset("\t中", 4, 5, 5) == 1);
    assert(text_offset("\t中", 4, 6, 5) == 1);
    assert(text_offset("\t中", 4, 7, 5) == 4);
    mousemask(ALL_MOUSE_EVENTS, NULL);
    editing=1;
    check_editor("copy retains source", "abcDEFghi", "abcghiDEF", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_END), K(KEY_F(5)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("copy before source", "abcDEFghi", "DEFabcghi", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_HOME), K(KEY_F(5)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("move follows block", "abcDEFghi", "abcghi", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_END), K(KEY_F(6)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("move before source", "abcDEFghi", "DEFabcghi", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_HOME), K(KEY_F(6)), SAVE_QUIT});
    check_editor("reverse multiline copy", "až\n中é\nz", "azž\n中é\n", (Event[]){K(KEY_DOWN), K(KEY_DOWN), K(KEY_F(3)), K(KEY_UP), K(KEY_UP), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_DOWN), K(KEY_DOWN), K(KEY_END), K(KEY_F(5)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("multiline move", "ab\ncd\nef", "abef\ncd\n", (Event[]){K(KEY_END), K(KEY_F(3)), K(KEY_DOWN), K(KEY_DOWN), K(KEY_HOME), K(KEY_F(3)), K(KEY_END), K(KEY_F(6)), SAVE_QUIT});
    check_editor("whole file delete", "ž\n中\n", "", (Event[]){K(KEY_F(3)), K(KEY_NPAGE), K(KEY_END), K(KEY_F(3)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("newline delete", "ab\ncd", "abcd", (Event[]){K(KEY_END), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("active mark delete", "a中éz", "az", (Event[]){K(KEY_RIGHT), K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(8)), SAVE_QUIT});
    check_editor("source boundaries", "abc", "abc", (Event[]){K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_F(5)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("inside source no-op", "abcd", "abcd", (Event[]){K(KEY_F(3)), K(KEY_END), K(KEY_F(3)), K(KEY_LEFT), K(KEY_F(5)), K(KEY_F(6)), SAVE_QUIT});
    check_editor("marks follow edits", "abc\ndef", "Ž\nc\ndef", (Event[]){K(KEY_F(3)), K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_HOME), T("Ž"), K('\n'), K(KEY_F(8)), SAVE_QUIT});
    check_editor("normal UTF-8 editing", "aé\n中z", "aŽz", (Event[]){K(KEY_END), K(KEY_BACKSPACE), K(KEY_DC), K(KEY_DC), T("Ž"), SAVE_QUIT});
    check_editor("empty selection and resize", "", "中", (Event[]){K(KEY_F(5)), K(KEY_F(6)), K(KEY_F(8)), K(KEY_F(3)), K(KEY_F(3)), K(KEY_F(8)), K(KEY_RESIZE), T("中"), SAVE_QUIT});
    check_editor("tab atomic movement", "a\t中z", "a\tX中z", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), T("X"), SAVE_QUIT});
    check_editor("tab backspace", "a\t中z", "a中z", (Event[]){K(KEY_RIGHT), K(KEY_RIGHT), K(KEY_BACKSPACE), SAVE_QUIT});
    check_editor("tab delete", "a\t中z", "a中z", (Event[]){K(KEY_RIGHT), K(KEY_DC), SAVE_QUIT});
    check_editor("tab vertical navigation", "\tX\n1234567", "\tX\n12345Z67", (Event[]){K(KEY_RIGHT), K(KEY_DOWN), T("Z"), SAVE_QUIT});
    check_editor("tab mouse snapping", "a\t中z", "aX\t中z", (Event[]){M(BUTTON1_CLICKED, 3, 1), T("X"), SAVE_QUIT});
    check_editor("tab copy keeps bytes and cursor", "\t中", "\t中X\t", (Event[]){K(KEY_F(3)), K(KEY_RIGHT), K(KEY_F(3)), K(KEY_END), K(KEY_F(5)), T("X"), SAVE_QUIT});
    check_editor("combining mark after tab", "\t́X", "́X", (Event[]){K(KEY_DC), SAVE_QUIT});
    check_editor("tab insertion", "中", "\tX中", (Event[]){K('\t'), T("X"), SAVE_QUIT});
    check_editor("drag follows cursor and freezes on release", "abcDEFghi", "abcghi", (Event[]){M(BUTTON1_PRESSED, 3, 1), AT(0, 3), M(REPORT_MOUSE_POSITION, 6, 1), AT(0, 6), M(BUTTON1_RELEASED, 6, 1), K(KEY_LEFT), K(KEY_F(8)), SAVE_QUIT});
    check_editor("reverse drag", "abcDEFghi", "abcghi", (Event[]){M(BUTTON1_PRESSED, 6, 1), M(REPORT_MOUSE_POSITION, 3, 1), M(BUTTON1_RELEASED, 3, 1), K(KEY_F(8)), SAVE_QUIT});
    check_editor("release supplies final endpoint", "abcDEFghi", "abcghi", (Event[]){M(BUTTON1_PRESSED, 3, 1), M(BUTTON1_RELEASED, 6, 1), K(KEY_F(8)), SAVE_QUIT});
    check_editor("UTF-8 tab multiline drag", "a\t中é\nxyz", "az", (Event[]){M(BUTTON1_PRESSED, 3, 1), AT(0, 1), M(REPORT_MOUSE_POSITION, 2, 2), AT(1, 2), M(BUTTON1_RELEASED, 2, 2), K(KEY_F(8)), SAVE_QUIT});
    check_editor("click clears selection", "abcDEFghi", "abcDEFghi", (Event[]){M(BUTTON1_PRESSED, 3, 1), M(REPORT_MOUSE_POSITION, 6, 1), M(BUTTON1_RELEASED, 6, 1), M(BUTTON1_PRESSED, 0, 1), M(BUTTON1_RELEASED, 0, 1), K(KEY_F(5)), K(KEY_F(8)), SAVE_QUIT});
    check_editor("hover and other buttons do not move cursor", "abc", "Xabc", (Event[]){M(REPORT_MOUSE_POSITION, 2, 1), M(BUTTON3_PRESSED, 2, 1), AT(0, 0), T("X"), SAVE_QUIT});
    check_editor("drag back to anchor clears selection", "abcDEFghi", "abcDEFghi", (Event[]){M(BUTTON1_PRESSED, 3, 1), M(REPORT_MOUSE_POSITION, 6, 1), M(REPORT_MOUSE_POSITION, 3, 1), M(BUTTON1_RELEASED, 3, 1), K(KEY_F(8)), SAVE_QUIT});
    check_editor("press outside content does not start a drag", "abc", "Xabc", (Event[]){M(BUTTON1_PRESSED, 0, 0), M(REPORT_MOUSE_POSITION, 2, 1), M(BUTTON1_RELEASED, 2, 1), AT(0, 0), T("X"), SAVE_QUIT});
    char tall[181], wide[81], clipped[78];
    for (int row=0; row < 30; row++) snprintf(tall+6*row, 7, "row%02d\n", row);
    memset(wide, 'a', 78); memcpy(wide, "START", 5); strcpy(wide+78, "\tZ");
    strcpy(clipped, "A\t"); memset(clipped+2, 'a', 75); clipped[77]=0;
    check_editor("drag past bottom does not scroll", tall, tall, (Event[]){M(BUTTON1_PRESSED, 0, 10), M(REPORT_MOUSE_POSITION, 0, 99), AT(21, 0), TOP("row00"), M(BUTTON1_RELEASED, 0, 99), AT(21, 0), TOP("row00"), SAVE_QUIT});
    check_editor("drag past right edge does not scroll", wide, wide, (Event[]){M(BUTTON1_PRESSED, 77, 1), M(REPORT_MOUSE_POSITION, 99, 1), AT(0, 78), TOP("START"), M(BUTTON1_RELEASED, 99, 1), AT(0, 78), TOP("START"), SAVE_QUIT});
    check_editor("clipped tab keeps viewport on mouse input", clipped, clipped, (Event[]){K(KEY_END), TOP("--->"), M(BUTTON1_PRESSED, 0, 1), AT(0, 0), TOP("--->"), M(BUTTON1_RELEASED, 0, 1), AT(0, 0), TOP("--->"), SAVE_QUIT});
    editing=0;
    check_editor("viewer footer and read-only", "abc\ndef", "abc\ndef", (Event[]){K(KEY_F(5)), K(KEY_F(6)), K(KEY_F(8)), T("x"), K(KEY_RESIZE), K(KEY_F(3)), K(-1)});
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Editor: 10000 range edits, allocation failures, tab/selection rendering and 34 input sequences passed.");
    return 0;
}

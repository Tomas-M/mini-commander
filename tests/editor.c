// Run with: gcc -Os -ffunction-sections -fdata-sections tests/editor.c ui.c dialog.c -Wl,--gc-sections -lncursesw -o /tmp/mc-editor-test && /tmp/mc-editor-test
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
static int allocation_failure=-1, event_index, editing;
static const char *test_name;
typedef struct { int key; const char *text; } Event;
static const Event *events;
#define K(key) {key, NULL}
#define T(text) {0, text}
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
    const char *labels[]={"Save", "Mark", NULL, "Copy", "Move", "Search", "Delete", NULL, "Quit"};
    int row=getmaxy(stdscr)-1, columns=getmaxx(stdscr);
    for (int i=0; i < 9; i++)
    {
        const char *label=labels[i];
        if (!editing) label=i == 1 || i == 8 ? "Quit" : i == 5 ? "Search" : NULL;
        int x=i*columns/9;
        if (!label) { assert((mvwinch(newscr, row, x) & A_CHARTEXT) == ' '); continue; }
        x+=i == 8 ? 3 : 2;
        for (int j=0; label[j]; j++) assert((mvwinch(newscr, row, x+j) & A_CHARTEXT) == (unsigned char)label[j]);
    }
    Event event=events[event_index++];
    assert(event.key != -1);
    text[0]=0;
    if (event.text) strcpy(text, event.text);
    if (event.key == KEY_RESIZE) resizeterm(20, 100);
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
    assert(view_edit_file(path, editing) == 0);
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

// Selection must override syntax/control colors and include a selected newline.
static void check_rendering(void)
{
    file_lines next={"", 0, NULL}, line={"int\t\001中é", strlen("int\t\001中é"), &next};
    WINDOW *win=newwin(2, 20, 0, 0);
    for (int monochrome=0; monochrome < 2; monochrome++)
    {
        color_enabled=!monochrome;
        werase(win);
        display_line(win, &line, 20, 0, 1, SYNTAX_C, 1, line.line_length+1);
        for (int column=1; column <= 8; column++)
        {
            chtype cell=mvwinch(win, 0, column);
            if (PAIR_NUMBER(cell) != COLOR_BLACK_ON_CYAN)
                fprintf(stderr, "selection column=%d mono=%d pair=%d char=%lu\n", column, monochrome, PAIR_NUMBER(cell), (unsigned long)(cell & A_CHARTEXT));
            assert(PAIR_NUMBER(cell) == COLOR_BLACK_ON_CYAN);
            assert(!!(cell & A_REVERSE) == monochrome);
        }
        assert(PAIR_NUMBER(mvwinch(win, 0, 0)) != COLOR_BLACK_ON_CYAN);
        werase(win);
        display_line(win, &line, 3, 5, 1, SYNTAX_C, 5, line.line_length);
        assert(PAIR_NUMBER(mvwinch(win, 0, 0)) == COLOR_BLACK_ON_CYAN);
    }
    color_enabled=1;
    delwin(win);
}

// Cover both directions, repeated block commands, UTF-8, edits, EOF, resize, and viewer isolation.
int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    check_ranges();
    FILE *output=tmpfile(), *input=tmpfile();
    SCREEN *screen=newterm("xterm", output, input);
    assert(screen);
    start_color();
    for (int pair=1; pair <= COLOR_RED_ON_BLUE; pair++) init_pair(pair, COLOR_WHITE, COLOR_BLUE);
    check_rendering();
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
    editing=0;
    check_editor("viewer footer and read-only", "abc\ndef", "abc\ndef", (Event[]){K(KEY_F(5)), K(KEY_F(6)), K(KEY_F(8)), T("x"), K(KEY_RESIZE), K(KEY_F(3)), K(-1)});
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Editor: 10000 range edits, allocation failures, selection rendering and 15 key sequences passed.");
    return 0;
}

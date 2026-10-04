// Run with: gcc -Os -ffunction-sections -fdata-sections tests/dialog.c ui.c init.c -Wl,--gc-sections -lncursesw -o /tmp/mc-dialog-test && /tmp/mc-dialog-test
#include "../includes.h"
#include <assert.h>
#define read_text_key test_read_key
#include "../dialog.c"
#undef read_text_key

int color_enabled=1;
extern SCREEN *screen;
typedef struct { int key, button, x, y; mmask_t mouse; const char *text; } Event;
static const Event *events;
static int event_index;
#define K(key) {key}
#define M(button, x, y, state) {KEY_MOUSE, button, x, y, state}
#define T(text) {0, 0, 0, 0, 0, text}
#define END K(-1)

// Locate the rendered brackets independently of the layout helper, then inject a click.
int test_read_key(WINDOW *win, char *text)
{
    Event event=events[event_index++];
    assert(event.key != -1);
    text[0]=0;
    if (event.text) strcpy(text, event.text);
    if (event.key != KEY_MOUSE) return event.key;
    int x=0, y=0, found=-1, cursor_x=getcurx(win), cursor_y=getcury(win);
    if (event.button >= 0)
    {
        for (y=0; y < getmaxy(win); y++)
        {
            for (x=0; x < getmaxx(win); x++)
                if ((mvwinch(win, y, x) & A_CHARTEXT) == '[' && ++found == event.button) break;
            if (found == event.button) break;
        }
        assert(found == event.button);
    }
    if (event.button == -2) { x=3; y=3; }
    wmove(win, cursor_y, cursor_x);
    MEVENT mouse={0, getbegx(win)+x+event.x, getbegy(win)+y+event.y, 0, event.mouse};
    assert(ungetmouse(&mouse) == OK);
    assert(wgetch(win) == KEY_MOUSE);
    return KEY_MOUSE;
}

// Run the actual modal loop with both combined clicks and separate press/release events.
static void check_dialog(char *buttons[], int vertical, int danger, char *prompt, int expected, const Event *input)
{
    events=input;
    event_index=0;
    int interval=mouseinterval(-1);
    mmask_t mask=mousemask(ALL_MOUSE_EVENTS|REPORT_MOUSE_POSITION, NULL), restored;
    assert(show_dialog("Confirm action", buttons, 0, prompt, danger, vertical, 0) == expected);
    assert(events[event_index].key == -1);
    mousemask(mask, &restored);
    assert(mask == restored && interval == mouseinterval(-1));
}

// Cover button edges, gaps, prompts, Unicode labels, both layouts, and keyboard fallback.
int main(void)
{
    setlocale(LC_CTYPE, "C.UTF-8");
    FILE *output=tmpfile(), *input=tmpfile();
    screen=newterm("xterm", output, input);
    assert(screen);
    resizeterm(40, 100);
    init_screen();
    char *buttons[]={"OK", "Cancel", NULL};
    char *vertical[]={"By name", "By size", "By date", NULL};
    char *unicode[]={"是", "Zrušit", "中文", NULL};
    for (int immediate=0; immediate < 2; immediate++)
    {
        mouseinterval(immediate ? 0 : 50);
        check_dialog(buttons, 0, 0, NULL, 1, (Event[]){M(0, 2, 0, BUTTON1_CLICKED), END});
        check_dialog(buttons, 0, 0, NULL, 2, (Event[]){M(1, 9, 0, BUTTON1_CLICKED), END});
        check_dialog(buttons, 0, 0, NULL, 2, (Event[]){M(1, 2, 0, BUTTON1_PRESSED), M(1, 3, 0, BUTTON1_RELEASED), END});
        check_dialog(buttons, 0, 0, NULL, 2, (Event[]){M(0, 2, 0, BUTTON1_PRESSED), M(1, 3, 0, BUTTON1_RELEASED), M(1, 2, 0, BUTTON1_CLICKED), END});
        check_dialog(buttons, 0, 0, NULL, -1, (Event[]){M(1, 2, 0, BUTTON1_RELEASED), K(27), END});
        check_dialog(buttons, 0, 0, NULL, 2, (Event[]){M(-1, -1, 0, BUTTON1_PRESSED), M(0, 2, 0, BUTTON1_RELEASED), M(1, 2, 0, BUTTON1_CLICKED), END});
        check_dialog(buttons, 0, 0, NULL, 1, (Event[]){M(0, 6, 0, BUTTON1_CLICKED), M(0, 2, -1, BUTTON1_CLICKED), M(-1, -1, 0, BUTTON1_CLICKED), M(0, 2, 0, REPORT_MOUSE_POSITION), M(0, 2, 0, BUTTON3_CLICKED), M(0, 5, 0, BUTTON1_CLICKED), END});
        check_dialog(buttons, 0, 1, NULL, 2, (Event[]){M(1, 2, 0, BUTTON1_PRESSED), M(1, 2, 0, BUTTON1_RELEASED), END});
        check_dialog(vertical, 1, 0, NULL, 2, (Event[]){M(1, 10, 0, BUTTON1_CLICKED), END});
        check_dialog(unicode, 0, 0, NULL, 3, (Event[]){M(2, 7, 0, BUTTON1_CLICKED), END});
        check_dialog(unicode, 1, 0, NULL, 2, (Event[]){M(1, 5, 0, BUTTON1_PRESSED), M(1, 6, 0, BUTTON1_RELEASED), END});
        char prompt[CMD_MAX]="original";
        check_dialog(buttons, 0, 0, prompt, 2, (Event[]){M(1, 2, 0, BUTTON1_CLICKED), END});
        assert(!strcmp(prompt, "original"));
        check_dialog(buttons, 0, 0, prompt, 1, (Event[]){T("Ž"), M(0, 2, 0, BUTTON1_PRESSED), M(0, 2, 0, BUTTON1_RELEASED), END});
        assert(!strcmp(prompt, "Ž"));
        check_dialog(buttons, 0, 0, NULL, 2, (Event[]){K(KEY_RIGHT), K('\n'), END});
        check_dialog(buttons, 0, 0, NULL, -1, (Event[]){K(KEY_F(10)), END});
        strcpy(prompt, "a中éž");
        check_dialog(buttons, 0, 0, prompt, 1, (Event[]){K('\t'), M(-2, 2, 0, BUTTON1_CLICKED), T("X"), K('\n'), END});
        assert(!strcmp(prompt, "aX中éž"));
        strcpy(prompt, "a中éž");
        check_dialog(buttons, 0, 0, prompt, 1, (Event[]){M(-2, 4, 0, BUTTON1_PRESSED), M(-2, 4, 0, BUTTON1_RELEASED), T("X"), K('\n'), END});
        assert(!strcmp(prompt, "a中éXž"));
        strcpy(prompt, "abc");
        check_dialog(buttons, 0, 0, prompt, 1, (Event[]){M(-2, 15, 0, BUTTON1_CLICKED), T("X"), K('\n'), END});
        assert(!strcmp(prompt, "abcX"));
        memset(prompt, 'a', 60); prompt[60]=0;
        check_dialog(buttons, 0, 0, prompt, 1, (Event[]){M(-2, 2, 0, BUTTON1_CLICKED), T("X"), K('\n'), END});
        assert(strlen(prompt) == 61 && prompt[34] == 'X');
    }
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Dialogs: 38 mouse/keyboard scenarios passed.");
    return 0;
}

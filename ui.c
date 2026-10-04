#include "includes.h"
#include "types.h"
#include "globals.h"

// Decode a character and its combining marks, keeping invalid bytes individually editable.
int text_cell(const char *text, int length, wchar_t *chars, int *width)
{
    mbstate_t state={0};
    size_t bytes=mbrtowc(chars, text, length, &state);
    if (bytes == (size_t)-1 || bytes == (size_t)-2)
    {
        chars[0]=L'\xfffd';
        bytes=1;
    }
    if (!bytes) bytes=1;
    *width=wcwidth(chars[0]);
    if (*width < 1) *width=1;
    int count=1;
    if (chars[0] && wcwidth(chars[0]) == 0)
    {
        chars[1]=chars[0];
        chars[0]=L'\x25cc';
        count=2;
    }
    // Combining marks do not attach to a tab.
    while (chars[0] != L'\t' && bytes < (size_t)length)
    {
        wchar_t next;
        state=(mbstate_t){0};
        size_t size=mbrtowc(&next, text+bytes, length-bytes, &state);
        if (!size || size == (size_t)-1 || size == (size_t)-2 || wcwidth(next) != 0) break;
        if (count < CCHARW_MAX-1) chars[count++]=next;
        bytes+=size;
    }
    chars[count]=L'\0';
    return bytes;
}

// Advance by one complete UTF-8 character, including its combining marks.
int text_next(const char *text, int length, int position)
{
    wchar_t chars[CCHARW_MAX];
    int width;
    return position+text_cell(text+position, length-position, chars, &width);
}

// Find the preceding display character without stopping inside a UTF-8 sequence.
int text_previous(const char *text, int position)
{
    int previous=0, width;
    wchar_t chars[CCHARW_MAX];
    for (int offset=0; offset < position;)
    {
        previous=offset;
        offset+=text_cell(text+offset, position-offset, chars, &width);
    }
    return previous;
}

// Convert a byte length to terminal columns, including double-width characters.
int text_column(const char *text, int length, int tab_width)
{
    int column=0, width;
    wchar_t chars[CCHARW_MAX];
    for (int offset=0; offset < length; column+=width)
    {
        offset+=text_cell(text+offset, length-offset, chars, &width);
        if (chars[0] == L'\t') width=tab_width;
    }
    return column;
}

// Snap a screen column to the beginning of a complete display character.
int text_offset(const char *text, int length, int column, int tab_width)
{
    int offset=0, current=0, width;
    wchar_t chars[CCHARW_MAX];
    while (offset < length)
    {
        int bytes=text_cell(text+offset, length-offset, chars, &width);
        if (chars[0] == L'\t') width=tab_width;
        if (current+width > column) break;
        offset+=bytes;
        current+=width;
    }
    return offset;
}

// Shorten by terminal columns, keeping multibyte characters and their accents intact.
void shorten(char *name, int width, char *result) {
    result[0]='\0';
    if (width <= 0) return;
    wchar_t text[CMD_MAX];
    mbstate_t state={0};
    size_t count=0;
    while (*name && count < CMD_MAX - 1) {
        size_t bytes=mbrtowc(&text[count], name, MB_CUR_MAX, &state);
        // Invalid filename bytes and control characters get a display-only placeholder.
        if (bytes == (size_t)-1 || bytes == (size_t)-2) {
            memset(&state, 0, sizeof(state));
            text[count]=L'?';
            bytes=1;
        }
        if (wcwidth(text[count]) < 0) text[count]=L'?';
        name+=bytes;
        count++;
    }
    text[count]=L'\0';
    if (wcswidth(text, count) > width) {
        size_t left=0, right=count;
        int left_width=0, right_width=0, half=(width - 1) / 2;
        while (left < count && left_width + wcwidth(text[left]) <= half)
            left_width+=wcwidth(text[left++]);
        while (right > left && right_width + wcwidth(text[right - 1]) <= width - 1 - half)
            right_width+=wcwidth(text[--right]);
        // Do not attach a suffix's orphaned combining marks to the truncation marker.
        while (right < count && wcwidth(text[right]) == 0) right++;
        memmove(text + left + 1, text + right, (count - right + 1) * sizeof(*text));
        text[left]=L'~';
    }
    wcstombs(result, text, CMD_MAX - 1);
    result[CMD_MAX - 1]='\0';
}

// Draw complete UTF-8 cells in a column viewport without wrapping or emitting controls.
void draw_text(WINDOW *win, int row, int x, const char *text, int start, int width)
{
    attr_t attributes;
    short color;
    wattr_get(win, &attributes, &color, NULL);
    int length=strlen(text), column=0;
    for (int offset=0; offset < length && column < start+width;)
    {
        wchar_t chars[CCHARW_MAX];
        int cell_width;
        offset+=text_cell(text+offset, length-offset, chars, &cell_width);
        if (!iswprint(chars[0])) chars[0]=L'.';
        if (column >= start && column+cell_width <= start+width)
        {
            cchar_t cell;
            setcchar(&cell, chars, attributes, color, NULL);
            mvwadd_wchnstr(win, row, x+column-start, &cell, 1);
        }
        column+=cell_width;
    }
}

// Keep Unicode input separate from ncurses key codes, which overlap numerically.
int read_text_key(WINDOW *win, char *text, MEVENT *mouse)
{
    wint_t input;
    text[0]='\0';
    // One KEY_MOUSE can announce several queued events; drain them before waiting.
    if (getmouse(mouse) == OK) return KEY_MOUSE;
    int result=wget_wch(win, &input);
    if (result == ERR) return ERR;
    if (result == KEY_CODE_YES && input == KEY_MOUSE) return getmouse(mouse) == OK ? KEY_MOUSE : ERR;
    if (result == KEY_CODE_YES) return noesc(input);
    if (input == 127 || input == 8) return KEY_BACKSPACE;
    if (input < 32) return input;
    if (!iswprint(input)) return ERR;
    mbstate_t state={0};
    size_t bytes=wcrtomb(text, input, &state);
    if (bytes == (size_t)-1) return ERR;
    text[bytes]='\0';
    return 0;
}

// Hide the cursor during changed output, then show it only at its final position.
void refresh_screen(int cursor_visibility)
{
    if (is_wintouched(newscr)) curs_set(0);
    doupdate();
    curs_set(cursor_visibility);
}

// Draw the inset frame shared by confirmation and progress dialogs.
void draw_dialog_frame(WINDOW *win, int separator)
{
    int width=getmaxx(win), height=getmaxy(win);
    mvwvline(win, 2, 1, '|', height-4);
    mvwvline(win, 2, width-2, '|', height-4);
    int rows[]={1, separator, height-2};
    for (int i=0; i < 3; i++)
    {
        mvwhline(win, rows[i], 2, '-', width-4);
        mvwaddch(win, rows[i], 1, '+');
        mvwaddch(win, rows[i], width-2, '+');
    }
}

// Draw newline-separated dialog text, shortening each line to the available columns.
void draw_dialog_text(WINDOW *win, int row, const char *text)
{
    char *copy=strdup(text), *line=strtok(copy, "\n");
    while (line)
    {
        mvwaddstr(win, row++, 3, SHORTEN(line, getmaxx(win)-6));
        line=strtok(NULL, "\n");
    }
    free(copy);
}

// Keep function keys in their usual slots and clip labels on narrow terminals.
const char *const panel_buttons[9]={"Sort", "View", "Edit", "Copy", "Move", "Mkdir", "Del", "Refresh", "Quit"};

void draw_buttons(int maxY, int maxX, const char *const labels[9]) {
    attrset(A_NORMAL);
    move(maxY - 1, 0);
    clrtoeol();
    for (int i=0; i < 9; i++) {
        int x=i*maxX/9, width=(i+1)*maxX/9-x-1;
        if (width <= 0) continue;
        char key[4];
        int length=snprintf(key, sizeof(key), "F%d", i+2);
        attrset(A_NORMAL);
        mvaddnstr(maxY-1, x, key, width);
        if (width <= length) continue;
        attrset(COLOR_PAIR(COLOR_BLACK_ON_CYAN));
        mvhline(maxY-1, x+length, ' ', width-length);
        mvaddnstr(maxY-1, x+length, labels[i], width-length);
    }
    attrset(A_NORMAL);
}

// Translate clicks on visible, active footer slots into their keyboard actions.
int button_key(MEVENT *event, const char *const labels[9], int *pressed)
{
    int hit=0, columns=getmaxx(stdscr), footer=event->y == getmaxy(stdscr)-1;
    for (int i=0; footer && i < 9; i++)
        if (labels[i][0] && event->x >= i*columns/9 && event->x < (i+1)*columns/9-1) hit=KEY_F(i+2);
    if ((event->bstate & BUTTON1_PRESSED) && !(event->bstate & REPORT_MOUSE_POSITION)) *pressed=hit ? hit : -1;
    int clicked=event->bstate & (BUTTON1_CLICKED|BUTTON1_DOUBLE_CLICKED|BUTTON1_TRIPLE_CLICKED);
    if (event->bstate & BUTTON1_RELEASED)
    {
        // With mouseinterval(0), ncurses can report a quick click as a release alone.
        clicked=*pressed == 0 || *pressed == hit;
        *pressed=0;
    }
    if (clicked && hit) { *pressed=0; return hit; }
    return footer ? ERR : KEY_MOUSE;
}

// Standard xterm profiles report clicks only; request motion while a button is held.
void mouse_tracking(int enabled)
{
    const char *prefix=tigetstr("kmous");
    if (prefix && (!strcmp(prefix, "\033[M") || !strcmp(prefix, "\033[<")))
    {
        fputs(enabled ? "\033[?1002h" : "\033[?1002l", stdout);
        fflush(stdout);
    }
}

void draw_windows(int maxY, int maxX) {
    // Stage the background before the panel windows.
    wnoutrefresh(stdscr);

    WINDOW **windows[]={&win1, &win2};
    for (int i=0; i < 2; i++) {
        int x=i*maxX/2, width=(i+1)*maxX/2-x;
        delwin(*windows[i]);
        *windows[i]=newwin(maxY-2, width, 0, x);
        wbkgd(*windows[i], COLOR_PAIR(COLOR_WHITE_ON_BLUE));
        wborder(*windows[i], '|', '|', '-', '-', '+', '+', '+', '+');
        wnoutrefresh(*windows[i]);
    }
}

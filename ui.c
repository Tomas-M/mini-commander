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
    while (bytes < (size_t)length)
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
int text_column(const char *text, int length)
{
    int column=0, width;
    wchar_t chars[CCHARW_MAX];
    for (int offset=0; offset < length; column+=width)
        offset+=text_cell(text+offset, length-offset, chars, &width);
    return column;
}

// Snap a screen column to the beginning of a complete display character.
int text_offset(const char *text, int length, int column)
{
    int offset=0, current=0, width;
    wchar_t chars[CCHARW_MAX];
    while (offset < length)
    {
        int bytes=text_cell(text+offset, length-offset, chars, &width);
        if (current+width > column) break;
        offset+=bytes;
        current+=width;
    }
    return offset;
}

// Keep Unicode input separate from ncurses key codes, which overlap numerically.
int read_text_key(WINDOW *win, char *text)
{
    wint_t input;
    text[0]='\0';
    int result=wget_wch(win, &input);
    if (result == ERR) return ERR;
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

void draw_buttons(int maxY, int maxX) {
    move(maxY - 1, 0);
    clrtoeol();

    char *buttons[] = {"Sort", "View", "Edit", "Copy", "Move", "Mkdir", "Del", "Refresh", "Quit"};
    int num_buttons = sizeof(buttons) / sizeof(char *);

    int total_width = maxX - (num_buttons - 1);  // Subtract (num_buttons - 1) to account for spaces between buttons
    int button_width = (total_width - 1) / num_buttons;  // -1 to account for the extra character in "F10"

    int extra_space = total_width - (button_width * num_buttons) - 1;  // -1 to account for the extra character in "F10"

    int x = 0;
    for (int i = 0; i < num_buttons; ++i) {
        int extra = 0;
        if (extra_space > 0) {
            extra = 1;
            extra_space--;
        }

        attrset(A_NORMAL);
        if (i == num_buttons - 1) {  // Last button (F10)
            mvprintw(maxY - 1, x, "F%d ", i + 2);
        } else {
            mvprintw(maxY - 1, x, "F%d", i + 2);
        }

        attron(COLOR_PAIR(COLOR_BLACK_ON_CYAN));
        mvprintw(maxY - 1, x + 2 + (i == num_buttons - 1), "%-*s", button_width - 2 + extra, buttons[i]);

        x += button_width + extra + 1 + (i == num_buttons - 1);  // +1 spacer between buttons, +1 for the last button (F10)
    }
}

void draw_windows(int maxY, int maxX) {
    // Refresh stdscr to ensure it's updated
    refresh();

    // Calculate window dimensions
    int winHeight = maxY - 2;
    int winWidth1 = maxX / 2;
    int winWidth2 = maxX / 2;

    // Adjust for odd COLS
    if (maxX % 2 != 0) {
        winWidth2 += 1;
    }

    // Delete old windows
    delwin(win1);
    delwin(win2);

    // Create new windows
    win1 = newwin(winHeight, winWidth1, 0, 0);
    win2 = newwin(winHeight, winWidth2, 0, winWidth1);

    // Apply the color pair to the window
    wbkgd(win1, COLOR_PAIR(COLOR_WHITE_ON_BLUE));
    wbkgd(win2, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    // Add borders to windows using wborder()
    wborder(win1, '|', '|', '-', '-', '+', '+', '+', '+');
    wborder(win2, '|', '|', '-', '-', '+', '+', '+', '+');

    // Refresh windows to make borders visible
    wrefresh(win1);
    wrefresh(win2);
}

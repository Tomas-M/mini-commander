#include "includes.h"
#include "types.h"
#include "globals.h"


char *find_newline(char *buffer, size_t length) {
    char *pos_r = memchr(buffer, '\r', length);
    char *pos_n = memchr(buffer, '\n', length);

    if (pos_r && pos_n) {
        if (pos_r + 1 == pos_n) {
            return pos_n; // if \r\n is encountered, break on the later
        }
        return pos_r < pos_n ? pos_r : pos_n;
    } else if (pos_r) {
        return pos_r;
    } else {
        return pos_n;
    }
}



// Complete partial writes and retry interrupted writes before reporting a failure.
static int write_all(int fd, const char *data, size_t length)
{
    while (length)
    {
        ssize_t written=write(fd, data, length);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0)
        {
            if (written == 0) errno=EIO;
            return -1;
        }
        data+=written;
        length-=written;
    }
    return 0;
}

// Save through symlinks and preserve ownership and permissions when replacing the target.
int write_file_lines(const char *filename, file_lines *lines)
{
    char *target=realpath(filename, NULL);
    if (!target) return -1;
    struct stat original;
    if (stat(target, &original) != 0)
    {
        free(target);
        return -1;
    }

    char temp_filename[strlen(target)+11];
    snprintf(temp_filename, sizeof(temp_filename), "%s.tmpXXXXXX", target);
    int fd=mkstemp(temp_filename);
    if (fd == -1)
    {
        free(target);
        return -1;
    }

    int error=0;
    for (file_lines *current=lines; current; current=current->next)
    {
        if (write_all(fd, current->line, current->line_length) != 0 ||
            (current->next && write_all(fd, "\n", 1) != 0))
        {
            error=errno;
            break;
        }
    }

    // Apply the mode after ownership and data changes, which can clear special mode bits.
    if (!error && fchown(fd, original.st_uid, original.st_gid) != 0) error=errno;
    if (!error && fchmod(fd, original.st_mode & 07777) != 0) error=errno;
    if (!error && fsync(fd) != 0) error=errno;
    if (close(fd) != 0 && !error) error=errno;
    if (!error && rename(temp_filename, target) != 0) error=errno;
    free(target);
    if (error)
    {
        unlink(temp_filename);
        errno=error;
        return -1;
    }
    return 0;
}



file_lines* read_file_lines(const char *filename, off_t *num_lines, off_t *num_bytes) {
    // Initialize linked list and counters
    file_lines *head = NULL, *tail = NULL;
    *num_lines = 0;

    // Open the file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    // Get the file size
    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        close(fd);
        return NULL;
    }

    *num_bytes = sb.st_size;

    // Handle empty file scenario separately
    if (sb.st_size == 0) {
        head = malloc(sizeof(file_lines));
        head->line = malloc(1);
        close(fd);
        head->line_length = 0;
        head->next = NULL;
        *num_lines = 1;
        return head;
    }


    // Memory map the file
    char *file_in_memory = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_in_memory == MAP_FAILED)
    {
        close(fd);
        return NULL;
    }

    char *line_start = file_in_memory;

    // Iterate through mapped memory

    // Iterate through mapped memory
    for (char *current = file_in_memory; current <= file_in_memory + sb.st_size; ++current) {
        // Check for end of file or newline character
        if (current == file_in_memory + sb.st_size || *current == '\n') {
            file_lines *new_node = malloc(sizeof(file_lines));

            // Allocate memory for the line data and copy it from the mapped memory
            int line_length = current - line_start;
            char *line_copy = malloc(line_length+1);
            memcpy(line_copy, line_start, line_length);

            // Fill in the new node
            new_node->line = line_copy;
            new_node->line_length = line_length;
            new_node->next = NULL;

            // Append to the linked list
            if (!head) {
                head = new_node;
            } else {
                tail->next = new_node;
            }
            tail = new_node;

            // Prepare for next block
            line_start = current + 1;
            (*num_lines)++;
        }
    }

    // Clean up
    munmap(file_in_memory, sb.st_size);
    close(fd);

    return head;
}


void free_file_lines(file_lines *head) {
    while (head != NULL) {
        file_lines *temp = head;
        head = head->next;
        free(temp->line);
        free(temp);
    }
}

// Match the Unicode word boundary used by the former keyword expressions.
static int highlight_word(const char *text)
{
    wchar_t ch;
    mbstate_t state={0};
    size_t bytes=mbrtowc(&ch, text, strnlen(text, MB_CUR_MAX), &state);
    return bytes > 0 && bytes <= MB_CUR_MAX && (ch == L'_' || iswalnum(ch));
}

// Find a fixed keyword, rejecting suffixes such as "integer" or "int中文".
static int highlight_keyword(const char *text, const char *words)
{
    while (*words)
    {
        int length=strcspn(words, " ");
        if (!strncmp(text, words, length) && !highlight_word(text+length)) return length;
        words+=length;
        if (*words) words++;
    }
    return 0;
}

// Recognize the existing C/shell rules in priority order, including greedy spans.
static int highlight_token(const char *text, int syntax, int *attributes)
{
    int length=0, color=COLOR_WHITE_ON_BLUE, bold=0;
    const char *end;
    if (*text && (syntax & SYNTAX_C))
    {
        if (*text == '"' && (end=strrchr(text+1, '"')))
            { length=end-text+1; color=COLOR_GREEN_ON_BLUE; }
        else if (!strncmp(text, "#include", 8) || !strncmp(text, "#define", 7))
            { length=strlen(text); color=COLOR_RED_ON_BLUE; bold=A_BOLD; }
        else if (!strncmp(text, "//", 2))
            { length=strlen(text); color=COLOR_YELLOW_ON_BLUE; }
        // The former musl expression also accepts buffer ends as word boundaries.
        else if (!strncmp(text, "...", 3) && (!text[3] || highlight_word(text+3)))
            { length=3; color=COLOR_YELLOW_ON_BLUE; bold=A_BOLD; }
        else if ((length=highlight_keyword(text, "auto break case char const continue default do double else enum extern float for goto if int long register return short signed sizeof static struct switch typedef union unsigned void volatile while asm inline wchar_t")))
            { color=COLOR_YELLOW_ON_BLUE; bold=A_BOLD; }
        else if (strchr("!%=*+-></", *text) || !strncmp(text, "&&", 2) || !strncmp(text, "||", 2))
        {
            length=1; color=COLOR_YELLOW_ON_BLUE; bold=A_BOLD;
            const char *pairs="==!=&&->||";
            for (int i=0; pairs[i]; i+=2)
                if (text[0] == pairs[i] && text[1] == pairs[i+1]) length=2;
        }
        else if (strchr("(){},:?[]", *text))
            { length=1; color=COLOR_CYAN_ON_BLUE; }
        else if (strchr(";&^~|", *text))
            { length=1; color=COLOR_MAGENTA_ON_BLUE; bold=A_BOLD; }
    }
    if (*text && !length && (syntax & SYNTAX_SHELL))
    {
        if (!strncmp(text, "#!/", 3))
            { length=strlen(text); color=COLOR_CYAN_ON_BLACK; }
        else if (*text == '#')
            { length=strlen(text); color=COLOR_YELLOW_ON_BLUE; }
        else if (strchr(";{}", *text))
            { length=1; color=COLOR_CYAN_ON_BLUE; bold=A_BOLD; }
        else if (*text == '$')
        {
            length=1; color=COLOR_GREEN_ON_BLUE; bold=A_BOLD;
            if ((text[1] == '(' || text[1] == '{') && (end=strrchr(text+2, text[1] == '(' ? ')' : '}')))
                { length=end-text+1; bold=0; }
            else if (text[1] && strchr("*@#?-$_!0123456789", text[1]))
                { length=2; color=COLOR_RED_ON_BLUE; }
            else length+=strspn(text+1, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
        }
        else if ((text[0] == '1' || text[0] == '2') && text[1] == '>')
        {
            length=2; color=COLOR_RED_ON_BLUE; bold=A_BOLD;
            if (text[2] == '&' && text[3] == (text[0] == '1' ? '2' : '1')) length=4;
        }
        else
        {
            if (!strncmp(text, "function", 8) && !highlight_word(text+8))
                for (const char *next=text+8; (next=strstr(next, "()")); next+=2) length=next-text+2;
            int name=strspn(text, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
            if (!length && name && !strncmp(text+name, "()", 2)) length=name+2;
            if (length) { color=COLOR_MAGENTA_ON_BLUE; bold=A_BOLD; }
            else if ((length=highlight_keyword(text, "break case clear continue declare done do echo elif else esac exit export fi for getopts if in read return select set shift source then trap until unset wait while")))
                { color=COLOR_YELLOW_ON_BLUE; bold=A_BOLD; }
        }
    }
    *attributes=COLOR_PAIR(color)|bold;
    return length;
}

// Render complete UTF-8 cells and clip by columns without wrapping into the next row.
void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode, int syntax)
{
    int row=getcury(win), column=0, match_end=0, attributes=COLOR_PAIR(COLOR_WHITE_ON_BLUE);
    char *text=malloc(line->line_length+1);
    memcpy(text, line->line, line->line_length);
    text[line->line_length]='\0';
    for (int offset=0; offset < line->line_length && column < current_col+max_x;)
    {
        wchar_t chars[CCHARW_MAX];
        int width, bytes=text_cell(text+offset, line->line_length-offset, chars, &width);
        if (offset >= match_end)
            match_end=offset+highlight_token(text+offset, syntax, &attributes);
        if (column >= current_col && column+width <= current_col+max_x)
        {
            int cell_attributes=attributes;
            if (chars[0] == L'\t')
            {
                chars[0]=L'>';
                cell_attributes=COLOR_PAIR(COLOR_CYAN_ON_BLUE);
            }
            else if (!iswprint(chars[0]))
            {
                chars[0]=editor_mode && chars[0] < 32 ? L'@'+chars[0] : L'.';
                if (editor_mode) cell_attributes=COLOR_PAIR(COLOR_WHITE_ON_RED);
            }
            cchar_t cell;
            setcchar(&cell, chars, cell_attributes & ~A_COLOR, PAIR_NUMBER(cell_attributes), NULL);
            mvwadd_wchnstr(win, row, column-current_col, &cell, 1);
        }
        column+=width;
        offset+=bytes;
    }
    free(text);
}


// Compare decoded characters so case-insensitive search also works with non-ASCII text.
static int text_matches(const char *text, int length, const char *pattern)
{
    int remaining=strlen(pattern);
    while (remaining)
    {
        wchar_t left, right;
        mbstate_t left_state={0}, right_state={0};
        size_t left_bytes=mbrtowc(&left, text, length, &left_state);
        size_t right_bytes=mbrtowc(&right, pattern, remaining, &right_state);
        if (!left_bytes || left_bytes > (size_t)length || right_bytes > (size_t)remaining) return 0;
        if (towlower(left) != towlower(right)) return 0;
        text+=left_bytes;
        length-=left_bytes;
        pattern+=right_bytes;
        remaining-=right_bytes;
    }
    return 1;
}

int view_edit_file(char *filename, int editor_mode) {
    int max_y, max_x;
    int screen_start_line = 0;
    int screen_start_col = 0;
    int cursor_row = 0;
    int is_modified = 0;
    int syntax=0;
    char find_str[CMD_MAX] = {0};

    // Get the screen dimensions
    getmaxyx(stdscr, max_y, max_x);

    // Top line on screen
    WINDOW *toprow_win = newwin(1, max_x, 0, 0);
    wbkgd(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));
    wattron(toprow_win, COLOR_PAIR(COLOR_BLACK_ON_CYAN));

    // Create a new window for displaying the file content
    WINDOW *content_win = newwin(max_y - 2, max_x, 1, 0);
    keypad(content_win, TRUE);

    werase(content_win); // Clear the window
    wbkgd(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE)); // Set the background color
    wattron(content_win, COLOR_PAIR(COLOR_WHITE_ON_BLUE));

    // Build the linked list of line pointers
    off_t num_lines, num_bytes;
    file_lines *lines = read_file_lines(filename, &num_lines, &num_bytes);
    if (!lines)
    {
        delwin(toprow_win);
        delwin(content_win);
        show_errormsg(SPRINTF("Cannot open file:\n%s\n%s", filename, strerror(errno)));
        return -1;
    }

    // Extract file extension
    char *file_type = strrchr(filename, '.');  // find last '.' in filename

    if (editor_mode)
    {
        if (file_type && (!strcmp(file_type, ".c") || !strcmp(file_type, ".h"))) syntax|=SYNTAX_C;
        if ((file_type && !strcmp(file_type, ".sh")) || (lines->line_length > 3 && !strncmp(lines->line, "#!/", 3))) syntax|=SYNTAX_SHELL;
    }

    // Byte positions identify edits; terminal columns are derived only for display/navigation.
    int cursor_byte=0;
    while (1)
    {
        file_lines *current_line=lines;
        off_t seek=0;
        for (int i=0; i < cursor_row; i++)
        {
            seek+=current_line->line_length+1;
            current_line=current_line->next;
        }
        int column=text_column(current_line->line, cursor_byte);
        // Insertions and line joins can attach existing combining marks to the preceding character.
        cursor_byte=text_offset(current_line->line, current_line->line_length, column);
        if (editor_mode)
        {
            if (cursor_row < screen_start_line) screen_start_line=cursor_row;
            if (cursor_row >= screen_start_line+max_y-2) screen_start_line=cursor_row-max_y+3;
            if (column < screen_start_col) screen_start_col=column;
            wchar_t chars[CCHARW_MAX];
            int cursor_width=1;
            if (cursor_byte < current_line->line_length)
                text_cell(current_line->line+cursor_byte, current_line->line_length-cursor_byte, chars, &cursor_width);
            if (column+cursor_width > screen_start_col+max_x) screen_start_col=column+cursor_width-max_x;
        }

        werase(content_win);
        file_lines *shown=lines;
        for (int i=0; i < screen_start_line; i++) shown=shown->next;
        int shown_rows=0;
        for (; shown && shown_rows < max_y-2; shown_rows++, shown=shown->next)
        {
            wmove(content_win, shown_rows, 0);
            display_line(content_win, shown, max_x, screen_start_col, editor_mode, syntax);
        }
        werase(toprow_win);
        if (editor_mode)
        {
            char charcode[16];
            if (cursor_byte < current_line->line_length)
            {
                wchar_t chars[CCHARW_MAX];
                int width;
                text_cell(current_line->line+cursor_byte, current_line->line_length-cursor_byte, chars, &width);
                snprintf(charcode, sizeof(charcode), "U+%04X", (unsigned int)chars[0]);
            }
            else snprintf(charcode, sizeof(charcode), "%s", current_line->next ? "#10" : "<EOF>");
            mvwprintw(toprow_win, 0, 0, "%s   [-%s--] %3d L:[%3d+%3d %3d/%3lld] *(%4lld/%lldb)   %s",
                filename, is_modified ? "M" : "-", column, screen_start_line+1, cursor_row-screen_start_line,
                cursor_row+1, (long long)num_lines, (long long)(seek+cursor_byte), (long long)num_bytes, charcode);
        }
        else
        {
            int last=screen_start_line+shown_rows;
            mvwprintw(toprow_win, 0, 0, "%s", filename);
            int width=snprintf(NULL, 0, "        %d/%lld   %lld%%", last, (long long)num_lines, (long long)(100*last/num_lines));
            mvwprintw(toprow_win, 0, max_x-width, "        %d/%lld   %lld%%", last, (long long)num_lines, (long long)(100*last/num_lines));
        }
        wnoutrefresh(toprow_win);
        if (editor_mode) wmove(content_win, cursor_row-screen_start_line, column-screen_start_col);
        wnoutrefresh(content_win);
        refresh_screen(editor_mode);

        char input_text[MB_LEN_MAX+1];
        int input=read_text_key(content_win, input_text);
        int target_column=-1;
        switch (input)
        {
            case KEY_F(3):
                if (editor_mode) break;
                goto close_editor;
            case KEY_F(10):
            case 27:
                if (is_modified)
                {
                    int button=show_dialog(SPRINTF("File %s was modified.\nSave before close?", filename), (char *[]) {"Yes", "No", "Cancel", NULL}, 2, NULL, 0, 0, 0);
                    if (button != 1 && button != 2) break;
                    if (button == 1 && write_file_lines(filename, lines) != 0)
                    {
                        show_errormsg(SPRINTF("Cannot save file:\n%s\n%s", filename, strerror(errno)));
                        break;
                    }
                }
                goto close_editor;
            case KEY_F(2):
                if (editor_mode && show_dialog(SPRINTF("Confirm save file:\n%s", filename), (char *[]) {"Save", "Cancel", NULL}, 0, NULL, 0, 0, 0) == 1)
                {
                    if (write_file_lines(filename, lines) != 0)
                        show_errormsg(SPRINTF("Cannot save file:\n%s\n%s", filename, strerror(errno)));
                    else is_modified=0;
                }
                break;
            case KEY_F(7):
            case KEY_SHIFT_F7:
            {
                if ((!find_str[0] || input == KEY_F(7)) && show_dialog("Enter search string:", (char *[]) {"Find", "Cancel", NULL}, 0, find_str, 0, 0, 0) != 1) break;
                if (!find_str[0]) break;
                file_lines *search_line=current_line;
                int found_row=cursor_row, offset=cursor_byte, found=-1;
                wchar_t chars[CCHARW_MAX];
                int width;
                if (offset < search_line->line_length)
                    offset+=text_cell(search_line->line+offset, search_line->line_length-offset, chars, &width);
                else offset++;
                while (search_line)
                {
                    for (; offset < search_line->line_length; offset+=text_cell(search_line->line+offset, search_line->line_length-offset, chars, &width))
                        if (text_matches(search_line->line+offset, search_line->line_length-offset, find_str))
                        {
                            found=offset;
                            break;
                        }
                    if (found >= 0) break;
                    search_line=search_line->next;
                    found_row++;
                    offset=0;
                }
                if (found < 0) show_dialog("Search string not found", (char *[]) {"OK", NULL}, 0, NULL, 0, 0, 0);
                else
                {
                    cursor_row=found_row;
                    cursor_byte=found;
                    screen_start_line=found_row;
                    screen_start_col=text_column(search_line->line, found);
                }
                break;
            }
            case KEY_UP:
            case KEY_DOWN:
            case KEY_PPAGE:
            case KEY_NPAGE:
            {
                int step=(input == KEY_PPAGE || input == KEY_NPAGE) ? max_y-2 : 1;
                if (input == KEY_UP || input == KEY_PPAGE) step=-step;
                if (editor_mode)
                {
                    cursor_row+=step;
                    if (cursor_row < 0) cursor_row=0;
                    if (cursor_row >= num_lines) cursor_row=num_lines-1;
                    target_column=column;
                }
                else
                {
                    screen_start_line+=step;
                    int last=num_lines > max_y-2 ? num_lines-max_y+2 : 0;
                    if (screen_start_line > last) screen_start_line=last;
                    if (screen_start_line < 0) screen_start_line=0;
                    cursor_row=screen_start_line;
                    cursor_byte=0;
                }
                break;
            }
            case KEY_LEFT:
                if (!editor_mode)
                {
                    screen_start_col=screen_start_col > 10 ? screen_start_col-10 : 0;
                    break;
                }
                if (cursor_byte) cursor_byte=text_previous(current_line->line, cursor_byte);
                else if (cursor_row > 0)
                {
                    cursor_row--;
                    target_column=INT_MAX;
                }
                break;
            case KEY_RIGHT:
                if (!editor_mode) screen_start_col+=10;
                else if (cursor_byte < current_line->line_length)
                {
                    wchar_t chars[CCHARW_MAX];
                    int width;
                    cursor_byte+=text_cell(current_line->line+cursor_byte, current_line->line_length-cursor_byte, chars, &width);
                }
                else if (current_line->next)
                {
                    cursor_row++;
                    cursor_byte=0;
                }
                break;
            case KEY_HOME:
                if (editor_mode) cursor_byte=0;
                else screen_start_line=cursor_row=cursor_byte=0;
                break;
            case KEY_END:
                if (editor_mode) cursor_byte=current_line->line_length;
                else
                {
                    screen_start_line=num_lines > max_y-2 ? num_lines-max_y+2 : 0;
                    cursor_row=screen_start_line;
                    cursor_byte=0;
                }
                break;
            case KEY_RESIZE:
                getmaxyx(stdscr, max_y, max_x);
                wresize(toprow_win, 1, max_x);
                wresize(content_win, max_y-2, max_x);
                break;
            case '\n':
            case '\r':
                if (editor_mode)
                {
                    file_lines *next=malloc(sizeof(*next));
                    next->line_length=current_line->line_length-cursor_byte;
                    next->line=malloc(next->line_length+1);
                    memcpy(next->line, current_line->line+cursor_byte, next->line_length);
                    next->next=current_line->next;
                    current_line->next=next;
                    current_line->line_length=cursor_byte;
                    cursor_row++;
                    cursor_byte=0;
                    num_lines++;
                    num_bytes++;
                    is_modified=1;
                }
                break;
            case KEY_BACKSPACE:
                if (!editor_mode) break;
                if (cursor_byte > 0)
                {
                    int previous=text_previous(current_line->line, cursor_byte);
                    memmove(current_line->line+previous, current_line->line+cursor_byte, current_line->line_length-cursor_byte);
                    current_line->line_length-=cursor_byte-previous;
                    num_bytes-=cursor_byte-previous;
                    cursor_byte=previous;
                    is_modified=1;
                }
                else if (cursor_row > 0)
                {
                    file_lines *previous=lines;
                    for (int i=0; i < cursor_row-1; i++) previous=previous->next;
                    cursor_byte=previous->line_length;
                    previous->line=realloc(previous->line, cursor_byte+current_line->line_length+1);
                    memcpy(previous->line+cursor_byte, current_line->line, current_line->line_length);
                    previous->line_length+=current_line->line_length;
                    previous->next=current_line->next;
                    free(current_line->line);
                    free(current_line);
                    cursor_row--;
                    num_lines--;
                    num_bytes--;
                    is_modified=1;
                }
                break;
            case KEY_DC:
                if (!editor_mode) break;
                if (cursor_byte < current_line->line_length)
                {
                    wchar_t chars[CCHARW_MAX];
                    int width, bytes=text_cell(current_line->line+cursor_byte, current_line->line_length-cursor_byte, chars, &width);
                    memmove(current_line->line+cursor_byte, current_line->line+cursor_byte+bytes, current_line->line_length-cursor_byte-bytes);
                    current_line->line_length-=bytes;
                    num_bytes-=bytes;
                    is_modified=1;
                }
                else if (current_line->next)
                {
                    file_lines *next=current_line->next;
                    current_line->line=realloc(current_line->line, current_line->line_length+next->line_length+1);
                    memcpy(current_line->line+current_line->line_length, next->line, next->line_length);
                    current_line->line_length+=next->line_length;
                    current_line->next=next->next;
                    free(next->line);
                    free(next);
                    num_lines--;
                    num_bytes--;
                    is_modified=1;
                }
                break;
            case KEY_MOUSE:
            {
                MEVENT event;
                if (!editor_mode || getmouse(&event) != OK) break;
                if (event.y < 1 || event.y >= max_y-1 || event.x < 0 || event.x >= max_x) break;
                cursor_row=screen_start_line+event.y-1;
                if (cursor_row >= num_lines) cursor_row=num_lines-1;
                target_column=screen_start_col+event.x;
                break;
            }
            default:
                if (input == '\t') strcpy(input_text, "\t");
                if (editor_mode && input_text[0])
                {
                    int bytes=strlen(input_text);
                    current_line->line=realloc(current_line->line, current_line->line_length+bytes+1);
                    memmove(current_line->line+cursor_byte+bytes, current_line->line+cursor_byte, current_line->line_length-cursor_byte);
                    memcpy(current_line->line+cursor_byte, input_text, bytes);
                    current_line->line_length+=bytes;
                    cursor_byte+=bytes;
                    num_bytes+=bytes;
                    is_modified=1;
                }
                break;
        }
        if (target_column >= 0)
        {
            current_line=lines;
            for (int i=0; i < cursor_row; i++) current_line=current_line->next;
            cursor_byte=text_offset(current_line->line, current_line->line_length, target_column);
        }
    }

close_editor:
    delwin(toprow_win);
    delwin(content_win);
    free_file_lines(lines);
    return 0;
}

int view_file(char *filename) {
    return view_edit_file(filename, 0);
}

int edit_file(char *filename) {
    return view_edit_file(filename, 1);
}

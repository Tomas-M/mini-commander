#include "includes.h"
#include "types.h"
#include "globals.h"

static const char tab_marker[]="<--->";

typedef struct {
    int row, byte, top, left;
    off_t mark_start, mark_end;
    size_t revision;
} editor_state;

typedef struct undo_entry {
    struct undo_entry *next;
    editor_state state;
    off_t start;
    size_t removed, inserted;
    char text[];
} undo_entry;

static void copy_text_range(file_lines *lines, off_t start, off_t end, char *text);

// Movement costs only a state record; edits additionally retain the replaced bytes.
static undo_entry *remember_edit(file_lines *lines, editor_state state, off_t start, off_t end, size_t inserted)
{
    size_t removed=end-start;
    if (removed > SIZE_MAX-sizeof(undo_entry)) { errno=ENOMEM; return NULL; }
    undo_entry *entry=malloc(sizeof(*entry)+removed);
    if (!entry) return NULL;
    *entry=(undo_entry){.state=state, .start=start, .removed=removed, .inserted=inserted};
    if (removed) copy_text_range(lines, start, end, entry->text);
    return entry;
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



// Release a complete list or a partially constructed edit.
void free_file_lines(file_lines *head) {
    while (head) {
        file_lines *next=head->next;
        free(head->line);
        free(head);
        head=next;
    }
}

// Split exact bytes into terminated lines, preserving empty lines and embedded NULs.
static file_lines *split_file_lines(const char *data, size_t length, off_t *count, file_lines **tail)
{
    file_lines *head=NULL;
    *tail=NULL;
    *count=0;
    for (size_t offset=0;;)
    {
        const char *newline=memchr(data+offset, '\n', length-offset);
        size_t bytes=newline ? (size_t)(newline-data)-offset : length-offset;
        if (bytes > INT_MAX) { errno=EFBIG; break; }
        file_lines *next=malloc(sizeof(*next));
        if (!next) break;
        next->line=malloc(bytes+1);
        if (!next->line) { free(next); break; }
        memcpy(next->line, data+offset, bytes);
        next->line[bytes]='\0';
        next->line_length=bytes;
        next->next=NULL;
        if (*tail) (*tail)->next=next;
        else head=next;
        *tail=next;
        (*count)++;
        if (!newline) return head;
        offset+=bytes+1;
    }
    free_file_lines(head);
    return NULL;
}

// Use the same line construction for initial loading and later range replacements.
file_lines *read_file_lines(const char *filename, off_t *num_lines, off_t *num_bytes)
{
    *num_lines=0;
    int fd=open(filename, O_RDONLY);
    if (fd < 0) return NULL;
    struct stat statbuf;
    if (fstat(fd, &statbuf) != 0) { close(fd); return NULL; }
    *num_bytes=statbuf.st_size;
    char *data=statbuf.st_size ? mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0) : "";
    if (data == MAP_FAILED) { close(fd); return NULL; }
    file_lines *tail, *head=split_file_lines(data, statbuf.st_size, num_lines, &tail);
    int error=errno;
    if (statbuf.st_size) munmap(data, statbuf.st_size);
    close(fd);
    errno=error;
    return head;
}

// Resolve a row and, when requested, its absolute byte offset.
static file_lines *line_at_row(file_lines *line, int row, off_t *position)
{
    if (position) *position=0;
    for (int i=0; i < row; i++, line=line->next)
        if (position) *position+=line->line_length+1;
    return line;
}

// Resolve a file byte position, treating each line separator as one byte.
static file_lines *line_at_position(file_lines *line, off_t *position, int *row)
{
    if (row) *row=0;
    while (line->next && *position > line->line_length)
    {
        *position-=line->line_length+1;
        line=line->next;
        if (row) (*row)++;
    }
    return line;
}

// Copy a half-open byte range, including embedded NULs and line separators.
static void copy_text_range(file_lines *lines, off_t start, off_t end, char *text)
{
    off_t length=end-start;
    file_lines *line=line_at_position(lines, &start, NULL);
    while (length)
    {
        int bytes=line->line_length-start;
        if (bytes > length) bytes=length;
        memcpy(text, line->line+start, bytes);
        text+=bytes;
        length-=bytes;
        if (!length) break;
        *text++='\n';
        length--;
        line=line->next;
        start=0;
    }
}

// Build replacement lines before changing the file, so allocation failure loses no data.
static int replace_text_range(file_lines *lines, off_t start, off_t end, const char *text, size_t length, off_t *num_lines)
{
    file_lines *first=line_at_position(lines, &start, NULL);
    file_lines *last=line_at_position(lines, &end, NULL);
    size_t total=start+length+last->line_length-end;
    char *data=malloc(total+1);
    if (!data) return -1;
    memcpy(data, first->line, start);
    if (length) memcpy(data+start, text, length);
    memcpy(data+start+length, last->line+end, last->line_length-end);

    file_lines *tail;
    off_t added;
    file_lines *head=split_file_lines(data, total, &added, &tail);
    if (!head) { free(data); return -1; }
    file_lines *after=last->next, *old=first->next;
    free(first->line);
    (*num_lines)--;
    while (old != after)
    {
        file_lines *next=old->next;
        free(old->line);
        free(old);
        old=next;
        (*num_lines)--;
    }
    tail->next=after;
    *first=*head;
    free(head);
    free(data);
    *num_lines+=added;
    return 0;
}

// Keep a mark attached to its original text when surrounding bytes are edited.
static off_t move_mark(off_t mark, off_t start, off_t end, size_t length, int right_affinity)
{
    if (mark < start) return mark;
    if (mark > end) return mark+length-(end-start);
    return start+(right_affinity ? length : 0);
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
void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode, int syntax, off_t mark_start, off_t mark_end)
{
    int row=getcury(win), column=0, match_end=0, attributes=COLOR_PAIR(COLOR_WHITE_ON_BLUE);
    int tab_width=sizeof(tab_marker)-1;
    const char *text=line->line;
    for (int offset=0; offset < line->line_length && column < current_col+max_x;)
    {
        wchar_t chars[CCHARW_MAX];
        int width, bytes=text_cell(text+offset, line->line_length-offset, chars, &width);
        int is_tab=chars[0] == L'\t';
        if (is_tab) width=tab_width;
        if (offset >= match_end)
            match_end=offset+highlight_token(text+offset, syntax, &attributes);
        int cell_attributes=attributes;
        if (is_tab) cell_attributes=COLOR_PAIR(COLOR_CYAN_ON_BLUE);
        else if (!iswprint(chars[0]))
        {
            chars[0]=editor_mode && chars[0] < 32 ? L'@'+chars[0] : L'.';
            if (editor_mode) cell_attributes=COLOR_PAIR(COLOR_WHITE_ON_RED);
        }
        if (offset < mark_end && offset+bytes > mark_start)
            cell_attributes=COLOR_PAIR(COLOR_BLACK_ON_CYAN);
        // Clip each marker column while keeping the tab a single editable byte.
        for (int part=0; part < (is_tab ? tab_width : 1); part++)
        {
            int x=column+part-current_col, cell_width=is_tab ? 1 : width;
            if (x < 0 || x+cell_width > max_x) continue;
            if (is_tab) chars[0]=editor_mode ? tab_marker[part] : L' ';
            cchar_t cell;
            setcchar(&cell, chars, cell_attributes & ~A_COLOR, PAIR_NUMBER(cell_attributes), NULL);
            mvwadd_wchnstr(win, row, x, &cell, 1);
        }
        column+=width;
        offset+=bytes;
    }
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
    int tab_width=sizeof(tab_marker)-1;
    off_t mark_start=-1, mark_end=-1;
    off_t drag_start=-1;
    mmask_t saved_mousemask=0;
    int saved_mouseinterval=0, pressed_button=0;
    char find_str[CMD_MAX] = {0};
    const char *editor_buttons[]={"Save", "Mark", "", "Copy", "Move", "Search", "Delete", "", "Quit"};
    const char *viewer_buttons[]={"", "Quit", "", "", "", "Search", "", "", "Quit"};
    const char **buttons=editor_mode ? editor_buttons : viewer_buttons;

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
        // Receive press, drag and release separately; restore panel mouse behavior on exit.
        mousemask(BUTTON1_PRESSED|BUTTON1_RELEASED|BUTTON1_CLICKED|REPORT_MOUSE_POSITION, &saved_mousemask);
        saved_mouseinterval=mouseinterval(0);
        mouse_tracking(1);
    }

    // Byte positions identify edits; terminal columns are derived only for display/navigation.
    int cursor_byte=0, input=ERR;
    undo_entry *undo=NULL;
    size_t revision=0, next_revision=0, saved_revision=0;
    while (1)
    {
        off_t seek;
        file_lines *current_line=line_at_row(lines, cursor_row, &seek);
        int column=text_column(current_line->line, cursor_byte, tab_width);
        // Insertions and line joins can attach existing combining marks to the preceding character.
        cursor_byte=text_offset(current_line->line, current_line->line_length, column, tab_width);
        off_t position=seek+cursor_byte, selected_start=mark_start;
        off_t selected_end=mark_end >= 0 ? mark_end : position;
        if (selected_start >= 0 && selected_start > selected_end)
        {
            off_t swap=selected_start;
            selected_start=selected_end;
            selected_end=swap;
        }
        if (mark_start < 0) selected_end=-1;
        if (editor_mode && input != KEY_MOUSE && input != 21)
        {
            if (cursor_row < screen_start_line) screen_start_line=cursor_row;
            if (cursor_row >= screen_start_line+max_y-2) screen_start_line=cursor_row-max_y+3;
            if (column < screen_start_col) screen_start_col=column;
            wchar_t chars[CCHARW_MAX];
            int cursor_width=1;
            if (cursor_byte < current_line->line_length)
            {
                text_cell(current_line->line+cursor_byte, current_line->line_length-cursor_byte, chars, &cursor_width);
                if (chars[0] == L'\t') cursor_width=tab_width;
                if (cursor_width > max_x) cursor_width=max_x;
            }
            if (column+cursor_width > screen_start_col+max_x) screen_start_col=column+cursor_width-max_x;
        }

        draw_buttons(max_y, max_x, buttons);
        wnoutrefresh(stdscr);
        werase(content_win);
        off_t shown_position;
        file_lines *shown=line_at_row(lines, screen_start_line, &shown_position);
        int shown_rows=0;
        for (; shown && shown_rows < max_y-2; shown_rows++, shown=shown->next)
        {
            wmove(content_win, shown_rows, 0);
            display_line(content_win, shown, max_x, screen_start_col, editor_mode, syntax, selected_start-shown_position, selected_end-shown_position);
            shown_position+=shown->line_length+1;
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
            mvwprintw(toprow_win, 0, 0, "%s   [%c%s--] %3d L:[%3d+%3d %3d/%3lld] *(%4lld/%lldb)   %s",
                filename, mark_start >= 0 ? 'B' : '-', is_modified ? "M" : "-", column, screen_start_line+1, cursor_row-screen_start_line,
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
        if (editor_mode)
        {
            // A click can snap to the start of a character clipped by the viewport.
            int x=column-screen_start_col;
            if (x < 0) x=0;
            if (x >= max_x) x=max_x-1;
            wmove(content_win, cursor_row-screen_start_line, x);
        }
        wnoutrefresh(content_win);
        refresh_screen(editor_mode);

        char input_text[MB_LEN_MAX+1];
        MEVENT event;
        input=read_text_key(content_win, input_text, &event);
        if (input == KEY_MOUSE)
        {
            int key=button_key(&event, buttons, &pressed_button);
            if (drag_start < 0) input=key;
        }
        else pressed_button=0;
        editor_state before={cursor_row, cursor_byte, screen_start_line, screen_start_col, mark_start, mark_end, revision};
        if (input == 21 && editor_mode) // Ctrl+U also restores movement and viewport changes.
        {
            if (!undo) continue;
            if ((undo->removed || undo->inserted) &&
                replace_text_range(lines, undo->start, undo->start+undo->inserted, undo->text, undo->removed, &num_lines) != 0)
            { show_errormsg(SPRINTF("Cannot undo:\n%s", strerror(errno))); continue; }
            num_bytes+=(off_t)undo->removed-(off_t)undo->inserted;
            cursor_row=undo->state.row;
            cursor_byte=undo->state.byte;
            screen_start_line=undo->state.top;
            screen_start_col=undo->state.left;
            mark_start=undo->state.mark_start;
            mark_end=undo->state.mark_end;
            revision=undo->state.revision;
            is_modified=revision != saved_revision;
            undo_entry *previous=undo->next;
            free(undo);
            undo=previous;
            drag_start=-1;
            continue;
        }
        if (input != KEY_MOUSE) drag_start=-1;
        int target_column=-1;
        off_t edit_start=position, edit_end=position;
        size_t insert_length=0;
        const char *insert=NULL;
        char *block=NULL;
        switch (input)
        {
            case KEY_F(3):
                if (!editor_mode) goto close_editor;
                if (mark_start >= 0 && mark_end < 0)
                {
                    mark_start=selected_start;
                    mark_end=selected_end;
                    if (mark_start == mark_end) mark_start=mark_end=-1;
                }
                else { mark_start=position; mark_end=-1; }
                break;
            case KEY_F(5):
            case KEY_F(6):
            case KEY_F(8):
            {
                if (!editor_mode || selected_start < 0 || selected_start == selected_end) break;
                if (input == KEY_F(8))
                {
                    edit_start=selected_start;
                    edit_end=selected_end;
                    break;
                }
                // Inserting inside the source would split the original selected block.
                if (position > selected_start && position < selected_end) break;
                if (input == KEY_F(6) && (position == selected_start || position == selected_end)) break;
                insert_length=selected_end-selected_start;
                if (input == KEY_F(6))
                {
                    edit_start=position < selected_start ? position : selected_start;
                    edit_end=position > selected_end ? position : selected_end;
                    insert_length=edit_end-edit_start;
                }
                block=malloc(insert_length);
                if (!block)
                {
                    show_errormsg("Not enough memory to copy the block.");
                    insert_length=0;
                    edit_end=edit_start;
                    break;
                }
                insert=block;
                if (input == KEY_F(5)) copy_text_range(lines, selected_start, selected_end, block);
                else if (position < selected_start)
                {
                    copy_text_range(lines, selected_start, selected_end, block);
                    copy_text_range(lines, position, selected_start, block+selected_end-selected_start);
                }
                else
                {
                    copy_text_range(lines, selected_end, position, block);
                    copy_text_range(lines, selected_start, selected_end, block+position-selected_end);
                }
                break;
            }
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
                    else { saved_revision=revision; is_modified=0; }
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
                    screen_start_col=text_column(search_line->line, found, tab_width);
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
                    cursor_byte=text_next(current_line->line, current_line->line_length, cursor_byte);
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
                    insert="\n";
                    insert_length=1;
                }
                break;
            case KEY_BACKSPACE:
                if (!editor_mode) break;
                if (cursor_byte > 0)
                    edit_start=seek+text_previous(current_line->line, cursor_byte);
                else if (cursor_row > 0) edit_start--;
                break;
            case KEY_DC:
                if (!editor_mode) break;
                if (cursor_byte < current_line->line_length)
                    edit_end=seek+text_next(current_line->line, current_line->line_length, cursor_byte);
                else if (current_line->next) edit_end++;
                break;
            case KEY_MOUSE:
            {
                if (!editor_mode) break;
                int pressed=event.bstate & BUTTON1_PRESSED;
                int released=event.bstate & (BUTTON1_RELEASED|BUTTON1_CLICKED);
                int clicked=event.bstate & BUTTON1_CLICKED;
                if (!(event.bstate & (BUTTON1_PRESSED|BUTTON1_RELEASED|BUTTON1_CLICKED|REPORT_MOUSE_POSITION))) break;
                if (drag_start < 0 && !pressed && !clicked) break;
                int outside=!wenclose(content_win, event.y, event.x);
                if (outside && drag_start < 0) break;
                if (event.y < 1) event.y=1;
                if (event.y >= max_y-1) event.y=max_y-2;
                if (event.x < 0) event.x=0;
                if (event.x >= max_x) event.x=max_x-1;
                cursor_row=screen_start_line+event.y-1;
                if (cursor_row >= num_lines) cursor_row=num_lines-1;
                off_t mouse_position;
                file_lines *line=line_at_row(lines, cursor_row, &mouse_position);
                cursor_byte=text_offset(line->line, line->line_length, screen_start_col+event.x, tab_width);
                mouse_position+=cursor_byte;
                // A press clears the old block; subsequent motion selects from this anchor.
                if (drag_start >= 0)
                {
                    mark_start=mouse_position < drag_start ? mouse_position : drag_start;
                    mark_end=mouse_position > drag_start ? mouse_position : drag_start;
                    if (mark_start == mark_end) mark_start=mark_end=-1;
                }
                else
                {
                    mark_start=mark_end=-1;
                    if (pressed) drag_start=mouse_position;
                }
                if (released) drag_start=-1;
                break;
            }
            default:
                if (input == '\t') strcpy(input_text, "\t");
                if (editor_mode && input_text[0])
                {
                    insert=input_text;
                    insert_length=strlen(input_text);
                }
                break;
        }
        undo_entry *entry=NULL;
        int changed_text=edit_start != edit_end || insert_length;
        if (changed_text)
        {
            entry=remember_edit(lines, before, edit_start, edit_end, insert_length);
            if (!entry || replace_text_range(lines, edit_start, edit_end, insert, insert_length, &num_lines) != 0)
            {
                show_errormsg(SPRINTF("Cannot edit file:\n%s", strerror(errno)));
                free(entry);
                entry=NULL;
            }
            else
            {
                num_bytes+=insert_length-(edit_end-edit_start);
                position=edit_start+insert_length;
                if (input == KEY_F(5))
                {
                    position=edit_start;
                    mark_start=selected_start;
                    mark_end=selected_end;
                }
                mark_start=move_mark(mark_start, edit_start, edit_end, insert_length, 1);
                mark_end=move_mark(mark_end, edit_start, edit_end, insert_length, 0);
                if (input == KEY_F(6))
                {
                    off_t length=selected_end-selected_start;
                    mark_start=edit_start;
                    if (seek+cursor_byte > selected_end) mark_start=edit_end-length;
                    mark_end=mark_start+length;
                    position=mark_start;
                }
                if (input == KEY_F(8)) mark_start=mark_end=-1;
                // An edit may join a base character to existing combining marks.
                off_t *marks[]={&mark_start, &mark_end};
                for (int i=0; i < 2; i++)
                    if (*marks[i] >= 0)
                    {
                        off_t offset=*marks[i];
                        file_lines *line=line_at_position(lines, &offset, NULL);
                        int column=text_column(line->line, offset, tab_width);
                        *marks[i]+=text_offset(line->line, line->line_length, column, tab_width)-offset;
                    }
                if (mark_start == mark_end) mark_start=mark_end=-1;
                line_at_position(lines, &position, &cursor_row);
                cursor_byte=position;
                revision=++next_revision;
                is_modified=revision != saved_revision;
            }
        }
        free(block);
        if (target_column >= 0)
        {
            current_line=line_at_row(lines, cursor_row, NULL);
            cursor_byte=text_offset(current_line->line, current_line->line_length, target_column, tab_width);
        }
        if (editor_mode && !changed_text &&
            (before.row != cursor_row || before.byte != cursor_byte || before.top != screen_start_line ||
             before.left != screen_start_col || before.mark_start != mark_start || before.mark_end != mark_end))
        {
            entry=remember_edit(lines, before, 0, 0, 0);
            if (!entry)
            {
                cursor_row=before.row; cursor_byte=before.byte;
                screen_start_line=before.top; screen_start_col=before.left;
                mark_start=before.mark_start; mark_end=before.mark_end;
                show_errormsg("Not enough memory for undo");
            }
        }
        if (entry) { entry->next=undo; undo=entry; }
    }

close_editor:
    while (undo) { undo_entry *previous=undo->next; free(undo); undo=previous; }
    if (editor_mode)
    {
        mousemask(saved_mousemask, NULL);
        mouseinterval(saved_mouseinterval);
        mouse_tracking((saved_mousemask & REPORT_MOUSE_POSITION) != 0);
    }
    delwin(toprow_win);
    delwin(content_win);
    free_file_lines(lines);
    return 0;
}

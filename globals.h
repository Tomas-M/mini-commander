#ifndef GLOBALS_H
#define GLOBALS_H

void initialize_ncurses(void);
void refresh_screen(int cursor_visibility);
void draw_buttons(int maxY, int maxX, const char *const labels[9]);
extern const char *const panel_buttons[9];
int button_key(MEVENT *event, const char *const labels[9], int *pressed);
void mouse_tracking(int enabled);
void select_file(PanelProp *panel, FileNode *file, int selected);
int panel_mouse(MEVENT *event, PanelMouse *mouse);
void draw_windows(int maxY, int maxX);
int text_cell(const char *text, int length, wchar_t *chars, int *width);
int text_previous(const char *text, int position);
int text_next(const char *text, int length, int position);
int text_column(const char *text, int length, int tab_width);
int text_offset(const char *text, int length, int column, int tab_width);
void draw_text(WINDOW *win, int row, int x, const char *text, int start, int width);
int read_text_key(WINDOW *win, char *text, MEVENT *mouse);
void shorten(char *name, int width, char *result);
void update_panel(WINDOW *win, PanelProp *panel);
void update_panel_cursor(void);
void restore_panel_position(PanelProp *panel);
void init_screen(void);
void cleanup(void);
void redraw_ui(void);
int compare_nodes(FileNode *a, FileNode *b, SortOrders sort_order);
void sort_file_nodes(FileNode **head_ref, SortOrders sort_order);
int update_panel_files(PanelProp *panel);
void update_files_in_both_panels(void);
void free_file_nodes(FileNode *head);
int lines(char * title);
WINDOW *create_dialog(char *title, char *buttons[], int prompt_is_present, int is_danger, int vertical_buttons);
void update_dialog_buttons(WINDOW *win, char * title, char *buttons[], int selected, int prompt_present, int editing_prompt, int is_danger, int vertical_buttons);
int show_dialog(char *title, char *buttons[], int selected, char *prompt, int is_danger, int vertical_buttons, int edit_prompt);
void show_errormsg(char * msg);
void update_cmd(void);
int execute_file(const char *filename);
int command_cd(const char *command);
void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode, int syntax, off_t mark_start, off_t mark_end);
int view_edit_file(char *filename, int editor_mode);
int file_has_extension(const char *filename, const char *extensions[]);
void dive_into_directory(FileNode *current);
int change_panel_directory(PanelProp *panel, const char *path);
int noesc(int ch);
void format_size_with_units(off_t size, char *size_str, size_t len, int maxlen);
void show_shadow(WINDOW *win);
void draw_dialog_frame(WINDOW *win, int separator);
void draw_dialog_text(WINDOW *win, int row, const char *text);
void dialog_save_screen();
void dialog_restore_screen();
void create_progress_dialog(int title_lines);
int file_exists(const char *path);
int update_progress_dialog(char *title, int current_progress, int total_progress, char *infotext);
int update_progress_dialog_delta(char *title, int current_progress, int total_progress, char *infotext);
int panel_mass_action(OperationFunc func, char *tgt, operationContext *context);
void run_file_operation(OperationFunc operation, char *target);
int recursive_operation(const char *src, const char *tgt, operationContext *context, OperationFunc func);
int copy_operation(const char *src, const char *tgt, operationContext *context);
int move_operation(const char *src, const char *tgt, operationContext *context);
int delete_operation(const char *src, const char *tgt, operationContext *context);
int countstats_operation(const char *src, const char *tgt, operationContext *context);
int mkdir_recursive(const char *path, mode_t mode);
int format_number(off_t num, char *str);
char *format_text(char *buffer, const char *format, ...);

// Macro to use shorten inline
#define SHORTEN(name, width) ({ \
    static char result_buf[CMD_MAX] = {0}; \
    shorten((name), (width), result_buf); \
    result_buf; \
})

// Compound literals remain valid until the calling block ends.
#define SPRINTF(...) format_text((char[CMD_MAX]){0}, __VA_ARGS__)

#endif // GLOBALS_H

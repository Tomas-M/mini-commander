#ifndef GLOBALS_H
#define GLOBALS_H

void initialize_ncurses(void);
void draw_buttons(int maxY, int maxX);
void draw_windows(int maxY, int maxX);
void shorten(char *name, int width, char *result);
void update_panel(WINDOW *win, PanelProp *panel);
void update_panel_cursor(void);
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
int show_dialog(char *title, char *buttons[], int selected, char *prompt, int is_danger, int vertical_buttons);
void show_errormsg(char * msg);
void cursor_to_cmd(void);
void update_cmd(void);
void display_line(WINDOW *win, file_lines *line, int max_x, int current_col, int editor_mode, PatternColorPair* patterns, int num_patterns);
int view_file(char *filename);
int edit_file(char *filename);
int file_has_extension(const char *filename, const char *extensions[]);
void dive_into_directory(FileNode *current);
int noesc(int ch);
void format_size_with_units(off_t size, char *size_str, size_t len, int maxlen);
void show_shadow(WINDOW *win);
void dialog_save_screen();
void dialog_restore_screen();
void create_progress_dialog(int title_lines);
int file_exists(const char *path);
int update_progress_dialog(char *title, int current_progress, int total_progress, char *infotext);
int update_progress_dialog_delta(char *title, int current_progress, int total_progress, char *infotext);
int panel_mass_action(OperationFunc func, char *tgt, operationContext *context);
int recursive_operation(const char *src, const char *tgt, operationContext *context, OperationFunc func);
int copy_operation(const char *src, const char *tgt, operationContext *context);
int move_operation(const char *src, const char *tgt, operationContext *context);
int delete_operation(const char *src, const char *tgt, operationContext *context);
int countstats_operation(const char *src, const char *tgt, operationContext *context);
int mkdir_recursive(const char *path, mode_t mode);
int format_number(off_t num, char *str);

// Macro to use shorten inline
#define SHORTEN(name, width) ({ \
    static char result_buf[CMD_MAX] = {0}; \
    shorten((name), (width), result_buf); \
    result_buf; \
})

#define SPRINTF(fmt, ...) ({ \
    char tmp[CMD_MAX]; \
    sprintf(tmp, fmt, ##__VA_ARGS__); \
    tmp; \
})

#endif // GLOBALS_H

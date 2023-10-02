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
WINDOW *create_dialog(char *title, char *buttons[], int prompt_is_present, int is_danger);
void update_dialog_buttons(WINDOW *win, char * title, char *buttons[], int selected, int prompt_present, int editing_prompt, int is_danger);
int show_dialog(char *title, char *buttons[], char *prompt, int is_danger);
void cursor_to_cmd(void);
void update_cmd(void);
void display_line(WINDOW *win, file_lines *line, int max_x, int current_col);
int view_file(char *filename);
int file_has_extension(const char *filename, const char *extensions[]);
void dive_into_directory(FileNode *current);
int noesc(int ch);

// Macro to use shorten inline
#define SHORTEN(name, width) ({ \
    static char result_buf[CMD_MAX] = {0}; \
    shorten((name), (width), result_buf); \
    result_buf; \
})


#endif // GLOBALS_H

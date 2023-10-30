
#define CMD_MAX 16384
#define KEY_ALT_ENTER    0507  /* custom alt-enter key */
#define KEY_ALT_a        0506  /* custom alt-a key */
#define KEY_ALT_s        0505  /* custom alt-s key */

typedef enum {
    SORT_BY_NAME_ASC = 0,
    SORT_BY_SIZE_ASC,
    SORT_BY_TIME_ASC,
    SORT_BY_NAME_DESC,
    SORT_BY_SIZE_DESC,
    SORT_BY_TIME_DESC,
    SORT_BY_NAME_DIRSFIRST_ASC,
    SORT_BY_SIZE_DIRSFIRST_ASC,
    SORT_BY_TIME_DIRSFIRST_ASC,
    SORT_BY_NAME_DIRSFIRST_DESC,
    SORT_BY_SIZE_DIRSFIRST_DESC,
    SORT_BY_TIME_DIRSFIRST_DESC
} SortOrders;

typedef struct FileNode {
    char* name;
    time_t mtime;
    off_t size;
    mode_t chmod;
    uid_t chown;
    int is_dir;
    int is_executable;
    int is_link;
    int is_link_to_dir;  // link points to a directory
    int is_link_broken; // invalid link
    int is_device;
    char* link_target;
    int is_selected; // with Insert key
    struct FileNode *next;
} FileNode;

typedef struct PanelProp {
    int selected_index;
    int scroll_index;
    SortOrders sort_order;
    char path[CMD_MAX];
    char file_under_cursor[CMD_MAX];
    int num_selected_files;
    off_t bytes_selected_files;
    int files_count;
    int search_mode;
    char search_text[CMD_MAX];
    char prev_search_text[CMD_MAX];
    FileNode *files;
} PanelProp;

typedef struct file_lines {
    char *line;
    int line_length;
    struct file_lines *next;
} file_lines;

enum Color {
    COLOR_WHITE_ON_BLACK = 1,
    COLOR_WHITE_ON_BLUE,
    COLOR_WHITE_ON_RED,
    COLOR_BLACK_ON_WHITE,
    COLOR_BLACK_ON_CYAN,
    COLOR_BLACK_ON_CYAN_BTN,
    COLOR_BLACK_ON_CYAN_PMPT,
    COLOR_YELLOW_ON_CYAN,
    COLOR_YELLOW_ON_BLUE,
    COLOR_GREEN_ON_BLUE,
    COLOR_MAGENTA_ON_BLUE,
    COLOR_CYAN_ON_BLUE,
    COLOR_RED_ON_BLUE
};


typedef struct operationContext {
    off_t current_size;
    off_t current_items;
    off_t total_size;
    off_t total_items;
    int confirm_all_yes;
    int confirm_all_no;
    int skip_all;
    int keep_item_selected;
    char confirm_yes_prefix[CMD_MAX];
    int abort;
} operationContext;

enum operationResult {
    OPERATION_OK = 0,
    OPERATION_RETRY,
    OPERATION_PARENT_OK_PROCESS_CHILDS,
    OPERATION_RETRY_AFTER_CHILDS,
    OPERATION_SKIP,
    OPERATION_ABORT
};


typedef int (*OperationFunc)(const char *, const char *, operationContext *);


// Define a struct to pair regex patterns with their associated colors.
typedef struct {
    char *pattern;
    int color_pair;
    int is_bold;
} PatternColorPair;


extern PanelProp left_panel;
extern PanelProp right_panel;
extern PanelProp* active_panel;

// Global windows
extern WINDOW *win1;
extern WINDOW *win2;
extern WINDOW *progress;

extern struct utsname unameData;
extern struct passwd *pw;
extern const char *username;

extern struct timeval last_click_time;
extern struct timeval current_time;
extern struct timeval diff_time;

extern int cursor_pos;
extern int cmd_offset;
extern int prompt_length;

extern char cmd[CMD_MAX];
extern int cmd_len;

extern int color_enabled;

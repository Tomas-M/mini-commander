#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "types.h"
#include "globals.h"


int panel_mass_action(OperationFunc operation) {
    int err = 0;
    char source_path[CMD_MAX] = {0};
    char target_path[CMD_MAX] = {0};
    PanelProp * inactive_panel = active_panel == &left_panel ? &right_panel : &left_panel;
    operationContext context = {0};

    if (active_panel->num_selected_files == 0) {
        sprintf(source_path, "%s/%s", active_panel->path, active_panel->file_under_cursor);
        sprintf(target_path, "%s/%s", inactive_panel->path, active_panel->file_under_cursor);
        err = recursive_operation(source_path, target_path, &context, operation);
    } else {
        FileNode *temp = active_panel->files;
        while (temp != NULL) {
            if (temp->is_selected) {
                sprintf(source_path, "%s/%s", active_panel->path, temp->name);
                sprintf(target_path, "%s/%s", inactive_panel->path, temp->name);
                err = recursive_operation(source_path, target_path, &context, operation);
                if (err == OPERATION_OK) {
                    if (temp->is_selected) {
                        active_panel->num_selected_files--;
                    }
                    temp->is_selected = 0;
                } else if (err != OPERATION_SKIP) {
                    // error was printed in operation()
                }
            }
            temp = temp->next;
        }
    }
    update_files_in_both_panels();
    return 0;
}

int recursive_operation(const char *src, const char *tgt, operationContext *context, OperationFunc operation) {
    int ret;
    struct stat statbuf;
    if (lstat(src, &statbuf) == -1) return -1;

    // try the operation right away
    ret = operation(src, tgt, context);

    if (ret == OPERATION_OK) {
        // operation on parent item was OK, finish here
    } else {
        // OPERATION_PARENT_OK || OPERATION_RETRY_AFTER_CHILDS || OPERATION_SKIP
        // Recursive operation is needed for further processing
        if (S_ISDIR(statbuf.st_mode)) {
            DIR *dir = opendir(src);
            if (!dir) return -1;
            struct dirent *entry;
            while ((entry = readdir(dir))) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                char source_path[CMD_MAX];
                char target_path[CMD_MAX];
                sprintf(source_path, "%s%s%s", src, src[strlen(src) - 1] == '/' ? "" : "/", entry->d_name);
                sprintf(target_path, "%s%s%s", tgt, tgt[strlen(tgt) - 1] == '/' ? "" : "/", entry->d_name);
                recursive_operation(source_path, target_path, context, operation);
            }
            closedir(dir);
            if (ret == OPERATION_RETRY_AFTER_CHILDS) {
                // try again the initial src
                ret = operation(src, tgt, context);
                if (ret == OPERATION_OK) {
                    // operation on parent item was OK, finish here
                } else {
                    // print error
                    return ret;
                }
            }
        }
    }

    return 0;
}

int stats_operation(const char *src, const char *tgt, operationContext *context) {
    struct stat statbuf;
    if (lstat(src, &statbuf) != -1) {
        context->total_items++;
        if (!S_ISDIR(statbuf.st_mode)) {
           context->total_size += statbuf.st_size;
        }
    }
    return 0;
}



int delete_operation(const char *src, const char *tgt, operationContext *context) {
    int ret = 0;
    struct stat statbuf;
    if (lstat(src, &statbuf) == -1) return -1;

    if (S_ISDIR(statbuf.st_mode)) {
        ret = rmdir(src);
        if (ret == 0) return OPERATION_OK;
        if (errno == ENOTEMPTY || errno == EEXIST) {
            // directory not empty, ask user what to do next
            char title[CMD_MAX] = {};
            sprintf(title, "Directory \"%s\" not empty.\nDelete it recursively?", src);
            int btn = show_dialog(title, (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, NULL, 1);
            if (btn == 1) { // yes
                return OPERATION_RETRY_AFTER_CHILDS;
            } else if (btn == 2) { // no
                return OPERATION_SKIP;
            } else if (btn == 3) { // all
                context->confirm_all = 1;
                return OPERATION_RETRY_AFTER_CHILDS;
            } else if (btn == 4) { // none
                context->confirm_none = 1;
                return OPERATION_SKIP;
            } else if (btn == 5) { // abort
                context->abort = 1;
                return OPERATION_OK;
            }
        }
        else return errno;
    } else {
        ret = unlink(src);
        if (ret == 0) return OPERATION_OK;
        else return errno;
    }
}


int copy_operation(const char *src, const char *tgt, operationContext *context) {
}

int move_operation(const char *src, const char *tgt, operationContext *context) {
}



int mkdir_recursive(const char *path, mode_t mode) {
    struct stat st;

    // Check if the directory exists and is really a directory
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return 0; // Directory already exists
        } else {
            return EEXIST; // Path exists but is not a directory
        }
    } else if (errno != ENOENT) {
        // If the error is not "no such file or directory", return with an error
        return errno;
    }

    // try mkdir directly, if OK return
    if (mkdir(path, mode) == 0) return 0;

    // If the directory does not exist and could not be created so far, start the recursive creation
    char tmp[256];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (stat(tmp, &st) != 0) {
                if (errno == ENOENT) {
                    if (mkdir(tmp, mode) != 0) {
                        return errno; // Error creating directory
                    }
                } else {
                    return errno; // Some other error occurred
                }
            } else if (!S_ISDIR(st.st_mode)) {
                return ENOTDIR; // Path exists but is not a directory
            }
            *p = '/';
        }
    }

    return mkdir(tmp, mode) ? errno : 0;
}



int copy_file_or_directory(const char *src, const char *dest) {
    struct stat statbuf;
    int result = lstat(src, &statbuf);
    if (result == -1) {
        perror("lstat");
        return -1;
    }

    if (S_ISREG(statbuf.st_mode)) {
        int src_fd = open(src, O_RDONLY);
        int dest_fd = open(dest, O_WRONLY | O_CREAT, statbuf.st_mode);
        char buffer[4096];
        ssize_t bytes;
        while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
            write(dest_fd, buffer, bytes);
        }
        close(src_fd);
        close(dest_fd);
    } else if (S_ISDIR(statbuf.st_mode)) {
        result = mkdir(dest, statbuf.st_mode);
        if (result == -1) {
            perror("mkdir");
            return -1;
        }
    } else if (S_ISLNK(statbuf.st_mode)) {
        char buffer[4096];
        ssize_t len = readlink(src, buffer, sizeof(buffer) - 1);
        if (len != -1) {
            buffer[len] = '\0';
            symlink(buffer, dest);
        }
    } else if (S_ISCHR(statbuf.st_mode) || S_ISBLK(statbuf.st_mode)) {
        mknod(dest, statbuf.st_mode, statbuf.st_rdev);
    }

    return 0;
}


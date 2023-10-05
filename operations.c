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
                context.item_skipped = 0;
                sprintf(source_path, "%s/%s", active_panel->path, temp->name);
                sprintf(target_path, "%s/%s", inactive_panel->path, temp->name);
                err = recursive_operation(source_path, target_path, &context, operation);
                if (context.abort == 1) break;
                if (err == OPERATION_OK && context.item_skipped == 0) {
                    if (temp->is_selected) {
                        active_panel->num_selected_files--;
                    }
                    temp->is_selected = 0;
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

    // try the operation right away
    ret = operation(src, tgt, context);
    if (context->abort == 1) return OPERATION_ABORT;

    if (ret == OPERATION_OK) {
        // operation on parent item was OK, finish here
        return ret;
    } else if (ret == OPERATION_SKIP) {
        // do nothing, return skip
        return ret;
    } else if (ret == OPERATION_PARENT_OK_PROCESS_CHILDS || ret == OPERATION_RETRY_AFTER_CHILDS) {
        // Recursive operation on a directory is needed for further processing
        struct stat statbuf = {0};
        lstat(src, &statbuf); // no error checking, we assume that if original operation was ok, this will be ok too
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
                if (context->abort == 1) return 0;
            }
            closedir(dir);
            if (ret == OPERATION_RETRY_AFTER_CHILDS) {
                // try again the initial src
                ret = operation(src, tgt, context);
                if (context->abort == 1) return 0;
                if (ret == OPERATION_OK) {
                    // operation on parent item was OK, finish here
                } else {
                    // print error
                    return ret;
                }
            }
        } else {
            // no childs, end ok
            return OPERATION_OK;
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
    // tgt is ignored for delete operation
    int ret = OPERATION_RETRY;
    int btn = 0;

    while (ret == OPERATION_RETRY) {

        struct stat statbuf;
        ret = lstat(src, &statbuf);
        if (ret != 0) {
            if (context->skip_all == 1) return OPERATION_SKIP;
            btn = show_dialog(SPRINTF("Stat failed for \"%s\"\n%s (%d)", src, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
            if (btn == 1 || btn == 0) { context->item_skipped = 1; return OPERATION_SKIP; }
            if (btn == 2) { context->skip_all = 1; return OPERATION_SKIP; }
            if (btn == 3) { ret = OPERATION_RETRY; continue; }
            if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
        }

        if (S_ISDIR(statbuf.st_mode)) {
            ret = rmdir(src);
            if (ret == 0) return OPERATION_OK;

            // if error is directory not empty, ask user to delete subdirectories
            if (errno == ENOTEMPTY || errno == EEXIST) {
                // directory not empty, ask user what to do next
                btn = 0;
                if (context->confirm_all_yes == 1) {
                    btn = 1;
                }
                if (strlen(context->confirm_yes_prefix) != 0 && strncmp(context->confirm_yes_prefix, src, strlen(context->confirm_yes_prefix)) == 0) {
                    btn = 1;
                }
                if (context->confirm_all_no == 1) {
                    btn = 2;
                }

                if (btn == 0) {
                    char title[CMD_MAX] = {};
                    sprintf(title, "Directory \"%s\" not empty.\nDelete it recursively?\n", src);
                    btn = show_dialog(title, (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, NULL, 1);
                }

                if (btn == 1) { // yes
                    sprintf(context->confirm_yes_prefix, "%s", src);
                    return OPERATION_RETRY_AFTER_CHILDS;
                } else if (btn == 2 || btn == 0) { // no
                    context->item_skipped = 1;
                    return OPERATION_SKIP;
                } else if (btn == 3) { // all
                    context->confirm_all_yes = 1;
                    return OPERATION_RETRY_AFTER_CHILDS;
                } else if (btn == 4) { // none
                    context->item_skipped = 1;
                    context->confirm_all_no = 1;
                    return OPERATION_SKIP;
                } else if (btn == 5) { // abort
                    context->abort = 1;
                    return OPERATION_SKIP;
                }
            } else {
                if (context->skip_all == 1) return OPERATION_SKIP;
                btn = show_dialog(SPRINTF("Cannot remove \"%s\"\n%s (%d)", src, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
                if (btn == 0 || btn == 1) { context->item_skipped = 1; return OPERATION_SKIP; }
                if (btn == 2) { context->item_skipped = 1; context->skip_all = 1; return OPERATION_SKIP; }
                if (btn == 3) { ret = OPERATION_RETRY; continue; }
                if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
            }
        } else {
            ret = unlink(src);
            if (ret == 0) return OPERATION_OK;
            else {
                if (context->skip_all == 1) return OPERATION_SKIP;
                btn = show_dialog(SPRINTF("Cannot remove \"%s\"\n%s (%d)", src, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
                if (btn == 0 || btn == 1) return OPERATION_SKIP;
                if (btn == 2) { context->item_skipped = 1; context->skip_all = 1; return OPERATION_SKIP; }
                if (btn == 3) { ret = OPERATION_RETRY; continue; }
                if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
            }
        }
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


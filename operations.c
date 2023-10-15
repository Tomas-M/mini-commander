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


int panel_mass_action(OperationFunc operation, char *tgt, operationContext *context) {
    int err = 0;
    char source_path[CMD_MAX] = {0};
    char target_path[CMD_MAX] = {0};
    char target[CMD_MAX] = {0};
    FileNode *unselect_item = NULL;

    WINDOW *saved_screen;
    saved_screen = dupwin(newscr);

    create_progress_dialog(1);

    if (active_panel->num_selected_files == 0) {

        FileNode *current = active_panel->files;
        while (current != NULL) {
            if (strcmp(current->name, active_panel->file_under_cursor) == 0) {
                current->is_selected = 1;
                active_panel->num_selected_files = 1;
                active_panel->bytes_selected_files = current->size;
                unselect_item = current;
                break;
            }
            current = current->next;
        }
    }

    int initial_num_selected = active_panel->num_selected_files;

    // process selected files
    FileNode *current = active_panel->files;
    while (current != NULL) {
        if (current->is_selected) {
            context->keep_item_selected = 0;
            sprintf(source_path, "%s/%s", active_panel->path, current->name);

            if (tgt != NULL && strlen(tgt) > 0)
            {
                if (tgt[0] == '/') { // absolute path
                    sprintf(target, "%s", tgt);
                } else { // relative path
                    sprintf(target, "%s/%s", active_panel->path, tgt);
                }

                if (initial_num_selected == 1 && !file_exists(target)) {
                    sprintf(target_path, "%s", target);
                } else {
                    sprintf(target_path, "%s/%s", target, current->name);
                }
            }
            err = recursive_operation(source_path, target_path, context, operation);
            if (context->abort == 1) break;
            if (err == OPERATION_OK && context->keep_item_selected == 0) {
                if (current->is_selected) {
                    active_panel->num_selected_files--;
                }
                current->is_selected = 0;
            }
        }
        current = current->next;
    }

    if (unselect_item != NULL) {
        unselect_item->is_selected = 0;
        active_panel->num_selected_files = 0;
        active_panel->bytes_selected_files = 0;
    }

    update_progress_dialog_delta(NULL, 0, 0, NULL); // reset internal count of lines, and internal time counter
    delwin(progress); // was created by create_progress_dialog

    overwrite(saved_screen, newscr);
    delwin(saved_screen);
    wrefresh(newscr);
    return 0;
}


int recursive_operation(const char *src, const char *tgt, operationContext *context, OperationFunc operation) {
    int ret;
    context->current_items++;

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




int countstats_operation(const char *src, const char *tgt, operationContext *context) {

    struct stat statbuf;
    if (lstat(src, &statbuf) != -1) {
        context->total_items++;
        if (!S_ISDIR(statbuf.st_mode)) {
           context->total_size += statbuf.st_size;
        }
    }
    char infotext[CMD_MAX];
    char num[30];

    context->keep_item_selected = 1; // don't unselect items on stat
    format_number(context->total_size, num);
    sprintf(infotext, "Items: %d\nSize: %s bytes", context->total_items, num);

    int delta = update_progress_dialog_delta(SPRINTF("Scanning %s", src), 0, 0, infotext);
    if (delta == 1) {
        // ignored here
    }
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    return OPERATION_PARENT_OK_PROCESS_CHILDS;
}



int delete_operation(const char *src, const char *tgt, operationContext *context) {
    // tgt is ignored for delete operation
    int ret = OPERATION_RETRY;
    int btn = 0;
    errno = 0;

    int delta = update_progress_dialog_delta(SPRINTF("Delete\n%s", src), 100, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
    if (delta == 1) {
        // ignored
    }
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {

        struct stat statbuf;
        ret = lstat(src, &statbuf);
        if (ret != 0) {
            if (context->skip_all == 1) return OPERATION_SKIP;
            btn = show_dialog(SPRINTF("Stat failed for \"%s\"\n%s (%d)", src, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
            if (btn == 1 || btn == 0) { context->keep_item_selected = 1; return OPERATION_SKIP; }
            if (btn == 2) { context->keep_item_selected = 1; context->skip_all = 1; return OPERATION_SKIP; }
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
                int prefix_already_matches = 0;
                if (context->confirm_all_yes == 1) {
                    btn = 1;
                }
                if (strlen(context->confirm_yes_prefix) != 0 && strncmp(context->confirm_yes_prefix, src, strlen(context->confirm_yes_prefix)) == 0) {
                    btn = 1;
                    prefix_already_matches = 1;
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
                    if (!prefix_already_matches) {
                        sprintf(context->confirm_yes_prefix, "%s", src);
                    }
                    return OPERATION_RETRY_AFTER_CHILDS;
                } else if (btn == 2 || btn == 0) { // no
                    context->keep_item_selected = 1;
                    return OPERATION_SKIP;
                } else if (btn == 3) { // all
                    context->confirm_all_yes = 1;
                    return OPERATION_RETRY_AFTER_CHILDS;
                } else if (btn == 4) { // none
                    context->keep_item_selected = 1;
                    context->confirm_all_no = 1;
                    return OPERATION_SKIP;
                } else if (btn == 5) { // abort
                    context->abort = 1;
                    return OPERATION_SKIP;
                }
            } else {
                if (context->skip_all == 1) return OPERATION_SKIP;
                btn = show_dialog(SPRINTF("Cannot remove \"%s\"\n%s (%d)", src, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
                if (btn == 0 || btn == 1) { context->keep_item_selected = 1; return OPERATION_SKIP; }
                if (btn == 2) { context->keep_item_selected = 1; context->skip_all = 1; return OPERATION_SKIP; }
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
                if (btn == 2) { context->keep_item_selected = 1; context->skip_all = 1; return OPERATION_SKIP; }
                if (btn == 3) { ret = OPERATION_RETRY; continue; }
                if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
            }
        }
    }
}


int copy_operation(const char *src, const char *tgt, operationContext *context) {
    int ret = OPERATION_RETRY;
    errno = 0; // reset

    int delta = update_progress_dialog_delta(SPRINTF("Copying\n%s\nTo\n%s", src, tgt), 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
    if (delta == 1) {
        // ignored here
    }
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {

        int btn = 0;
        char errmsg[CMD_MAX] = {0};
        int target_exists = 1;

        do {
            struct stat statbufsrc;
            if (lstat(src, &statbufsrc) != 0) {
                sprintf(errmsg,"Stat operation failed for %s", src);
                break;
            }

            struct stat statbuftgt;
            if (lstat(tgt, &statbuftgt) != 0) {
                if (errno == ENOENT) {
                    target_exists = 0;
                } else { // other error
                    sprintf(errmsg,"Stat operation failed for %s", tgt);
                    break;
                }
            }

            // source is a regular file
            if (S_ISREG(statbufsrc.st_mode)) {

                if (target_exists && S_ISDIR(statbuftgt.st_mode)) {
                    sprintf(errmsg,"Cannot overwrite directory\n%s\nwith a file\n%s", tgt, src);
                    break;
                }

                int src_fd = open(src, O_RDONLY);
                if (src_fd == -1) {
                    sprintf(errmsg,"Cannot open source file for reading:\n%s", src);
                    break;
                }

                int tgt_fd = open(tgt, O_WRONLY | O_CREAT | O_EXCL, statbufsrc.st_mode);
                if (tgt_fd == -1) {
                    if (errno == EEXIST) {
                        // ask user if overwrite
                        btn = 0;
                        if (context->confirm_all_yes == 1) btn = 1;
                        if (context->confirm_all_no == 1) btn = 2;
                        if (btn == 0) {
                            btn = show_dialog(SPRINTF("Target file exists:\n%s\nOverwrite this file?", tgt), (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, NULL, 1);
                        }
                        if (btn == 3) { // All
                            context->confirm_all_yes = 1;
                            btn = 1;
                        }
                        if (btn == 1) { // Yes
                            tgt_fd = open(tgt, O_WRONLY | O_CREAT | O_TRUNC, statbufsrc.st_mode);
                            if (tgt_fd == -1) {
                                close(src_fd);
                                sprintf(errmsg,"Cannot open target file for writing:\n%s", tgt);
                            }
                        }
                        if (btn == 2) { // No
                            close(src_fd);
                            return OPERATION_SKIP;
                        }
                        if (btn == 4) { // None
                            context->confirm_all_no = 1;
                            return OPERATION_SKIP;
                        }
                        if (btn == 5) {
                            context->abort = 1;
                            return OPERATION_ABORT;
                        }
                    } else {
                        close(src_fd);
                        sprintf(errmsg,"Cannot open target file for writing:\n%s", tgt);
                        break;
                    }
                }

                char buffer[16384];
                ssize_t bytes = 0;
                ssize_t total_bytes = 0;
                while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
                    if (write(tgt_fd, buffer, bytes) != bytes) {
                        close(src_fd);
                        close(tgt_fd);
                        sprintf(errmsg,"Cannot write data to:\n%s", tgt);
                        break;
                    }
                    total_bytes += bytes;
                    int delta = update_progress_dialog_delta(SPRINTF("Copying\n%s\nTo\n%s", src, tgt), statbufsrc.st_size > 0 ? total_bytes * 100 / statbufsrc.st_size : 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
                    if (delta > 0) {
                        close(src_fd);
                        close(tgt_fd);
                        int answer = show_dialog(SPRINTF("Incomplete file was retrieved. Keep it?\n%s", tgt), (char *[]) {"Keep it", "Delete", NULL}, NULL, 1);
                        if (answer == 2) unlink(tgt);
                        if (delta == 2) {
                            context->abort = 1;
                            return OPERATION_ABORT;
                        }
                        return OPERATION_SKIP;
                    }
                }

                if (strlen(errmsg) > 0) break; // second level break

                if (bytes == -1) {
                    // Handle error
                    close(src_fd);
                    close(tgt_fd);
                    sprintf(errmsg,"Cannot read data from:\n%s", src);
                    break;
                }

                int delta = update_progress_dialog_delta(SPRINTF("Copying\n%s\nTo\n%s", src, tgt), statbufsrc.st_size > 0 ? total_bytes * 100 / statbufsrc.st_size : 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);

                close(src_fd);
                close(tgt_fd);
                ret = 0;
            }
            // source is a directory
            else if (S_ISDIR(statbufsrc.st_mode)) {
                if (target_exists && S_ISDIR(statbuftgt.st_mode)) {
                    // do not overwrite existing directory
                    ret = 0;
                } else if (mkdir(tgt, statbufsrc.st_mode) == -1) {
                    sprintf(errmsg,"Failed to create directory:\n%s", tgt);
                    break;
                } else {
                    ret = 0;
                }
            }
            // source is a symlink
            else if (S_ISLNK(statbufsrc.st_mode)) {
                char buffer[CMD_MAX];
                ssize_t len = readlink(src, buffer, sizeof(buffer) - 1);
                if (len == -1) {
                    sprintf(errmsg,"Failed to read symbolic link from\n%s", src);
                    break;
                }
                buffer[len] = '\0';

                if (target_exists) {
                    // Ask user if they want to overwrite
                    btn = 0;
                    if (context->confirm_all_yes == 1) btn = 1;
                    if (context->confirm_all_no == 1) btn = 2;
                    if (btn == 0) {
                        btn = show_dialog(SPRINTF("Target file exists:\n%s\nOverwrite this file?", tgt), (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, NULL, 1);
                    }
                    if (btn == 3) { // All
                        context->confirm_all_yes = 1;
                        btn = 1;
                    }
                    if (btn == 1) { // Yes
                        // Remove the existing target
                        if (unlink(tgt) == -1) {
                            sprintf(errmsg, "Failed to remove existing target file\n%s", tgt);
                            break;
                        }
                    } else if (btn == 2 || btn == 0) { // No
                        return OPERATION_SKIP;
                    } else if (btn == 4) { // None
                        context->confirm_all_no = 1;
                        return OPERATION_SKIP;
                    } else if (btn == 5) { // abort
                        context->abort = 1;
                        return OPERATION_ABORT;
                    }
                }

                if (symlink(buffer, tgt) == -1) {
                    sprintf(errmsg,"Failed to create symbolic link\n%s", tgt);
                    break;
                } else {
                    ret = 0;
                }
            }
            // source is a character device or block device
            else if (S_ISCHR(statbufsrc.st_mode) || S_ISBLK(statbufsrc.st_mode)) {
                if (mknod(tgt, statbufsrc.st_mode, statbufsrc.st_rdev) == -1) {
                    sprintf(errmsg,"Failed to create special file\n%s", tgt);
                    break;
                } else {
                    ret = 0;
                }
            }
        } while (false);


        if (strlen(errmsg) > 0) {
            if (context->skip_all == 1) return OPERATION_SKIP;
            if (errno != 0) {
                btn = show_dialog(SPRINTF("%s\n%s (%d)", errmsg, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
            } else {
                btn = show_dialog(SPRINTF("%s", errmsg), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
            }
            if (btn == 1 || btn == 0) { context->keep_item_selected = 1; return OPERATION_SKIP; }
            if (btn == 2) { context->keep_item_selected = 1; context->skip_all = 1; return OPERATION_SKIP; }
            if (btn == 3) { ret = OPERATION_RETRY; continue; }
            if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
        }

        return OPERATION_PARENT_OK_PROCESS_CHILDS;
    }

    return 0;
}


int move_operation(const char *src, const char *tgt, operationContext *context) {
    int ret = OPERATION_RETRY;
    errno = 0; // reset

    int delta = update_progress_dialog_delta(SPRINTF("Renaming\n%s\nTo\n%s", src, tgt), 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
    if (delta == 1) {
        // ignored here
    }
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {
        int btn = 0;
        char errmsg[CMD_MAX] = {0};

        ret = rename(src, tgt);
        if (ret != 0) {
            if (context->skip_all == 1) return OPERATION_SKIP;
            btn = show_dialog(SPRINTF("Failed to rename\n%s\nTo\n%s\n%s (%d)", src, tgt, strerror(errno), errno), (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, NULL, 1);
            if (btn == 1 || btn == 0) { context->keep_item_selected = 1; return OPERATION_SKIP; }
            if (btn == 2) { context->keep_item_selected = 1; context->skip_all = 1; return OPERATION_SKIP; }
            if (btn == 3) { ret = OPERATION_RETRY; continue; }
            if (btn == 4) { context->abort = 1; return OPERATION_ABORT; }
        }
    }
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


int file_exists(const char *path) {
    struct stat info;

    if (lstat(path, &info) != 0) {
        // If stat fails, the path does not exist
        return 0;
    }

    return 1;
}


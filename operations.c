#include "includes.h"
#include "types.h"
#include "globals.h"

// Scan once for progress totals, then perform the requested operation on the same selection.
void run_file_operation(OperationFunc operation, char *target)
{
    operationContext context={0};
    panel_mass_action(countstats_operation, "", &context);
    if (context.abort) return;
    context.current_items=0;
    panel_mass_action(operation, target, &context);
}


// Share overwrite choices across regular files, symbolic links and renames.
static int confirm_overwrite(const char *target, operationContext *context)
{
    if (context->confirm_all_no) return OPERATION_SKIP;
    if (context->confirm_all_yes) return OPERATION_OK;
    int button=show_dialog(SPRINTF("Target file exists:\n%s\nOverwrite this file?", target),
        (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, 0, NULL, 1, 0, 0);
    if (button == 3) context->confirm_all_yes=1;
    if (button == 1 || button == 3) return OPERATION_OK;
    if (button == 5) { context->abort=1; return OPERATION_ABORT; }
    if (button == 4) context->confirm_all_no=1;
    context->keep_item_selected=1;
    return OPERATION_SKIP;
}

// Handle all recoverable operation errors consistently; closing the dialog skips the item.
static int operation_error(char *message, operationContext *context)
{
    int error=errno;
    if (context->skip_all) return OPERATION_SKIP;
    int button=show_dialog(error ? SPRINTF("%s\n%s (%d)", message, strerror(error), error) : message,
        (char *[]) {"Skip", "Skip all", "Retry", "Abort", NULL}, 0, NULL, 1, 0, 0);
    if (button == 3) return OPERATION_RETRY;
    if (button == 4) { context->abort=1; return OPERATION_ABORT; }
    if (button == 2) context->skip_all=1;
    context->keep_item_selected=1;
    return OPERATION_SKIP;
}

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

    if (ret == OPERATION_OK || ret == OPERATION_SKIP) return ret;
    if (ret != OPERATION_PARENT_OK_PROCESS_CHILDS && ret != OPERATION_RETRY_AFTER_CHILDS) return ret;
    if (ret == OPERATION_PARENT_OK_PROCESS_CHILDS || ret == OPERATION_RETRY_AFTER_CHILDS) {
        // Recursive operation on a directory is needed for further processing
        struct stat statbuf = {0};
        if (lstat(src, &statbuf) != 0) return operation_error(SPRINTF("Cannot stat:\n%s", src), context);
        if (S_ISDIR(statbuf.st_mode)) {
            DIR *dir = opendir(src);
            while (!dir)
            {
                int decision=operation_error(SPRINTF("Cannot read directory:\n%s", src), context);
                if (decision != OPERATION_RETRY) return decision;
                dir=opendir(src);
            }
            struct dirent *entry;
            int result=OPERATION_OK;
            for (;;) {
                errno=0;
                entry=readdir(dir);
                if (!entry) { if (errno) result=operation_error(SPRINTF("Cannot read directory:\n%s", src), context); break; }
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                char source_path[CMD_MAX];
                char target_path[CMD_MAX];
                sprintf(source_path, "%s%s%s", src, src[strlen(src) - 1] == '/' ? "" : "/", entry->d_name);
                sprintf(target_path, "%s%s%s", tgt, *tgt && tgt[strlen(tgt) - 1] == '/' ? "" : "/", entry->d_name);
                int child_result=recursive_operation(source_path, target_path, context, operation);
                if (child_result != OPERATION_OK) result=child_result;
                if (context->abort == 1) { closedir(dir); return OPERATION_ABORT; }
            }
            closedir(dir);
            if (result != OPERATION_OK) { context->keep_item_selected=1; return result; }
            if (ret == OPERATION_RETRY_AFTER_CHILDS) {
                // try again the initial src
                ret = operation(src, tgt, context);
                if (context->abort == 1) return OPERATION_ABORT;
                if (ret != OPERATION_OK) return ret;
            }
        }
    }

    // A cross-filesystem move must preserve metadata before removing any source.
    if (operation == copy_operation && context->moving)
    {
        for (;;)
        {
            struct stat source;
            if (lstat(src, &source) == 0)
            {
                struct timespec times[]={source.st_atim, source.st_mtim};
                if (lchown(tgt, source.st_uid, source.st_gid) == 0 &&
                    (S_ISLNK(source.st_mode) || chmod(tgt, source.st_mode & 07777) == 0) &&
                    utimensat(AT_FDCWD, tgt, times, AT_SYMLINK_NOFOLLOW) == 0) break;
            }
            int decision=operation_error(SPRINTF("Cannot preserve metadata:\n%s", tgt), context);
            if (decision != OPERATION_RETRY) return decision;
        }
    }

    return OPERATION_OK;
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
    sprintf(infotext, "Items: %lld\nSize: %s bytes", (long long)context->total_items, num);

    int delta = update_progress_dialog_delta(SPRINTF("Scanning %s", src), 0, 0, infotext);
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
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {

        struct stat statbuf;
        ret = lstat(src, &statbuf);
        if (ret != 0) {
            ret=operation_error(SPRINTF("Stat failed for \"%s\"", src), context);
            if (ret != OPERATION_RETRY) return ret;
            continue;
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
                    btn = show_dialog(title, (char *[]) {"Yes", "No", "All", "None", "Abort", NULL}, 0, NULL, 1, 0, 0);
                }

                if (btn == 1) { // yes
                    if (!prefix_already_matches) {
                        sprintf(context->confirm_yes_prefix, "%s", src);
                    }
                    return OPERATION_RETRY_AFTER_CHILDS;
                } else if (btn <= 0 || btn == 2) { // no
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
                ret=operation_error(SPRINTF("Cannot remove \"%s\"", src), context);
                if (ret != OPERATION_RETRY) return ret;
            }
        } else {
            if (unlink(src) == 0) return OPERATION_OK;
            ret=operation_error(SPRINTF("Cannot remove \"%s\"", src), context);
            if (ret != OPERATION_RETRY) return ret;
        }
    }
    return OPERATION_OK;
}


int copy_operation(const char *src, const char *tgt, operationContext *context) {
    int ret = OPERATION_RETRY;
    errno = 0; // reset

    int delta = update_progress_dialog_delta(SPRINTF("Copying\n%s\nTo\n%s", src, tgt), 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {

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

            // Reject identical entries before overwriting or descending into directories.
            if (target_exists && statbufsrc.st_dev == statbuftgt.st_dev && statbufsrc.st_ino == statbuftgt.st_ino)
            {
                errno=EINVAL;
                snprintf(errmsg, sizeof(errmsg), "Source and target are the same file:\n%s", src);
                break;
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
                        int decision=confirm_overwrite(tgt, context);
                        if (decision != OPERATION_OK) { close(src_fd); return decision; }
                        tgt_fd=open(tgt, O_WRONLY | O_CREAT, statbufsrc.st_mode);
                        if (tgt_fd == -1) {
                            close(src_fd);
                            sprintf(errmsg,"Cannot open target file for writing:\n%s", tgt);
                            break;
                        }
                    } else {
                        close(src_fd);
                        sprintf(errmsg,"Cannot open target file for writing:\n%s", tgt);
                        break;
                    }
                }

                // Compare opened files too: the target may be a symlink or change after lstat.
                struct stat opened_src, opened_tgt;
                if (fstat(src_fd, &opened_src) != 0 || fstat(tgt_fd, &opened_tgt) != 0)
                    snprintf(errmsg, sizeof(errmsg), "Cannot stat opened files:\n%s\n%s", src, tgt);
                else if (opened_src.st_dev == opened_tgt.st_dev && opened_src.st_ino == opened_tgt.st_ino)
                {
                    errno=EINVAL;
                    snprintf(errmsg, sizeof(errmsg), "Source and target are the same file:\n%s", src);
                }
                else if (ftruncate(tgt_fd, 0) != 0)
                    snprintf(errmsg, sizeof(errmsg), "Cannot truncate target file:\n%s", tgt);

                if (errmsg[0])
                {
                    close(src_fd);
                    close(tgt_fd);
                    break;
                }

                char buffer[16384];
                ssize_t bytes = 0;
                ssize_t total_bytes = 0;
                while ((bytes = read(src_fd, buffer, sizeof(buffer))) > 0) {
                    ssize_t offset=0;
                    while (offset < bytes)
                    {
                        ssize_t written=write(tgt_fd, buffer+offset, bytes-offset);
                        if (written < 0 && errno == EINTR) continue;
                        if (written <= 0) { if (!written) errno=EIO; break; }
                        offset+=written;
                    }
                    if (offset != bytes) {
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
                        int answer = show_dialog(SPRINTF("Incomplete file was retrieved. Keep it?\n%s", tgt), (char *[]) {"Keep it", "Delete", NULL}, 1, NULL, 1, 0, 0);
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

                update_progress_dialog_delta(SPRINTF("Copying\n%s\nTo\n%s", src, tgt), statbufsrc.st_size > 0 ? total_bytes * 100 / statbufsrc.st_size : 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);

                int finish_error=context->moving && fsync(tgt_fd) != 0 ? errno : 0;
                close(src_fd);
                if (close(tgt_fd) != 0 && !finish_error) finish_error=errno;
                if (finish_error) { errno=finish_error; sprintf(errmsg,"Cannot finish writing:\n%s", tgt); break; }
                ret = 0;
            }
            // source is a directory
            else if (S_ISDIR(statbufsrc.st_mode)) {
                if (target_exists && S_ISDIR(statbuftgt.st_mode)) {
                    // do not overwrite existing directory
                    ret = 0;
                } else if (mkdir(tgt, statbufsrc.st_mode | (context->moving ? S_IRWXU : 0)) == -1) {
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
                    int decision=confirm_overwrite(tgt, context);
                    if (decision != OPERATION_OK) return decision;
                    if (unlink(tgt) == -1) {
                        sprintf(errmsg, "Failed to remove existing target file\n%s", tgt);
                        break;
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
            else if (S_ISFIFO(statbufsrc.st_mode)) {
                if (mkfifo(tgt, statbufsrc.st_mode) != 0) { sprintf(errmsg,"Cannot create FIFO:\n%s", tgt); break; }
            }
            else { errno=ENOTSUP; sprintf(errmsg,"Unsupported file type:\n%s", src); }
        } while (false);


        if (errmsg[0]) {
            ret=operation_error(errmsg, context);
            if (ret == OPERATION_RETRY) continue;
            return ret;
        }

        return OPERATION_PARENT_OK_PROCESS_CHILDS;
    }

    return 0;
}


int move_operation(const char *src, const char *tgt, operationContext *context) {
    int ret = OPERATION_RETRY;
    errno = 0; // reset

    int delta = update_progress_dialog_delta(SPRINTF("Renaming\n%s\nTo\n%s", src, tgt), 0, context->total_items > 0 ? context->current_items * 100 / context->total_items : 0, NULL);
    if (delta == 2) {
        context->abort = 1;
        return OPERATION_ABORT;
    }

    while (ret == OPERATION_RETRY) {
        // Prefer atomic no-replace without requiring recent glibc or kernel headers.
        ret=-1;
        errno=ENOSYS;
#if defined(SYS_renameat2) && defined(RENAME_NOREPLACE)
        ret=syscall(SYS_renameat2, AT_FDCWD, src, AT_FDCWD, tgt, RENAME_NOREPLACE);
#endif
        int rename_unsupported=errno == ENOSYS || errno == EINVAL || errno == EOPNOTSUPP;
        if (ret != 0 && rename_unsupported)
        {
            // Portable fallback: another process can create the target after this check.
            struct stat target_stat;
            if (lstat(tgt, &target_stat) == 0) errno=EEXIST;
            else if (errno == ENOENT) ret=rename(src, tgt);
        }
        if (ret != 0 && errno == EEXIST)
        {
            int decision=confirm_overwrite(tgt, context);
            if (decision != OPERATION_OK) return decision;
            ret=rename(src, tgt);
        }
        if (ret == 0) return OPERATION_OK;
        if (errno == EXDEV)
        {
            operationContext copy=*context;
            copy.moving=1;
            copy.keep_item_selected=0;
            if (copy.current_items) copy.current_items--;
            ret=recursive_operation(src, tgt, &copy, copy_operation);
            *context=copy;
            if (ret != OPERATION_OK || copy.abort || copy.keep_item_selected)
            { context->keep_item_selected=1; return ret == OPERATION_OK ? OPERATION_SKIP : ret; }
            operationContext remove={.confirm_all_yes=1, .skip_all=context->skip_all};
            ret=recursive_operation(src, "", &remove, delete_operation);
            context->abort=remove.abort;
            context->skip_all=remove.skip_all;
            if (ret != OPERATION_OK) context->keep_item_selected=1;
            return ret;
        }
        ret=operation_error(SPRINTF("Failed to rename\n%s\nTo\n%s", src, tgt), context);
        if (ret != OPERATION_RETRY) return ret;
    }
    return OPERATION_OK;
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
    return lstat(path, &info) == 0;
}

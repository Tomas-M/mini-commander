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

#include "types.h"
#include "globals.h"


int panel_action(OperationFunc func) {
    int err = 0;
    char source_path[CMD_MAX] = {0};
    char target_path[CMD_MAX] = {0};
    PanelProp * inactive_panel = active_panel == &left_panel ? &right_panel : &left_panel;

    if (active_panel->num_selected_files == 0) {
        sprintf(source_path, "%s/%s", active_panel->path, active_panel->file_under_cursor);
        sprintf(target_path, "%s/%s", inactive_panel->path, active_panel->file_under_cursor);
        operationContext context = {0};
        err = func(source_path, target_path, &context);
    } else {
        FileNode *temp = active_panel->files;
        while (temp != NULL) {
            if (temp->is_selected) {
                sprintf(source_path, "%s/%s", active_panel->path, temp->name);
                sprintf(target_path, "%s/%s", inactive_panel->path, temp->name);
                operationContext context = {0};
                err = func(source_path, target_path, &context);
                if (err) {
                    break;
                } else {
                    temp->is_selected = 0;
                    active_panel->num_selected_files--;
                }
            }
            temp = temp->next;
        }
    }
    update_files_in_both_panels();
    return 0;
}



int recursive_operation(const char *src, const char *tgt, operationContext *context, OperationFunc func) {
    struct stat statbuf;
    if (lstat(src, &statbuf) == -1) return -1;

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
            recursive_operation(source_path, target_path, context, func);
        }
        closedir(dir);
        int ret = func(src, tgt, context);
    } else {
        int ret = func(src, tgt, context);
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
    if (lstat(src, &statbuf) != -1) {
        context->total_items++;
        if (S_ISDIR(statbuf.st_mode)) {
           context->total_size += statbuf.st_size;
           ret = rmdir(src);
        } else {
           ret = unlink(src);
        }
    }
    return ret;
}


int copy_operation(const char *src, const char *tgt, operationContext *context) {
}

int move_operation(const char *src, const char *tgt, operationContext *context) {
}


//mkdir_recursive("/usr/bin/something", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

int mkdir_recursive(const char *path, mode_t mode) {
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
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}


int is_directory_empty(const char *dirname) {
    int n = 0;
    struct dirent *d;
    DIR *dir = opendir(dirname);
    if (dir == NULL) //Not a directory or doesn't exist
        return 1;
    while ((d = readdir(dir)) != NULL) {
        if(++n > 2)
            break;
    }
    closedir(dir);
    if (n <= 2) //Directory Empty
        return 1;
    else
        return 0;
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

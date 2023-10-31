#include "includes.h"
#include "types.h"
#include "globals.h"

int compare_nodes(FileNode *a, FileNode *b, SortOrders sort_order) {
    int result = 0;
    int dirs_first = sort_order >= SORT_BY_NAME_DIRSFIRST_ASC;

    // Check if either node is ".."
    if (strcmp(a->name, "..") == 0) return -1;
    if (strcmp(b->name, "..") == 0) return 1;

    if (dirs_first && (a->is_dir != b->is_dir)) {
        return a->is_dir ? -1 : 1;
    }

    switch (sort_order % 6) {  // 6 because there are 6 basic sort types
        case SORT_BY_NAME_ASC:
        case SORT_BY_NAME_DESC:
            result = strcmp(a->name, b->name);
            break;
        case SORT_BY_SIZE_ASC:
        case SORT_BY_SIZE_DESC:
            result = (a->size > b->size) - (a->size < b->size);
            break;
        case SORT_BY_TIME_ASC:
        case SORT_BY_TIME_DESC:
            result = (a->mtime > b->mtime) - (a->mtime < b->mtime);
            break;
    }

    // DESC sort? revert result
    if (sort_order == SORT_BY_NAME_DESC || sort_order == SORT_BY_SIZE_DESC || sort_order == SORT_BY_TIME_DESC ||
        sort_order == SORT_BY_NAME_DIRSFIRST_DESC || sort_order == SORT_BY_SIZE_DIRSFIRST_DESC || sort_order == SORT_BY_TIME_DIRSFIRST_DESC) {
        result = -result;
    }

    return result;
}


void sort_file_nodes(FileNode **head_ref, SortOrders sort_order) {
    FileNode *sorted = NULL;
    FileNode *current = *head_ref;

    while (current != NULL) {
        FileNode *next = current->next;

        if (sorted == NULL || compare_nodes(current, sorted, sort_order) <= 0) {
            current->next = sorted;
            sorted = current;
        } else {
            FileNode *temp = sorted;
            while (temp->next != NULL && compare_nodes(current, temp->next, sort_order) > 0) {
                temp = temp->next;
            }
            current->next = temp->next;
            temp->next = current;
        }

        current = next;
    }

    *head_ref = sorted;
}


int update_panel_files(PanelProp *panel) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    struct stat link_stat;
    FileNode *head = NULL, *current = NULL, *original_head = NULL;

    original_head = panel->files;
    panel->files = NULL;
    panel->files_count = 0;
    panel->num_selected_files = 0;
    panel->bytes_selected_files = 0;

    if ((dir = opendir(panel->path)) == NULL) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0 && strcmp(panel->path, "/") == 0) continue;

        char full_path[CMD_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", panel->path, entry->d_name);
        lstat(full_path, &file_stat);

        FileNode *new_node = (FileNode*) calloc(1,sizeof(FileNode));

        panel->files_count++;
        new_node->next = NULL;
        new_node->name = strdup(entry->d_name);

        new_node->mtime = file_stat.st_mtime;
        new_node->size = file_stat.st_size;
        new_node->chmod = file_stat.st_mode;
        new_node->chown = file_stat.st_uid;
        new_node->is_dir = S_ISDIR(file_stat.st_mode);
        new_node->is_executable = (file_stat.st_mode & S_IXUSR) || (file_stat.st_mode & S_IXGRP) || (file_stat.st_mode & S_IXOTH);
        new_node->is_link = S_ISLNK(file_stat.st_mode);
        new_node->is_link_broken = 0;
        new_node->is_link_to_dir = 0;
        new_node->is_device = S_ISBLK(file_stat.st_mode) || S_ISCHR(file_stat.st_mode);

        if (new_node->is_link) {
            new_node->link_target = NULL;
            char target[CMD_MAX];
            ssize_t len = readlink(full_path, target, sizeof(target) - 1);
            if (len != -1) {
                target[len] = '\0';
                new_node->link_target = strdup(target);
            }

            if (stat(full_path, &link_stat) != 0) {
                new_node->is_link_broken = 1;  // Link is broken
            } else {
                new_node->is_link_to_dir = S_ISDIR(link_stat.st_mode);
            }
        }

        if (new_node->is_link_to_dir) new_node->is_dir = 1;

        // Check if this file was selected in the original list
        FileNode *old_node = original_head;
        while (old_node != NULL) {
            if (old_node->is_selected && strcmp(new_node->name, old_node->name) == 0) {
                new_node->is_selected = true;
                panel->num_selected_files++;
                if (!new_node->is_dir) panel->bytes_selected_files+=new_node->size;
                break;
            }
            old_node = old_node->next;
        }

        if (head == NULL) {
            head = new_node;
            current = head;
        } else {
            current->next = new_node;
            current = new_node;
        }
    }

    closedir(dir);
    panel->files = head;

    free_file_nodes(original_head);

    return panel->files_count;
}

void free_file_nodes(FileNode *head) {
    FileNode *tmp;
    while (head != NULL) {
        free(head->name);
        free(head->link_target);
        tmp = head;
        head = head->next;
        free(tmp);
    }
}

void dive_into_directory(FileNode *current) {
   if (strcmp(current->name, "..") == 0) {
       // Store the last directory name before going up
       char * last_slash = strrchr(active_panel->path, '/');
       strncpy(active_panel->file_under_cursor, last_slash + 1, CMD_MAX - 1);

       // Go back to upper dir
       last_slash = strrchr(active_panel->path, '/');
       int is_root = (last_slash == active_panel->path);
       memset(last_slash + is_root, 0, strlen(last_slash));
   } else {
       // Dive into the selected directory
       if (strlen(active_panel->path) > 1) strcat(active_panel->path, "/");
       strcat(active_panel->path, current->name);
       active_panel->file_under_cursor[0] = '\0';
   }

   free_file_nodes(active_panel->files);
   active_panel->files = NULL;
   active_panel->num_selected_files = 0;
   active_panel->bytes_selected_files = 0;
   active_panel->files_count = 0;

   // Update the file list for the new directory
   update_panel_files(active_panel);
   sort_file_nodes(&active_panel->files, active_panel->sort_order);
   update_panel_cursor();
}


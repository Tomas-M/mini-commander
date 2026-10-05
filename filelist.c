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

    int order=sort_order%6; // Name, size, time; then the same three in reverse.
    switch (order%3) {
        case SORT_BY_NAME_ASC:
            // Keep dot-prefixed names first in both sort directions.
            if ((a->name[0] == '.') != (b->name[0] == '.')) return a->name[0] == '.' ? -1 : 1;
            result = strcmp(a->name, b->name);
            break;
        case SORT_BY_SIZE_ASC:
            result = (a->size > b->size) - (a->size < b->size);
            break;
        case SORT_BY_TIME_ASC:
            result = (a->mtime > b->mtime) - (a->mtime < b->mtime);
            break;
    }

    return order >= 3 ? -result : result;
}


void sort_file_nodes(FileNode **head_ref, SortOrders sort_order) {
    FileNode *left=*head_ref;
    if (!left || !left->next) return;
    FileNode *slow=left, *fast=left->next;
    while (fast && fast->next) { slow=slow->next; fast=fast->next->next; }
    FileNode *right=slow->next;
    slow->next=NULL;
    sort_file_nodes(&left, sort_order);
    sort_file_nodes(&right, sort_order);
    FileNode **tail=head_ref;
    while (left && right)
    {
        FileNode **next=compare_nodes(left, right, sort_order) <= 0 ? &left : &right;
        *tail=*next;
        *next=(*next)->next;
        tail=&(*tail)->next;
    }
    *tail=left ? left : right;
}


int update_panel_files(PanelProp *panel) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    struct stat link_stat;
    FileNode *head = NULL, *current = NULL, *original_head = NULL;

    if ((dir = opendir(panel->path)) == NULL) return -1;
    int had_selection=panel->num_selected_files;
    original_head = panel->files;
    panel->files = NULL;
    panel->files_count = 0;
    panel->num_selected_files = 0;
    panel->bytes_selected_files = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0 && strcmp(panel->path, "/") == 0) continue;

        char full_path[CMD_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", panel->path, entry->d_name);
        if (lstat(full_path, &file_stat) != 0) continue;

        FileNode *new_node = (FileNode*) calloc(1,sizeof(FileNode));
        if (!new_node) break;

        panel->files_count++;
        new_node->name = strdup(entry->d_name);
        if (!new_node->name) { free(new_node); panel->files_count--; break; }

        new_node->mtime = file_stat.st_mtime;
        new_node->size = file_stat.st_size;
        new_node->chmod = file_stat.st_mode;
        new_node->chown = file_stat.st_uid;
        new_node->is_dir = S_ISDIR(file_stat.st_mode);
        new_node->is_executable = (file_stat.st_mode & S_IXUSR) || (file_stat.st_mode & S_IXGRP) || (file_stat.st_mode & S_IXOTH);
        new_node->is_link = S_ISLNK(file_stat.st_mode);
        new_node->is_device = S_ISBLK(file_stat.st_mode) || S_ISCHR(file_stat.st_mode);

        if (new_node->is_link) {
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
        FileNode *old_node = had_selection ? original_head : NULL;
        while (old_node != NULL) {
            if (old_node->is_selected && strcmp(new_node->name, old_node->name) == 0) {
                select_file(panel, new_node, 1);
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
       last_slash[last_slash == active_panel->path]='\0';
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


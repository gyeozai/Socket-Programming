#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

void count_files_in_directory(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    struct dirent *entry;
    int file_count = 0;

    if (!dir) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {  // Count regular files
            file_count++;
        }
    }

    printf("Directory: %s, Files: %d\n", dir_path, file_count);
    closedir(dir);
}

void list_directories(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;

    if (!dir) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        // Skip '.' and '..' entries
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR) {  // Check if it's a directory
            // printf("Directory: %s\n", full_path);
            count_files_in_directory(full_path);  // Count files in the directory
        }
    }
    closedir(dir);
}

int main() {
    const char *path = "videos";  // Current directory
    list_directories(path);
    return 0;
}

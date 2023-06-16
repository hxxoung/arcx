#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

struct file_header {
    char filename[255];
    long filesize;
};

void pack(char* archive_name, char* source_dir);
void unpack(char* archive_name, char* dest_dir);
void add(char* archive_name, char* target_filename);
void del(char* archive_name, char* target_filename);
void list(char* archive_name);

void pack(char* archive_name, char* source_dir) {
    // Open the directory
    DIR *d;
    struct dirent *dir;
    d = opendir(source_dir);
    if (d == NULL) {
        printf("Error: Unable to open source directory\n");
        return;
    }

    // Open archive file
    FILE *archive = fopen(archive_name, "w");
    if (archive == NULL) {
        printf("Error: Unable to open archive file\n");
        return;
    }

    int file_count = 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) { // If the entry is a regular file
            file_count++;
            struct file_header header;

            // Store the name of the file
            strcpy(header.filename, dir->d_name);

            // Determine the size of the file
            struct stat st;
            char filepath[255];
            strcpy(filepath, source_dir);
            strcat(filepath, "/");
            strcat(filepath, dir->d_name);

            if (stat(filepath, &st) == 0) {
                header.filesize = st.st_size;
            } else {
                printf("Error: Unable to determine the size of file %s\n", dir->d_name);
                return;
            }

            // Write the header to the archive
            fwrite(&header, sizeof(struct file_header), 1, archive);

            // Write the contents of the file to the archive
            FILE *source_file = fopen(filepath, "r");
            if (source_file == NULL) {
                printf("Error: Unable to open source file %s\n", dir->d_name);
                return;
            }

            char *buffer = malloc(header.filesize);
            fread(buffer, header.filesize, 1, source_file);
            fwrite(buffer, header.filesize, 1, archive);

            free(buffer);
            fclose(source_file);
        }
    }

    printf("%d file(s) archived.\n", file_count);

    closedir(d);
    fclose(archive);
}

void unpack(char* archive_name, char* dest_dir) {
    // Open archive file
    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        printf("Error: Unable to open archive file\n");
        return;
    }

    // Check if destination directory exists, if not, create it
    struct stat st = {0};
    if (stat(dest_dir, &st) == -1) {
        mkdir(dest_dir, 0700);
    }

    // Read each file_header and corresponding file contents in the archive
    while(1) {
        struct file_header header;

        // Read a header from the archive
        if(fread(&header, sizeof(struct file_header), 1, archive) <= 0)
            break; // Break if there is nothing to read

        // Construct the file path
        char filepath[255];
        strcpy(filepath, dest_dir);
        strcat(filepath, "/");
        strcat(filepath, header.filename);

        // Open the destination file
        FILE *dest_file = fopen(filepath, "w");
        if (dest_file == NULL) {
            printf("Error: Unable to open destination file %s\n", filepath);
            return;
        }

        // Read the contents from the archive and write them to the destination file
        char *buffer = malloc(header.filesize);
        fread(buffer, header.filesize, 1, archive);
        fwrite(buffer, header.filesize, 1, dest_file);

        free(buffer);
        fclose(dest_file);
    }

    fclose(archive);
}

void del(char* archive_name, char* target_filename) {
    // Temporary file name for intermediate storage
    char temp_archive_name[] = "temp.arcx";

    // Open archive file
    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        printf("Error: Unable to open archive file\n");
        return;
    }

    // Open temporary archive file
    FILE *temp_archive = fopen(temp_archive_name, "w");
    if (temp_archive == NULL) {
        printf("Error: Unable to open temporary archive file\n");
        return;
    }

    int file_found = 0;

    // Read each file_header and corresponding file contents in the archive
    while(1) {
        struct file_header header;

        // Read a header from the archive
        if(fread(&header, sizeof(struct file_header), 1, archive) <= 0)
            break; // Break if there is nothing to read

        // If this is the file to delete, skip it
        if(strcmp(header.filename, target_filename) == 0) {
            file_found = 1;
            // Skip over the file contents
            fseek(archive, header.filesize, SEEK_CUR);
            continue;
        }

        // Write the header to the temporary archive
        fwrite(&header, sizeof(struct file_header), 1, temp_archive);

        // Read the contents from the archive and write them to the temporary archive
        char *buffer = malloc(header.filesize);
        fread(buffer, header.filesize, 1, archive);
        fwrite(buffer, header.filesize, 1, temp_archive);

        free(buffer);
    }

    fclose(archive);
    fclose(temp_archive);

    if (file_found) {
        // If the file to delete was found, replace the original archive with the temporary archive
        remove(archive_name);
        rename(temp_archive_name, archive_name);
        printf("File %s deleted.\n", target_filename);
    } else {
        // If the file to delete was not found, remove the temporary archive
        remove(temp_archive_name);
        printf("No such file exists.\n");
    }
}

void add(char* archive_name, char* target_filename) {
    // Open the archive in append mode
    FILE *archive = fopen(archive_name, "a+");
    if (archive == NULL) {
        printf("Error: Unable to open archive file\n");
        return;
    }

    // Check if the file already exists in the archive
    struct file_header tmp_header;
    while(fread(&tmp_header, sizeof(struct file_header), 1, archive) > 0) {
        if(strcmp(tmp_header.filename, basename(target_filename)) == 0) {
            printf("Same file name already exists\n");
            fclose(archive);
            return;
        }

        // Skip the file content
        fseek(archive, tmp_header.filesize, SEEK_CUR);
    }

    // Open the file to be added
    FILE *target_file = fopen(target_filename, "r");
    if (target_file == NULL) {
        printf("Error: Unable to open target file\n");
        fclose(archive);
        return;
    }

    // Create a file_header for the file to be added
    struct file_header header;
    strcpy(header.filename, basename(target_filename));

    // Find the size of the file to be added
    fseek(target_file, 0, SEEK_END);
    header.filesize = ftell(target_file);
    rewind(target_file);

    // Write the file_header to the archive
    fwrite(&header, sizeof(struct file_header), 1, archive);

    // Read the file contents and write them to the archive
    char *buffer = malloc(header.filesize);
    fread(buffer, header.filesize, 1, target_file);
    fwrite(buffer, header.filesize, 1, archive);

    free(buffer);
    fclose(target_file);
    fclose(archive);

    printf("File %s added.\n", target_filename);
}

void list(char* archive_name) {
    // Open the archive in read mode
    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        printf("Error: Unable to open archive file\n");
        return;
    }

    int total_files = 0;

    // Read file headers until end of the archive
    struct file_header header;
    while(fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        printf("%s %ld bytes\n", header.filename, header.filesize);
        total_files++;

        // Skip the file content
        fseek(archive, header.filesize, SEEK_CUR);
    }

    printf("Total %d file(s) exist.\n", total_files);
    fclose(archive);
}


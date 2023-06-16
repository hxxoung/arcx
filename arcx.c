#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>
#include <errno.h>

#define MAX_PATH_LENGTH 255

struct file_header {
    char filename[MAX_PATH_LENGTH];
    long filesize;
};

void pack(char* archive_name, char* source_dir);
void unpack(char* archive_name, char* dest_dir);
void add(char* archive_name, char* target_filename);
void del(char* archive_name, char* target_filename);
void list(char* archive_name);

int copy_data(FILE *src, FILE *dest, long size);
int file_exists_in_archive(FILE *archive, char *filename);


void unpack(char* archive_name, char* dest_dir) {
    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    struct stat st = {0};
    if (stat(dest_dir, &st) == -1) {
        if (mkdir(dest_dir, 0700)) {
            fprintf(stderr, "Error: Unable to create destination directory. Error code: %d\n", errno);
            fclose(archive);
            return;
        }
    }

    struct file_header header;
    while(fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        char filepath[255];
        sprintf(filepath, "%s/%s", dest_dir, header.filename);

        FILE *dest_file = fopen(filepath, "w");
        if (dest_file == NULL) {
            fprintf(stderr, "Error: Unable to open destination file %s. Error code: %d\n", filepath, errno);
            fclose(archive);
            return;
        }

        char *buffer = malloc(header.filesize);
        if (buffer == NULL) {
            fprintf(stderr, "Error: Unable to allocate memory for file content buffer\n");
            fclose(dest_file);
            fclose(archive);
            return;
        }

        if (fread(buffer, header.filesize, 1, archive) <= 0) {
            fprintf(stderr, "Error: Unable to read file content for %s\n", header.filename);
            free(buffer);
            fclose(dest_file);
            fclose(archive);
            return;
        }

        if (fwrite(buffer, header.filesize, 1, dest_file) <= 0) {
            fprintf(stderr, "Error: Unable to write file content to %s\n", filepath);
        }

        free(buffer);
        fclose(dest_file);
    }

    if (ferror(archive)) {
        fprintf(stderr, "Error: Failed to read from archive file\n");
    }

    fclose(archive);
}


void pack(char* archive_name, char* source_dir) {
    DIR *d;
    struct dirent *dir;
    d = opendir(source_dir);
    if (d == NULL) {
        fprintf(stderr, "Error: Unable to open source directory\n");
        return;
    }

    FILE *archive = fopen(archive_name, "w");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        closedir(d);
        return;
    }

    int file_count = 0;
    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_REG) {
            file_count++;
            struct file_header header;

            if (strlen(dir->d_name) >= sizeof(header.filename)) {
                fprintf(stderr, "Error: File name %s is too long\n", dir->d_name);
                continue;
            }

            strcpy(header.filename, dir->d_name);

            struct stat st;
            char filepath[PATH_MAX];
            if (snprintf(filepath, sizeof(filepath), "%s/%s", source_dir, dir->d_name) >= sizeof(filepath)) {
                fprintf(stderr, "Error: File path %s/%s is too long\n", source_dir, dir->d_name);
                continue;
            }

            if (stat(filepath, &st) != 0) {
                fprintf(stderr, "Error: Unable to determine the size of file %s\n", dir->d_name);
                continue;
            }

            header.filesize = st.st_size;

            fwrite(&header, sizeof(struct file_header), 1, archive);

            FILE *source_file = fopen(filepath, "r");
            if (source_file == NULL) {
                fprintf(stderr, "Error: Unable to open source file %s\n", dir->d_name);
                continue;
            }

            char *buffer = malloc(header.filesize);
            if (buffer == NULL) {
                fprintf(stderr, "Error: Unable to allocate memory for file %s\n", dir->d_name);
                fclose(source_file);
                continue;
            }

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

void del(char* archive_name, char* target_filename) {
    char temp_archive_name[] = "temp.arcx";

    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    FILE *temp_archive = fopen(temp_archive_name, "w");
    if (temp_archive == NULL) {
        fprintf(stderr, "Error: Unable to open temporary archive file\n");
        fclose(archive);
        return;
    }

    int file_found = 0;
    struct file_header header;
    while(fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        if(strcmp(header.filename, target_filename) == 0) {
            file_found = 1;
            fseek(archive, header.filesize, SEEK_CUR);
            continue;
        }

        fwrite(&header, sizeof(struct file_header), 1, temp_archive);

        char *buffer = malloc(header.filesize);
        if (buffer == NULL) {
            fprintf(stderr, "Error: Unable to allocate memory for file %s\n", header.filename);
            fclose(archive);
            fclose(temp_archive);
            remove(temp_archive_name);
            return;
        }

        fread(buffer, header.filesize, 1, archive);
        fwrite(buffer, header.filesize, 1, temp_archive);

        free(buffer);
    }

    fclose(archive);
    fclose(temp_archive);

    if (file_found) {
        remove(archive_name);
        rename(temp_archive_name, archive_name);
        printf("File %s deleted.\n", target_filename);
    } else {
        remove(temp_archive_name);
        printf("No such file %s exists.\n", target_filename);
    }
}


void list(char* archive_name) {
    FILE *archive = fopen(archive_name, "r");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    int total_files = 0;
    struct file_header header;
    while(fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        printf("%s %ld bytes\n", header.filename, header.filesize);
        total_files++;

        if (fseek(archive, header.filesize, SEEK_CUR)) {
            fprintf(stderr, "Error: Unable to skip file content for %s\n", header.filename);
            fclose(archive);
            return;
        }
    }

    if (ferror(archive)) {
        fprintf(stderr, "Error: Failed to read from archive file\n");
    } else {
        printf("Total %d file(s) exist.\n", total_files);
    }

    fclose(archive);
}


int copy_data(FILE *src, FILE *dest, long size) {
    char *buffer = malloc(size);
    if(!buffer) {
        perror("Error: Unable to allocate buffer");
        return 0;
    }

    if(fread(buffer, size, 1, src) != 1) {
        perror("Error: Unable to read source data");
        free(buffer);
        return 0;
    }

    if(fwrite(buffer, size, 1, dest) != 1) {
        perror("Error: Unable to write destination data");
        free(buffer);
        return 0;
    }

    free(buffer);
    return 1;
}

int file_exists_in_archive(FILE *archive, char *filename) {
    rewind(archive);

    struct file_header tmp_header;
    while(fread(&tmp_header, sizeof(struct file_header), 1, archive) > 0) {
        if(strncmp(tmp_header.filename, filename, MAX_PATH_LENGTH) == 0) {
            return 1;
        }
        fseek(archive, tmp_header.filesize, SEEK_CUR);
    }
    return 0;
}

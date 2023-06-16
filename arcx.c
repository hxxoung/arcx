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

void fclose_safe(FILE *file);

void unpack(char* archive_name, char* dest_dir) {
    FILE *archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    struct stat st = {0};
    if (stat(dest_dir, &st) == -1) {
        if (mkdir(dest_dir, 0700)) {
            fprintf(stderr, "Error: Unable to create destination directory. Error code: %d\n", errno);
            fclose_safe(archive);
            return;
        }
    }

    struct file_header header;
    while (fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        char filepath[MAX_PATH_LENGTH + 1];
        snprintf(filepath, sizeof(filepath), "%s/%s", dest_dir, header.filename);

        FILE *dest_file = fopen(filepath, "wb");
        if (dest_file == NULL) {
            fprintf(stderr, "Error: Unable to open destination file %s. Error code: %d\n", filepath, errno);
            fclose_safe(archive);
            return;
        }

        char *buffer = malloc(header.filesize);
        if (buffer == NULL) {
            fprintf(stderr, "Error: Unable to allocate memory for file content buffer\n");
            fclose_safe(dest_file);
            fclose_safe(archive);
            return;
        }

        if (fread(buffer, header.filesize, 1, archive) <= 0) {
            fprintf(stderr, "Error: Unable to read file content for %s\n", header.filename);
            free(buffer);
            fclose_safe(dest_file);
            fclose_safe(archive);
            return;
        }

        if (fwrite(buffer, header.filesize, 1, dest_file) <= 0) {
            fprintf(stderr, "Error: Unable to write file content to %s\n", filepath);
        }

        free(buffer);
        fclose_safe(dest_file);
    }

    if (ferror(archive)) {
        fprintf(stderr, "Error: Failed to read from archive file\n");
    }

    fclose_safe(archive);
}

void pack(char* archive_name, char* source_dir) {
    DIR *d;
    struct dirent *dir;
    d = opendir(source_dir);
    if (d == NULL) {
        fprintf(stderr, "Error: Unable to open source directory\n");
        return;
    }

    FILE *archive = fopen(archive_name, "wb");
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

            if (strlen(dir->d_name) >= MAX_PATH_LENGTH) {
                fprintf(stderr, "Error: File name %s is too long\n", dir->d_name);
                continue;
            }

            strcpy(header.filename, dir->d_name);

            struct stat st;
            char filepath[MAX_PATH_LENGTH + 1];
            snprintf(filepath, sizeof(filepath), "%s/%s", source_dir, dir->d_name);

            if (stat(filepath, &st) != 0) {
                fprintf(stderr, "Error: Unable to determine the size of file %s\n", dir->d_name);
                continue;
            }

            header.filesize = st.st_size;

            fwrite(&header, sizeof(struct file_header), 1, archive);

            FILE *source_file = fopen(filepath, "rb");
            if (source_file == NULL) {
                fprintf(stderr, "Error: Unable to open source file %s\n", dir->d_name);
                continue;
            }

            char *buffer = malloc(header.filesize);
            if (buffer == NULL) {
                fprintf(stderr, "Error: Unable to allocate memory for file %s\n", dir->d_name);
                fclose_safe(source_file);
                continue;
            }

            fread(buffer, header.filesize, 1, source_file);
            fwrite(buffer, header.filesize, 1, archive);

            free(buffer);
            fclose_safe(source_file);
        }
    }

    printf("%d file(s) archived.\n", file_count);

    closedir(d);
    fclose_safe(archive);
}

void add(char* archive_name, char* target_filename) {
    FILE *archive = fopen(archive_name, "r+b");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    if (file_exists_in_archive(archive, target_filename)) {
        fprintf(stderr, "Error: File %s already exists in the archive\n", target_filename);
        fclose_safe(archive);
        return;
    }

    struct file_header header;
    struct stat st;
    char filepath[MAX_PATH_LENGTH + 1];
    snprintf(filepath, sizeof(filepath), "%s", target_filename);

    if (stat(filepath, &st) != 0) {
        fprintf(stderr, "Error: Unable to determine the size of file %s\n", target_filename);
        fclose_safe(archive);
        return;
    }

    strcpy(header.filename, basename(target_filename));
    header.filesize = st.st_size;

    fwrite(&header, sizeof(struct file_header), 1, archive);

    FILE *source_file = fopen(filepath, "rb");
    if (source_file == NULL) {
        fprintf(stderr, "Error: Unable to open source file %s\n", target_filename);
        fclose_safe(archive);
        return;
    }

    char *buffer = malloc(header.filesize);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Unable to allocate memory for file %s\n", target_filename);
        fclose_safe(source_file);
        fclose_safe(archive);
        return;
    }

    fread(buffer, header.filesize, 1, source_file);
    fwrite(buffer, header.filesize, 1, archive);

    free(buffer);
    fclose_safe(source_file);
    fclose_safe(archive);

    printf("File %s added to the archive.\n", target_filename);
}

void del(char* archive_name, char* target_filename) {
    char temp_archive_name[] = "temp.arcx";

    FILE *archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    FILE *temp_archive = fopen(temp_archive_name, "wb");
    if (temp_archive == NULL) {
        fprintf(stderr, "Error: Unable to open temporary archive file\n");
        fclose_safe(archive);
        return;
    }

    int file_found = 0;
    struct file_header header;
    while (fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        if (strcmp(header.filename, target_filename) == 0) {
            file_found = 1;
            fseek(archive, header.filesize, SEEK_CUR);
            continue;
        }

        fwrite(&header, sizeof(struct file_header), 1, temp_archive);

        char *buffer = malloc(header.filesize);
        if (buffer == NULL) {
            fprintf(stderr, "Error: Unable to allocate memory for file %s\n", header.filename);
            fclose_safe(archive);
            fclose_safe(temp_archive);
            remove(temp_archive_name);
            return;
        }

        fread(buffer, header.filesize, 1, archive);
        fwrite(buffer, header.filesize, 1, temp_archive);

        free(buffer);
    }

    fclose_safe(archive);
    fclose_safe(temp_archive);

    if (file_found) {
        remove(archive_name);
        rename(temp_archive_name, archive_name);
        printf("File %s deleted from the archive.\n", target_filename);
    } else {
        remove(temp_archive_name);
        printf("No such file %s exists in the archive.\n", target_filename);
    }
}

void list(char* archive_name) {
    FILE *archive = fopen(archive_name, "rb");
    if (archive == NULL) {
        fprintf(stderr, "Error: Unable to open archive file\n");
        return;
    }

    int total_files = 0;
    struct file_header header;
    while (fread(&header, sizeof(struct file_header), 1, archive) > 0) {
        printf("%s %ld bytes\n", header.filename, header.filesize);
        total_files++;

        if (fseek(archive, header.filesize, SEEK_CUR)) {
            fprintf(stderr, "Error: Unable to skip file content for %s\n", header.filename);
            fclose_safe(archive);
            return;
        }
    }

    if (ferror(archive)) {
        fprintf(stderr, "Error: Failed to read from archive file\n");
    } else {
        printf("Total %d file(s) exist in the archive.\n", total_files);
    }

    fclose_safe(archive);
}

int copy_data(FILE *src, FILE *dest, long size) {
    char *buffer = malloc(size);
    if (!buffer) {
        perror("Error: Unable to allocate buffer");
        return 0;
    }

    if (fread(buffer, size, 1, src) != 1) {
        perror("Error: Unable to read source data");
        free(buffer);
        return 0;
    }

    if (fwrite(buffer, size, 1, dest) != 1) {
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
    while (fread(&tmp_header, sizeof(struct file_header), 1, archive) > 0) {
        if (strcmp(tmp_header.filename, filename) == 0) {
            return 1;
        }
        fseek(archive, tmp_header.filesize, SEEK_CUR);
    }
    return 0;
}

void fclose_safe(FILE *file) {
    if (file != NULL) {
        fclose(file);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Error: Insufficient arguments\n");
        fprintf(stderr, "Usage: %s <command> <archive-filename> [additional arguments]\n", argv[0]);
        return 1;
    }

    char *command = argv[1];
    char *archive_name = argv[2];

    if (strcmp(command, "pack") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Error: Incorrect number of arguments for command 'pack'\n");
            fprintf(stderr, "Usage: %s pack <archive-filename> <src-directory>\n", argv[0]);
            return 1;
        }
        char *source_dir = argv[3];
        pack(archive_name, source_dir);
    } else if (strcmp(command, "unpack") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Error: Incorrect number of arguments for command 'unpack'\n");
            fprintf(stderr, "Usage: %s unpack <archive-filename> <dest-directory>\n", argv[0]);
            return 1;
        }
        char *dest_dir = argv[3];
        unpack(archive_name, dest_dir);
    } else if (strcmp(command, "add") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Error: Incorrect number of arguments for command 'add'\n");
            fprintf(stderr, "Usage: %s add <archive-filename> <target-filename>\n", argv[0]);
            return 1;
        }
        char *target_filename = argv[3];
        add(archive_name, target_filename);
    } else if (strcmp(command, "del") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Error: Incorrect number of arguments for command 'del'\n");
            fprintf(stderr, "Usage: %s del <archive-filename> <target-filename>\n", argv[0]);
            return 1;
        }
        char *target_filename = argv[3];
        del(archive_name, target_filename);
    } else if (strcmp(command, "list") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: Incorrect number of arguments for command 'list'\n");
            fprintf(stderr, "Usage: %s list <archive-filename>\n", argv[0]);
            return 1;
        }
        list(archive_name);
    } else {
        fprintf(stderr, "Error: Invalid command '%s'\n", command);
        fprintf(stderr, "Usage: %s <command> <archive-filename> [additional arguments]\n", argv[0]);
        return 1;
    }

    return 0;
}

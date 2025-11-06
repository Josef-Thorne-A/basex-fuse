
#include <basex/basexdbc.h>
#define FUSE_USE_VERSION 26
#include "uthash.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <err.h>
#define DBHOST "localhost"
#define DBPORT "1984"
#define DBUSER "admin"
#define DBPASSWD "test"

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

int sfd, rc;
#define MAX_DIR_ENTRIES INT_MAX

// need this to track file "sizes" since this doesn't quite make sense for
// basex.

struct FileSize {
    int size;
    const char *filename;
    UT_hash_handle hh; /* makes this structure hashable */
};

struct FileSize *filesizes = NULL;

static int bxfs_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    int size;

    const char *filename = path[0] == '/' ? path + 1 : path;

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    struct FileSize *fs;
    if (filesizes != NULL) {
        HASH_FIND_STR(filesizes, filename, fs);
        if (fs != NULL) {
            stbuf->st_size = fs->size;
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_nlink = 1;
            return 0;
        }
    }

    char command[4 + strlen(path)];
    sprintf(command, "GET %s", filename);
    char *result;
    char *info;
    const char *final_command = command;
    int retstat = basex_execute(sfd, final_command, &result, &info);
    if (retstat) {
        printf("File not found: %s\n", filename);
        return -ENOENT;
    }
    stbuf->st_size = strlen(result);

    fs = malloc(sizeof *fs);
    char *key = malloc((strlen(filename) + 1) * sizeof(char));
    strcpy(key, filename);
    fs->filename = key;
    fs->size = stbuf->st_size;
    HASH_ADD_STR(filesizes, filename, fs);
    stbuf->st_mode = S_IFREG | 0777;
    stbuf->st_nlink = 1;
    return 0;
}

char **get_dirs_from_db() {
    char *result;
    char *info;
    basex_execute(sfd, "DIR", &result, &info);
    int entrylength = 10;
    char **files = malloc(entrylength * sizeof(char *));
    char *line;
    char *filename;
    strsep(&result, "\n");
    strsep(&result, "\n");
    for (int i = 0; i < entrylength; i++) {
        line = strsep(&result, "\n");
        if (line == NULL)
            break;
        filename = strsep(&result, " ");
        if (filename[0] == '\n') {
            for (int j = i; j < entrylength; j++)
                files[i] = "";
            break;
        }
        files[i] = malloc(sizeof(char) * strlen(filename) + 1);
        strcpy(files[i], filename);
    }
    return files;
}

int free_entries(char **entries) {
    for (int i = 0; i < 9 && strcmp(entries[i], ""); i++) {
        if (entries[i] != NULL)
            free(entries[i]);
    }

    free(entries);
    return 0;
}

int bxfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                 off_t offset, struct fuse_file_info *fi) {
    int retstat = 0;
    int filstat = 0;
    DIR *dp;
    printf("starting read\n");
    if (strcmp(path, "/") != 0)
        return -ENOENT;
    struct dirent *de;
    filler(buf, ".", NULL, 1);
    filler(buf, "..", NULL, 2);
    char **entries = get_dirs_from_db();
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (!strcmp(entries[i], "")) {
            free_entries(entries);
            return 0;
        } else if (filler(buf, entries[i], NULL, 0)) {
            printf("Successfully read file: %s\n", entries[i]);
            free_entries(entries);
            return 0;
        }
    }
    // free_entries(entries);
    return 0;
}

int bxfs_open(const char *path, struct fuse_file_info *fi) {
    struct stat statbuf;
    int retstat = bxfs_getattr(path, &statbuf);
    // free(statbuf);
    return retstat;
}

int bxfs_read(const char *path, char *buf, size_t size, off_t offset,
              struct fuse_file_info *fi) {
    char command[5 + strlen(path)];
    const char *filename = path[0] == '/' ? path + 1 : path;
    sprintf(command, "GET %s", filename);
    char *result;
    char *info;
    const char *final_command = command;
    int retstat = basex_execute(sfd, final_command, &result, &info);
    if (retstat) {
        printf("File not found: %s\n", filename);
        return -ENOENT;
    }
    int length = strlen(result);
    if (offset >= (length - 1) || offset < 0)
        return 0;

    const char *to_read = result + offset;
    strncpy(buf, to_read, size);
    return strlen(buf);
}

int bxfs_getxattr(const char *, const char *, char *, size_t) { return 0; }

int bxfs_truncate(const char *path, off_t size) {
    struct FileSize *fs;
    const char *filename = path[0] == '/' ? path + 1 : path;
    HASH_FIND_STR(filesizes, filename, fs);
    if (fs) {
        fs->size = size;
        return 0;
    } else {
        return -EINVAL;
    }
}

int bxfs_write(const char *path, const char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi) {

    const char *filename = path[0] == '/' ? path + 1 : path;
    char trimmed_buf[size];
    memcpy(trimmed_buf, buf, size);
    trimmed_buf[size] = '\0';
    char command[4 + strlen(filename) + size];
    sprintf(command, "PUT %s %s", filename, trimmed_buf);
    char *result;
    char *info;
    const char *final_command = command;
    int retstat = basex_execute(sfd, command, &result, &info);
    if (retstat) {
        printf("Failed to write file: %s", filename);
        return -EINVAL;
    }
    return size;
}
static struct fuse_operations prefix_oper = {
    .getattr = bxfs_getattr,
    .read = bxfs_read,
    .open = bxfs_open,
    .readdir = bxfs_readdir,
    .write = bxfs_write,
    .truncate = bxfs_truncate,
#ifdef HAVE_SETXATTR
    .getxattr = bxfs_getxattr,
    .listxattr = bxfs_listxattr,
    .removexattr = bxfs_removexattr,
#endif
};

int main(int argc, char *argv[]) {

    sfd = basex_connect(DBHOST, DBPORT);
    if (sfd == -1) {
        warnx("Cannot connect to BaseX server.");
        return -1;
    }

    rc = basex_authenticate(sfd, DBUSER, DBPASSWD);
    if (rc == -1) {
        warnx("Access to DB denied.");
        goto err_out;
    }

    printf("opening DB\n");
    char *result;
    char *info;
    rc = basex_execute(sfd, "OPEN NewRLE", &result, &info);
    if (rc == -1) {
        warnx("Failed to open DB\n");
    }
    char **entries = get_dirs_from_db();
    for (int i = 0; entries[i] != NULL; i++)
        printf("%s\n", entries[i]);
    umask(0);
    return fuse_main(argc, argv, &prefix_oper, NULL);
    return 0;
err_out:
    basex_close(sfd);
    return -1;
}

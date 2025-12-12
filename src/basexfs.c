/*
Copyright (c) 2025, Josef Thorne https://github.com/Josef-Thorne-A
https://troydhanson.github.io/uthash/ All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

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
    char *cache;
    const char *filename;
    UT_hash_handle hh; /* makes this structure hashable */
};

struct FileSize *filesizes = NULL;

char **get_dirs_from_db() {
    char *result;
    char *info;
    basex_execute(sfd, "DIR", &result, &info);
    int entrylength = 50;
    char **files = malloc(entrylength * sizeof(char *));
    char *line;
    char *filename;
    // need this because strsep mangles our original string
    char *orig_res = result;
    strsep(&result, "\n");
    strsep(&result, "\n");
    for (int i = 0; 1; i++) {
        if (i >= entrylength)
            files = realloc(files, i * sizeof(char *) + i / 2);
        line = strsep(&result, "\n");
        if (line == NULL)
            break;
        filename = strsep(&line, " ");
        if (filename[0] == '\n' || filename[0] == 0) {
            files[i] = "";
            break;
        }
        files[i] = malloc(sizeof(char) * strlen(filename) + 1);
        strcpy(files[i], filename);
    }
    free(orig_res);
    free(info);
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
        free(result);
        free(info);
        return -ENOENT;
    }
    stbuf->st_size = strlen(result);

    fs = malloc(sizeof *fs);
    char *key = malloc((strlen(filename) + 1) * sizeof(char));
    strcpy(key, filename);
    fs->filename = key;
    fs->cache = result;
    fs->size = stbuf->st_size;
    HASH_ADD_STR(filesizes, filename, fs);
    stbuf->st_mode = S_IFREG | 0777;
    stbuf->st_nlink = 1;
    free(info);
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
    free_entries(entries);
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
    if (size == 0)
        return 0;
    char command[6 + strlen(path)];
    const char *filename = path[0] == '/' ? path + 1 : path;
    sprintf(command, "GET %s", filename);
    char *result = 0;
    char *info;
    int cached_size = 0;
    int retstat = 0;
    struct FileSize *fs;
    int length;
    if (filesizes != NULL) {
        HASH_FIND_STR(filesizes, filename, fs);
        if (fs != NULL && fs->cache != NULL) {
            cached_size = fs->size;
            result = fs->cache;
        } else {
            fs = malloc(sizeof *fs);
            retstat = basex_execute(sfd, command, &result, &info);

            free(info);
            if (retstat) {
                printf("File not found: %s\n", filename);
                free(result);
                return -ENOENT;
            } else {

                length = strlen(result);
                char *key = malloc((strlen(filename) + 1) * sizeof(char));
                fs->size = length;
                fs->cache = result;
                fs->filename = key;
                HASH_ADD_STR(filesizes, filename, fs);
            }
        }
    }
    length = fs->size;
    if (offset >= (length) || offset < 0)
        return 0;

    char selected_text[size + 1];
    strlcpy(selected_text, result + offset, size + 1);
    const char *to_read = selected_text;

    const int bytes_read = strlen((const char *)to_read);
    memcpy(buf, (const char *)to_read, size);
    if (bytes_read > size) {
        printf("READ MORE BYTES THAN SIZE, SHOULD NOT HAPPEN");
        return -1;
    } else if (bytes_read < size) {
        printf("BYTES NOT AS MUCH AS SIZE: %ld %d", size, bytes_read);
        return bytes_read;
    } else {
        return bytes_read;
    }
}

int bxfs_getxattr(const char *a, const char *b, char *c, size_t d) { return 0; }

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
    char trimmed_buf[size + 1];
    strlcpy(trimmed_buf, buf, size + 1);
    char command[5 + strlen(filename) + size];
    sprintf(command, "PUT %s %s", filename, trimmed_buf);
    char *result;
    char *info;
    const char *final_command = command;
    int retstat = basex_execute(sfd, command, &result, &info);
    if (retstat) {
        printf("Failed to write file: %s", filename);
        return -EINVAL;
    }
    free(result);
    free(info);
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

    char *dbhost = getenv("DBHOST");
    dbhost = dbhost ? dbhost : DBHOST;
    printf("%s", dbhost);

    char *dbport = getenv("DBPORT");
    dbport = dbport ? dbhost : DBPORT;

    sfd = basex_connect(dbhost, dbport);
    if (sfd == -1) {
        warnx("Cannot connect to BaseX server.");
        return -1;
    }
    char *dbuser = getenv("DBUSER");
    dbuser = dbuser ? dbuser : DBUSER;

    char *dbpasswd = getenv("DBPASSWD");
    dbpasswd = dbpasswd ? dbpasswd : DBPASSWD;

    rc = basex_authenticate(sfd, dbuser, dbpasswd);
    if (rc == -1) {
        warnx("Access to DB denied.");
        goto err_out;
    }
    char opendb[100] = "OPEN ";
    char *dbname = getenv("DBNAME");
    dbname = dbname ? dbname : "NewRLE";
    strcat(opendb, dbname);
    printf("opening DB\n");
    char *result;
    char *info;
    rc = basex_execute(sfd, opendb, &result, &info);
    if (rc == -1) {
        warnx("Failed to open DB\n");
    }
    free(result);
    free(info);
    umask(0);
    return fuse_main(argc, argv, &prefix_oper, NULL);

    return 0;
err_out:
    basex_close(sfd);
    return -1;
}

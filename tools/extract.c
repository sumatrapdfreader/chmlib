/***************************************************************************
 *          extract_chmLib.c - CHM archive extractor                       *
 *                           -------------------                           *
 *                                                                         *
 *  author:     Jed Wing <jedwin@ugcs.caltech.edu>                         *
 *  notes:      This is a quick-and-dirty chm archive extractor.           *
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#include "chm_lib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
#include <windows.h>
#include <direct.h>
#define mkdir(X, Y) _mkdir(X)
#define snprintf _snprintf
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

struct extract_context {
    const char* base_path;
};

static int dir_exists(const char* path) {
#ifdef WIN32
    /* why doesn't this work?!? */
    HANDLE hFile;

    hFile =
        CreateFileA(path, FILE_LIST_DIRECTORY, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        return 1;
    } else
        return 0;
#else
    struct stat statbuf;
    if (stat(path, &statbuf) != -1)
        return 1;
    else
        return 0;
#endif
}

static int rmkdir(char* path) {
    /*
     * strip off trailing components unless we can stat the directory, or we
     * have run out of components
     */

    char* i = strrchr(path, '/');

    if (path[0] == '\0' || dir_exists(path))
        return 0;

    if (i != NULL) {
        *i = '\0';
        rmkdir(path);
        *i = '/';
        mkdir(path, 0777);
    }

#ifdef WIN32
    return 0;
#else
    if (dir_exists(path))
        return 0;
    else
        return -1;
#endif
}

static bool extract_entry(chm_file* h, chm_entry* e, const char* base_path) {
    int64_t path_len;
    char buf[32768];
    char* i;

    if (e->path[0] != '/')
        return true;

    /* quick hack for security hole mentioned by Sven Tantau */
    if (strstr(e->path, "/../") != NULL) {
        /* fprintf(stderr, "Not extracting %s (dangerous path)\n", ui->path); */
        return true;
    }

    if (snprintf(buf, sizeof(buf), "%s%s", base_path, e->path) > 1024) {
        return false;
    }

    /* Get the length of the path */
    path_len = strlen(e->path) - 1;

    if (e->path[path_len] == '/') {
        /* this is directory */
        return rmkdir(buf) != -1;
    }

    /* this is file */
    FILE* fout;
    int64_t len, remain = (int64_t)e->length;
    int64_t offset = 0;

    printf("--> %s\n", e->path);
    if ((fout = fopen(buf, "wb")) == NULL) {
        /* make sure that it isn't just a missing directory before we abort */
        char newbuf[32768];
        strcpy(newbuf, buf);
        i = strrchr(newbuf, '/');
        *i = '\0';
        rmkdir(newbuf);
        if ((fout = fopen(buf, "wb")) == NULL)
            return false;
    }

    while (remain != 0) {
        len = chm_retrieve_entry(h, e, (uint8_t*)buf, offset, sizeof(buf));
        if (len > 0) {
            fwrite(buf, 1, (size_t)len, fout);
            offset += (int64_t)len;
            remain -= len;
        } else {
            fprintf(stderr, "incomplete file: %s\n", e->path);
            break;
        }
    }

    fclose(fout);
    return true;
}

static bool extract(chm_file* h, const char* base_path) {
    /* extract as many entries as possible */
    for (int i = 0; i < h->n_entries; i++) {
        if (!extract_entry(h, h->entries[i], base_path)) {
            return false;
        }
    }
    if (h->parse_entries_failed) {
        return false;
    }
    return true;
}

static bool extract_fd(const char* path, const char* base_path) {
    fd_reader_ctx ctx;
    if (!fd_reader_init(&ctx, path)) {
        fprintf(stderr, "failed to open %s\n", path);
        return false;
    }
    chm_file f;
    bool ok = chm_parse(&f, fd_reader, &ctx);
    if (!ok) {
        fprintf(stderr, "chm_parse() failed\n");
        fd_reader_close(&ctx);
        return false;
    }
    printf("%s:\n", path);
    ok = extract(&f, base_path);
    chm_close(&f);
    fd_reader_close(&ctx);
    return ok;
}

int main(int c, char** v) {
    if (c < 3) {
        fprintf(stderr, "usage: %s <chmfile> <outdir>\n", v[0]);
        exit(1);
    }

    bool ok = extract_fd(v[1], v[2]);
    if (!ok) {
        printf("   *** ERROR ***\n");
    }
    return 0;
}

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

#define UNUSED(x) (void) x

/* TODO: actually collect all data */
static uint8_t* extract_file(struct chmFile* h, struct chmUnitInfo* ui) {
    uint8_t buf[32 * 1024];

    LONGINT64 len, remain = (LONGINT64)ui->length;
    LONGUINT64 offset = 0;

    while (remain != 0) {
        len = chm_retrieve_object(h, ui, buf, offset, sizeof(buf));
        if (len > 0) {
            offset += (LONGUINT64)len;
            remain -= len;
        } else {
            return NULL;
        }
    }

    if (remain < 0) {
        return NULL;
    }

    return NULL;
}

/* return 1 if path ends with '/' */
static int is_dir(const char* path) {
    size_t n = strlen(path) - 1;
    return path[n] == '/';
}

static int enum_cb(struct chmFile* h, struct chmUnitInfo* ui, void* ctx) {
    static char buf[128] = {0};

    UNUSED(h);
    UNUSED(ctx);

    int isFile = ui->flags & CHM_ENUMERATE_FILES;

    if (ui->flags & CHM_ENUMERATE_NORMAL)
        strcpy(buf, "normal ");
    else if (ui->flags & CHM_ENUMERATE_SPECIAL)
        strcpy(buf, "special ");
    else if (ui->flags & CHM_ENUMERATE_META)
        strcpy(buf, "meta ");

    if (ui->flags & CHM_ENUMERATE_DIRS)
        strcat(buf, "dir");
    else if (isFile)
        strcat(buf, "file");

    printf("   %1d %8d %8d   %s\t\t%s\n", (int)ui->space, (int)ui->start, (int)ui->length, buf,
           ui->path);

    if (ui->length == 0 || !isFile) {
        return CHM_ENUMERATOR_CONTINUE;
    }

    /* this should be redundant to isFile, but better be safe than sorry */
    if (is_dir(ui->path)) {
        return CHM_ENUMERATOR_CONTINUE;
    }

    uint8_t* d = extract_file(h, ui);
    /* TODO: calculate and print sha1 */
    free(d);

    return CHM_ENUMERATOR_CONTINUE;
}

int main(int c, char** v) {
    struct chmFile* h;

    if (c < 2) {
        fprintf(stderr, "usage: %s <chmfile>\n", v[0]);
        exit(1);
    }

    h = chm_open(v[1]);
    if (h == NULL) {
        fprintf(stderr, "failed to open %s\n", v[1]);
        exit(1);
    }

    printf(" spc    start   length   type\t\t\tname\n");
    printf(" ===    =====   ======   ====\t\t\t====\n");

    if (!chm_enumerate(h, CHM_ENUMERATE_ALL, enum_cb, NULL)) {
        printf("   *** ERROR ***\n");
    }

    chm_close(h);

    return 0;
}

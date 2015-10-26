/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha1.h"
#include "chm_lib.h"

#define UNUSED(x) (void) x

static uint8_t* extract_file(struct chmFile* h, struct chmUnitInfo* ui) {
    LONGINT64 len = (LONGINT64)ui->length;

    uint8_t* buf = (uint8_t*)malloc(len + 1);
    if (buf == NULL) {
        return NULL;
    }
    buf[len] = 0; /* null-terminate just in case */

    LONGINT64 n = chm_retrieve_object(h, ui, buf, 0, len);
    if (n != len) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* return 1 if path ends with '/' */
static int is_dir(const char* path) {
    size_t n = strlen(path) - 1;
    return path[n] == '/';
}

/* return 1 if s contains ',' */
static int needs_csv_escaping(const char* s) {
    while (*s && (*s != ',')) {
        s++;
    }
    return *s == ',';
}

static int enum_cb(struct chmFile* h, struct chmUnitInfo* ui, void* ctx) {
    UNUSED(ctx);
    char buf[128] = {0};

    int isFile = ui->flags & CHM_ENUMERATE_FILES;

    if (ui->flags & CHM_ENUMERATE_SPECIAL)
        strcpy(buf, "special_");
    else if (ui->flags & CHM_ENUMERATE_META)
        strcpy(buf, "meta_");

    if (ui->flags & CHM_ENUMERATE_DIRS)
        strcat(buf, "dir");
    else if (isFile)
        strcat(buf, "file");

    /* TODO: calculate and print sha1 */
    if (needs_csv_escaping(ui->path)) {
        printf("%d,%d,%d,%s,\"%s\"\n", (int)ui->space, (int)ui->start, (int)ui->length, buf,
               ui->path);
    } else {
        printf("%1d,%d,%d,%s,%s\n", (int)ui->space, (int)ui->start, (int)ui->length, buf, ui->path);
    }

    if (ui->length == 0 || !isFile) {
        return CHM_ENUMERATOR_CONTINUE;
    }

    /* this should be redundant to isFile, but better be safe than sorry */
    if (is_dir(ui->path)) {
        return CHM_ENUMERATOR_CONTINUE;
    }

    uint8_t* d = extract_file(h, ui);
    free(d);

    return CHM_ENUMERATOR_CONTINUE;
}

#if 0
static void test_asan_1(int off) {
  int stack_array[100];
  stack_array[1] = 0;
  int n = stack_array[off + 100];  // BOOM
  printf("n: %d\n", n);
}
#endif

int main(int c, char** v) {
    struct chmFile* h;

    if (c < 2) {
        fprintf(stderr, "usage: %s <chmfile>\n", v[0]);
        exit(1);
    }

    /* test_asan_1(0); */
#if 0
    int err = sha1_test();
    if (err != CRYPT_OK) {
      fprintf(stderr, "sha1 doesn't work\n");
    }
#endif

    h = chm_open(v[1]);
    if (h == NULL) {
        fprintf(stderr, "failed to open %s\n", v[1]);
        exit(1);
    }

    if (!chm_enumerate(h, CHM_ENUMERATE_ALL, enum_cb, NULL)) {
        printf("   *** ERROR ***\n");
    }

    chm_close(h);

    return 0;
}

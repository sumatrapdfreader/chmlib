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

#include "sha1.h"

static uint8_t* extract_entry(struct chm_file* h, chm_entry* e) {
    int64_t len = (int64_t)e->length;

    uint8_t* buf = (uint8_t*)malloc((size_t)len + 1);
    if (buf == NULL) {
        return NULL;
    }
    buf[len] = 0; /* null-terminate just in case */

    int64_t n = chm_retrieve_entry(h, e, buf, 0, len);
    if (n != len) {
        free(buf);
        return NULL;
    }
    return buf;
}

/* return 1 if s contains ',' */
static int needs_csv_escaping(const char* s) {
    while (*s && (*s != ',')) {
        s++;
    }
    return *s == ',';
}

static char hex_char(int n) {
    static const char* s = "0123456789ABCDEF";
    return s[n];
}

static void sha1_to_hex(uint8_t* sha1, char* sha1Hex) {
    for (int i = 0; i < 20; i++) {
        uint8_t c = sha1[i];
        int n = (c >> 4) & 0xf;
        sha1Hex[i * 2] = hex_char(n);
        n = c & 0xf;
        sha1Hex[(i * 2) + 1] = hex_char(n);
    }
}

static int process_entry(struct chm_file* h, chm_entry* e) {
    char buf[128] = {0};
    uint8_t sha1[20] = {0};
    char sha1Hex[41] = {0};

    int isFile = e->flags & CHM_ENUMERATE_FILES;

    if (e->flags & CHM_ENUMERATE_SPECIAL)
        strcpy(buf, "special_");
    else if (e->flags & CHM_ENUMERATE_META)
        strcpy(buf, "meta_");

    if (e->flags & CHM_ENUMERATE_DIRS)
        strcat(buf, "dir");
    else if (isFile)
        strcat(buf, "file");

    if (e->length > 0) {
        uint8_t* d = extract_entry(h, e);
        if (d != NULL) {
            int err = sha1_process_all(d, (unsigned long)e->length, sha1);
            free(d);
            if (err != CRYPT_OK) {
                return 1;
            }
        }
    }

    sha1_to_hex(sha1, sha1Hex);
    if (needs_csv_escaping(e->path)) {
        printf("%d,%d,%d,%s,%s,\"%s\"\n", (int)e->space, (int)e->start, (int)e->length, buf,
               sha1Hex, e->path);
    } else {
        printf("%1d,%d,%d,%s,%s,%s\n", (int)e->space, (int)e->start, (int)e->length, buf, sha1Hex,
               e->path);
    }
    return 0;
}

/* returns 0 if ok, != 0 on error */
static int test_chm(chm_file* h) {
    chm_parse_result* res = chm_parse(h);
    int err = 0;
    for (int i = 0; i < res->n_entries; i++) {
        err = process_entry(h, res->entries[i]);
        if (err != 0) {
            printf("   *** ERROR ***\n");
            break;
        }
    }
    return err;
}

static int test_fd(const char* path) {
    fd_reader_ctx ctx;
    if (!fd_reader_init(&ctx, path)) {
        fprintf(stderr, "failed to open %s\n", path);
        return 1;
    }
    chm_file f;
    int ok = chm_init(&f, fd_reader, &ctx);
    if (!ok) {
        fd_reader_close(&ctx);
        return 1;
    }
    ok = test_chm(&f);
    chm_close(&f);
    fd_reader_close(&ctx);
    return ok;
}

static int show_dbg_out = 0;

static void dbg_print(const char* s) {
    fprintf(stderr, "%s", s);
}

int main(int c, char** v) {
    if (c < 2) {
        fprintf(stderr, "usage: %s <chmfile>\n", v[0]);
        exit(1);
    }
    if (show_dbg_out) {
        chm_set_dbgprint(dbg_print);
    }
    return test_fd(v[1]);
}

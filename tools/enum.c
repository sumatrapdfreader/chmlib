/***************************************************************************
 *          enum_chmLib.c - CHM archive test driver                        *
 *                           -------------------                           *
 *                                                                         *
 *  author:     Jed Wing <jedwin@ugcs.caltech.edu>                         *
 *  notes:      This is a quick-and-dirty test driver for the chm lib      *
 *              routines.  The program takes as its input the paths to one *
 *              or more .chm files.  It attempts to open each .chm file in *
 *              turn, and display a listing of all of the files in the     *
 *              archive.                                                   *
 *                                                                         *
 *              It is not included as a particularly useful program, but   *
 *              rather as a sort of "simplest possible" example of how to  *
 *              use the enumerate portion of the API.                      *
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

static void print_entry(chm_entry* e) {
    char buf[128] = {0};

    if (e->flags & CHM_ENUMERATE_NORMAL)
        strcpy(buf, "normal ");
    else if (e->flags & CHM_ENUMERATE_SPECIAL)
        strcpy(buf, "special ");
    else if (e->flags & CHM_ENUMERATE_META)
        strcpy(buf, "meta ");

    if (e->flags & CHM_ENUMERATE_DIRS)
        strcat(buf, "dir");
    else if (e->flags & CHM_ENUMERATE_FILES)
        strcat(buf, "file");

    printf("   %1d %8d %8d   %s\t\t%s\n", (int)e->space, (int)e->start, (int)e->length, buf,
           e->path);
}

static bool enum_fd(const char* path) {
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
    for (int i = 0; i < f.n_entries; i++) {
        print_entry(f.entries[i]);
    }
    if (f.parse_entries_failed) {
        return false;
    }
    chm_close(&f);
    fd_reader_close(&ctx);
    return true;
}

int main(int c, char** v) {
    for (int i = 1; i < c; i++) {
        const char* path = v[i];

        printf("%s:\n", path);
        printf(" spc    start   length   type\t\t\tname\n");
        printf(" ===    =====   ======   ====\t\t\t====\n");

        bool ok = enum_fd(path);
        if (!ok) {
            printf("   *** ERROR ***\n");
            exit(1);
        }
    }

    return 0;
}

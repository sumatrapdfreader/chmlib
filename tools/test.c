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

#define UNUSED(x) (void)x

static int enum_cb(struct chmFile* h, struct chmUnitInfo* ui, void* ctx) {
  static char buf[128] = { 0 };

  UNUSED(h);
  UNUSED(ctx);

  if (ui->flags & CHM_ENUMERATE_NORMAL)
      strcpy(buf, "normal ");
  else if (ui->flags & CHM_ENUMERATE_SPECIAL)
      strcpy(buf, "special ");
  else if (ui->flags & CHM_ENUMERATE_META)
      strcpy(buf, "meta ");

  if (ui->flags & CHM_ENUMERATE_DIRS)
      strcat(buf, "dir");
  else if (ui->flags & CHM_ENUMERATE_FILES)
      strcat(buf, "file");

  printf("   %1d %8d %8d   %s\t\t%s\n", (int)ui->space, (int)ui->start, (int)ui->length, buf,
         ui->path);
  return CHM_ENUMERATOR_CONTINUE;
}

#if 0
    LONGUINT64 ui_path_len;
    char buffer[32768];
    struct extract_context* ctx = (struct extract_context*)context;
    char* i;

    if (ui->path[0] != '/')
        return CHM_ENUMERATOR_CONTINUE;

    /* quick hack for security hole mentioned by Sven Tantau */
    if (strstr(ui->path, "/../") != NULL) {
        /* fprintf(stderr, "Not extracting %s (dangerous path)\n", ui->path); */
        return CHM_ENUMERATOR_CONTINUE;
    }

    if (snprintf(buffer, sizeof(buffer), "%s%s", ctx->base_path, ui->path) > 1024)
        return CHM_ENUMERATOR_FAILURE;

    /* Get the length of the path */
    ui_path_len = strlen(ui->path) - 1;

    /* Distinguish between files and dirs */
    if (ui->path[ui_path_len] != '/') {
        FILE* fout;
        LONGINT64 len, remain = (LONGINT64)ui->length;
        LONGUINT64 offset = 0;

        printf("--> %s\n", ui->path);
        if ((fout = fopen(buffer, "wb")) == NULL) {
            /* make sure that it isn't just a missing directory before we abort */
            char newbuf[32768];
            strcpy(newbuf, buffer);
            i = strrchr(newbuf, '/');
            *i = '\0';
            rmkdir(newbuf);
            if ((fout = fopen(buffer, "wb")) == NULL)
                return CHM_ENUMERATOR_FAILURE;
        }

        while (remain != 0) {
            len = chm_retrieve_object(h, ui, (unsigned char*)buffer, offset, 32768);
            if (len > 0) {
                fwrite(buffer, 1, (size_t)len, fout);
                offset += (LONGUINT64)len;
                remain -= len;
            } else {
                fprintf(stderr, "incomplete file: %s\n", ui->path);
                break;
            }
        }

        fclose(fout);
    } else {
        if (rmkdir(buffer) == -1)
            return CHM_ENUMERATOR_FAILURE;
    }

    return CHM_ENUMERATOR_CONTINUE;
#endif

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

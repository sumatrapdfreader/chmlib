/***************************************************************************
 *             chm_http.c - CHM archive test driver                        *
 *                           -------------------                           *
 *                                                                         *
 *  author:     Jed Wing <jedwin@ugcs.caltech.edu>                         *
 *  notes:      This is a slightly more complex test driver for the chm    *
 *              routines.  It also serves the purpose of making .chm html  *
 *              help files viewable from a text mode browser, which was my *
 *              original purpose for all of this.                          *
 *                                                                         *
 *              It is not included with the expectation that it will be of *
 *              use to others; nor is it included as an example of a       *
 *              stunningly good implementation of an HTTP server.  It is,  *
j *              in fact, probably badly broken for any serious usage.      *
 *                                                                         *
 *              Nevertheless, it is another example program, and it does   *
 *              serve a purpose for me, so I've included it as well.       *
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

/* standard system includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* includes for networking */
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* threading includes */
#include <pthread.h>

#include <getopt.h>

static int config_port = 8080;
static char config_bind[65536] = "0.0.0.0";

static void usage(const char* argv0) {
#ifdef CHM_HTTP_SIMPLE
    fprintf(stderr, "usage: %s <filename>\n", argv0);
#else
    fprintf(stderr, "usage: %s [--port=PORT] [--bind=IP] <filename>\n", argv0);
#endif
}

static int chmhttp_server(const char* path);

int main(int c, char** v) {
  int res;
#ifdef CHM_HTTP_SIMPLE
    if (c < 2) {
        usage(v[0]);
        return 1;
    }

    /* run the server */
    res = chmhttp_server(v[1]);

#else
    int optindex = 0;

    struct option longopts[] = {{"port", required_argument, 0, 'p'},
                                {"bind", required_argument, 0, 'b'},
                                {"help", no_argument, 0, 'h'},
                                {0, 0, 0, 0}};

    while (1) {
        int o;
        o = getopt_long(c, v, "n:b:h", longopts, &optindex);
        if (o < 0) {
            break;
        }

        switch (o) {
            case 'p':
                config_port = atoi(optarg);
                if (config_port <= 0) {
                    fprintf(stderr, "bad port number (%s)\n", optarg);
                    exit(1);
                }
                break;

            case 'b':
                strncpy(config_bind, optarg, 65536);
                config_bind[65535] = '\0';
                break;

            case 'h':
                usage(v[0]);
                break;
        }
    }

    if (optind + 1 != c) {
        usage(v[0]);
    }

    /* run the server */
    res = chmhttp_server(v[optind]);
#endif

    return res;
}

struct chmHttpServer {
    int socket;
    chm_file file;
};

struct chmHttpSlave {
    int fd;
    struct chmHttpServer* server;
};

static void* _slave(void* param);

static int chmhttp_server(const char* path) {
    struct chmHttpServer server;
    struct chmHttpSlave* slave;
    struct sockaddr_in bindAddr;
    socklen_t addrLen;
    pthread_t tid;
    int one = 1;

    fd_reader_ctx ctx;
    if (!fd_reader_init(&ctx, path)) {
        fprintf(stderr, "failed to open %s\n", path);
        return 1;
    }
    bool ok = chm_parse(&server.file, fd_reader, &ctx);
    if (!ok) {
      fprintf(stderr, "couldn't open file '%s'\n", path);
        fd_reader_close(&ctx);
        return 2;
    }

    server.socket = socket(AF_INET, SOCK_STREAM, 0);
    memset(&bindAddr, 0, sizeof(struct sockaddr_in));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(config_port);
    bindAddr.sin_addr.s_addr = inet_addr(config_bind);

    if (setsockopt(server.socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
        perror("setsockopt");
        return 3;
    }

    if (bind(server.socket, (struct sockaddr*)&bindAddr, sizeof(struct sockaddr_in)) < 0) {
        close(server.socket);
        server.socket = -1;
        fprintf(stderr, "couldn't bind to ip %s port %d\n", config_bind, config_port);
        return 3;
    }

    /* listen for connections */
    listen(server.socket, 75);
    addrLen = sizeof(struct sockaddr);
    while (1) {
        slave = (struct chmHttpSlave*)malloc(sizeof(struct chmHttpSlave));
        slave->server = &server;
        slave->fd = accept(server.socket, (struct sockaddr*)&bindAddr, &addrLen);
        if (slave->fd == -1)
            break;

        pthread_create(&tid, NULL, _slave, (void*)slave);
        pthread_detach(tid);
    }
    free(slave);
    return 0;
}

static void service_request(int fd, struct chm_file* file);

static void* _slave(void* param) {
    struct chmHttpSlave* slave;
    struct chm_file* file;

    /* grab our relevant information */
    slave = (struct chmHttpSlave*)param;
    file = &slave->server->file;

    /* handle request */
    service_request(slave->fd, file);

    /* free our resources and return */
    close(slave->fd);
    free(slave);
    return NULL;
}

static const char CONTENT_404[] =
    "HTTP/1.1 404 File not found\r\nConnection: close\r\nContent-Type: text/html; "
    "charset=iso-8859-1\r\n\r\n<html><head><title>404 File Not Found</title></head><body><h1>404 "
    "File not found</h1></body></html>\r\n";
static const char CONTENT_500[] =
    "HTTP/1.1 500 Unknown thing\r\nConnection: close\r\nContent-Type: text/html; "
    "charset=iso-8859-1\r\n\r\n<html><head><title>500 Unknown thing</title></head><body><h1>500 "
    "Unknown thing</h1></body></html>\r\n";
static const char INTERNAL_ERROR[] =
    "HTTP/1.1 500 Internal error\r\nConnection: close\r\nContent-Type: text/html; "
    "charset=iso-8859-1\r\n\r\n<html><head><title>500 Unknown thing</title></head><body><h1>500 "
    "Server error</h1></body></html>\r\n";

struct mime_mapping {
    const char* ext;
    const char* ctype;
};

static struct mime_mapping mime_types[] = {{".htm", "text/html"},
                                           {".html", "text/html"},
                                           {".css", "text/css"},
                                           {".gif", "image/gif"},
                                           {".jpg", "image/jpeg"},
                                           {".jpeg", "image/jpeg"},
                                           {".jpe", "image/jpeg"},
                                           {".bmp", "image/bitmap"},
                                           {".png", "image/png"}};

static const char* lookup_mime(const char* ext) {
    size_t nTypes = sizeof(mime_types) / sizeof(struct mime_mapping);
    for (size_t i = 0; ext && i < nTypes; i++) {
        if (strcasecmp(mime_types[i].ext, ext) == 0)
            return mime_types[i].ctype;
    }

    return "application/octet-stream";
}

static void print_entry_index(FILE *fout, chm_entry* e) {
    fprintf(fout,
            "<tr>"
            "<td align=right>%8d\n</td>"
            "<td><a href=\"%s\">%s</a></td>"
            "</tr>",
            (int)e->length, e->path, e->path);
}

static void deliver_index(FILE* fout, struct chm_file* file) {
    fprintf(fout,
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            /* "Content-Length: 1000000\r\n" */
            "Content-Type: text/html\r\n\r\n"

            "<h2><u>CHM contents:</u></h2>"
            "<body><table>"
            "<tr><td><h5>Size:</h5></td><td><h5>File:</h5></td></tr>"
            "<tt>");
    for (int i = 0; i < file->n_entries; i++) {
          print_entry_index(fout, file->entries[i]);
    }

    fprintf(fout, "</tt> </table></body></html>");
}

static int streq(const char* s1, const char* s2) {
    return strcasecmp(s1, s2) == 0;
}

static chm_entry *find_entry_by_path(chm_file *f, const char *path) {
  chm_entry *e;
  for (int i = 0; i < f->n_entries; i++) {
    e = f->entries[i];
    if (streq(e->path, path)) {
      return e;
    }
  }
  return NULL;
}

static void deliver_content(FILE* fout, const char* path, struct chm_file* file) {
    chm_entry *e;
    const char* ext;
    unsigned char buffer[65536];
    int swath, offset;

    if (strcmp(path, "/") == 0) {
        deliver_index(fout, file);
        fclose(fout);
        return;
    }

    e = find_entry_by_path(file, path);
    if (e == NULL) {
      fprintf(fout, CONTENT_404);
      fclose(fout);
      return;
    }

    /* send the file back */
    ext = strrchr(path, '.');
    fprintf(
        fout,
        "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n",
        (int)e->length, lookup_mime(ext));

    /* pump the data out */
    swath = 65536;
    offset = 0;
    while (offset < e->length) {
        if ((e->length - offset) < 65536)
            swath = (int)(e->length - offset);
        else
            swath = 65536;
        swath = (int)chm_retrieve_entry(file, e, buffer, offset, swath);
        offset += swath;
        fwrite(buffer, 1, (size_t)swath, fout);
    }
    fclose(fout);
}

static void service_request(int fd, struct chm_file* file) {
    char buffer[4096];
    char buffer2[4096];
    char* end;
    FILE* fout = fdopen(fd, "w+b");
    if (fout == NULL) {
        perror("chm_http: failed to fdopen client stream");
        write(fd, INTERNAL_ERROR, strlen(INTERNAL_ERROR));
        close(fd);
        return;
    }

    fgets(buffer, 4096, fout);
    while (1) {
        if (fgets(buffer2, 4096, fout) == NULL)
            break;
        if (buffer2[0] == '\r' || buffer2[0] == '\n' || buffer2[0] == '\0')
            break;
    }
    end = strrchr(buffer, ' ');
    if (strncmp(end + 1, "HTTP", 4) == 0)
        *end = '\0';
    if (strncmp(buffer, "GET ", 4) == 0)
        deliver_content(fout, buffer + 4, file);
    else {
        fprintf(fout, CONTENT_500);
        fclose(fout);
        return;
    }
}

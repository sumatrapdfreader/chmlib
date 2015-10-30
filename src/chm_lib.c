/***************************************************************************
 *             chm_lib.c - CHM archive manipulation routines               *
 *                           -------------------                           *
 *                                                                         *
 *  author:     Jed Wing <jedwin@ugcs.caltech.edu>                         *
 *  version:    0.3                                                        *
 *  notes:      These routines are meant for the manipulation of microsoft *
 *              .chm (compiled html help) files, but may likely be used    *
 *              for the manipulation of any ITSS archive, if ever ITSS     *
 *              archives are used for any other purpose.                   *
 *                                                                         *
 *              Note also that the section names are statically handled.   *
 *              To be entirely correct, the section names should be read   *
 *              from the section names meta-file, and then the various     *
 *              content sections and the "transforms" to apply to the data *
 *              they contain should be inferred from the section name and  *
 *              the meta-files referenced using that name; however, all of *
 *              the files I've been able to get my hands on appear to have *
 *              only two sections: Uncompressed and MSCompressed.          *
 *              Additionally, the ITSS.DLL file included with Windows does *
 *              not appear to handle any different transforms than the     *
 *              simple LZX-transform.  Furthermore, the list of transforms *
 *              to apply is broken, in that only half the required space   *
 *              is allocated for the list.  (It appears as though the      *
 *              space is allocated for ASCII strings, but the strings are  *
 *              written as unicode.  As a result, only the first half of   *
 *              the string appears.)  So this is probably not too big of   *
 *              a deal, at least until CHM v4 (MS .lit files), which also  *
 *              incorporate encryption, of some description.               *
 *                                                                         *
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU Lesser General Public License as        *
 *   published by the Free Software Foundation; either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 ***************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef WIN32
#include <windows.h>
#include <malloc.h>
#define strcasecmp stricmp
#define strncasecmp strnicmp
#else
/* #define _XOPEN_SOURCE 200809L */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/* #include <dmalloc.h> */
#endif

#include "chm_lib.h"
#include "lzx.h"

#ifndef CHM_MAX_BLOCKS_CACHED
#define CHM_MAX_BLOCKS_CACHED 5
#endif

/* names of sections essential to decompression */
#define CHMU_RESET_TABLE                                                                 \
    "::DataSpace/Storage/MSCompressed/Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/" \
    "InstanceData/ResetTable"
#define CHMU_LZXC_CONTROLDATA "::DataSpace/Storage/MSCompressed/ControlData"
#define CHMU_CONTENT "::DataSpace/Storage/MSCompressed/Content"

#define CHM_ITSF_V2_LEN 0x58
#define CHM_ITSF_V3_LEN 0x60
#define CHM_ITSP_V1_LEN 0x54

#define CHM_MAX_PATHLEN 512

/* structure of PMGL headers */
static const char _chm_pmgl_marker[4] = "PMGL";
#define CHM_PMGL_LEN 0x14
typedef struct pgml_hdr {
    char signature[4];     /*  0 (PMGL) */
    uint32_t free_space;   /*  4 */
    uint32_t unknown_0008; /*  8 */
    int32_t block_prev;    /*  c */
    int32_t block_next;    /* 10 */
} pgml_hdr;

/* structure of LZXC reset table */
#define CHM_LZXC_RESETTABLE_V1_LEN 0x28

/* structure of LZXC control data block */
/*#define CHM_LZXC_MIN_LEN 0x18
#define CHM_LZXC_V2_LEN 0x1c*/
typedef struct chmLzxcControlData {
    uint32_t size;            /*  0        */
    char signature[4];        /*  4 (LZXC) */
    uint32_t version;         /*  8        */
    uint32_t resetInterval;   /*  c        */
    uint32_t windowSize;      /* 10        */
    uint32_t windowsPerReset; /* 14        */
    uint32_t unknown_18;      /* 18        */
} chmLzxcControlData;

void mem_reader_init(mem_reader_ctx* ctx, void* data, int64_t size) {
    ctx->data = data;
    ctx->size = size;
}

int64_t mem_reader(void* ctx_arg, void* buf, int64_t off, int64_t len) {
    mem_reader_ctx* ctx = (mem_reader_ctx*)ctx_arg;
    int64_t toReadMax = ctx->size - off;
    if (toReadMax <= 0) {
        return -1;
    }
    if (len > toReadMax) {
        len = toReadMax;
    }
    char* d = (char*)ctx->data;
    d += off;
    memcpy(buf, d, len);
    return len;
}

int fd_reader_init(fd_reader_ctx* ctx, const char* path) {
    ctx->fd = open(path, O_RDONLY);
    return ctx->fd != -1;
}

void fd_reader_close(fd_reader_ctx* ctx) {
    if (ctx->fd != -1) {
        close(ctx->fd);
    }
}

#if 0
int64_t fd_reader(void* ctx_arg, void* buf, int64_t off, int64_t len) {
  fd_reader_ctx *ctx = (fd_reader_ctx*)ctx_arg;
  if (ctx->fd == -1) {
    return -1;
  }
  return pread64(ctx->fd, buf, (long)len, off);
}
#endif

int64_t fd_reader(void* ctx_arg, void* buf, int64_t off, int64_t len) {
    fd_reader_ctx* ctx = (fd_reader_ctx*)ctx_arg;
    if (ctx->fd == -1) {
        return -1;
    }
    int64_t oldOff = lseek(ctx->fd, 0, SEEK_CUR);
    lseek(ctx->fd, (long)off, SEEK_SET);
    int64_t n = read(ctx->fd, buf, len);
    lseek(ctx->fd, (long)oldOff, SEEK_SET);
    return n;
}

#ifdef WIN32
int win_reader_init(win_reader_ctx* ctx, const WCHAR* path) {
    ctx->fh = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
  return ctx->fh != INVALID_HANDLE_VALUE)
}

void win_reader_close(win_reader_ctx* ctx) {
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

int64_t win_reader(void* ctx_arg, void* buf, int64_t off, int64_t len) {
    win_reader_ctx* ctx = (win_reader_ctx*)ctx_arg;
    int64_t n = 0, oldOs = 0;
    if (h->fd == INVALID_HANDLE_VALUE)
        return -1;

    /* NOTE: this might be better done with CreateFileMapping, et cetera... */
    DWORD origOffsetLo = 0, origOffsetHi = 0;
    DWORD offsetLo, offsetHi;
    DWORD actualLen = 0;

    offsetLo = (unsigned int)(off & 0xffffffffL);
    offsetHi = (unsigned int)((off >> 32) & 0xffffffffL);
    origOffsetLo = SetFilePointer(h->fh, 0, &origOffsetHi, FILE_CURRENT);
    offsetLo = SetFilePointer(h->fh, offsetLo, &offsetHi, FILE_BEGIN);

    if (ReadFile(h->fh, buf, (DWORD)len, &actualLen, NULL) == TRUE)
        n = actualLen;
    else
        n = -1;

    SetFilePointer(h->fh, origOffsetLo, &origOffsetHi, FILE_BEGIN);
    return n;
}
#endif

#if defined(WIN32)
/* TODO: http://download.redis.io/redis-stable/deps/jemalloc/include/msvc_compat/strings.h
https://msdn.microsoft.com/en-us/library/fbxyd7zd.aspx
*/
static int ffs(unsigned int val) {
    int bit = 1, idx = 1;
    while (bit != 0 && (val & bit) == 0) {
        bit <<= 1;
        ++idx;
    }
    if (bit == 0)
        return 0;
    else
        return idx;
}
#endif

static dbgprintfunc g_dbg_print = NULL;

void chm_set_dbgprint(dbgprintfunc f) {
    g_dbg_print = f;
}

static void dbgprintf(const char* fmt, ...) {
    if (g_dbg_print == NULL) {
        return;
    }
    char buf[4096] = {0};
    va_list args;
    va_start(args, fmt);
    /* TODO: vsnprintf_s if MSVC */
    vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    g_dbg_print(buf);
}

#if 0
static void hexprint(uint8_t* d, int n) {
    for (int i = 0; i < n; i++) {
        dbgprintf("%x ", (int)d[i]);
    }
    dbgprintf("\n");
}
#endif

static void memzero(void* d, size_t len) {
    memset(d, 0, len);
}

static int memeq(const void* d1, const void* d2, size_t n) {
    return memcmp(d1, d2, n) == 0;
}

static int streq(const char* s1, const char* s2) {
    return strcasecmp(s1, s2) == 0;
}

typedef struct unmarshaller {
    uint8_t* d;
    int bytesLeft;
    int ok;
} unmarshaller;

static void unmarshaller_init(unmarshaller* u, uint8_t* d, int dLen) {
    u->d = d;
    u->bytesLeft = dLen;
    u->ok = true;
}

static uint8_t* eat_bytes(unmarshaller* u, int n) {
    if (!u->ok) {
        return NULL;
    }
    if (u->bytesLeft < n) {
        u->ok = false;
        return NULL;
    }
    uint8_t* res = u->d;
    u->bytesLeft -= n;
    u->d += n;
    return res;
}

static uint64_t get_uint_n(unmarshaller* u, int nBytesNeeded) {
    uint8_t* d = eat_bytes(u, nBytesNeeded);
    if (d == NULL) {
        return 0;
    }
    uint64_t res = 0;
    for (int i = nBytesNeeded - 1; i >= 0; i--) {
        res <<= 8;
        res |= d[i];
    }
    return res;
}

static uint64_t get_uint64(unmarshaller* u) {
    return get_uint_n(u, (int)sizeof(uint64_t));
}

static uint64_t get_int64(unmarshaller* u) {
    return (int64_t)get_uint64(u);
}

static uint32_t get_uint32(unmarshaller* u) {
    return get_uint_n(u, sizeof(uint32_t));
}

static int32_t get_int32(unmarshaller* u) {
    return (int32_t)get_uint32(u);
}

static void get_pchar(unmarshaller* u, char* dst, int n) {
    uint8_t* d = eat_bytes(u, n);
    if (d == NULL) {
        return;
    }
    memcpy(dst, (char*)d, n);
}

static void get_puchar(unmarshaller* u, uint8_t* dst, int n) {
    uint8_t* d = eat_bytes(u, n);
    if (d == NULL) {
        return;
    }
    memcpy((char*)dst, (char*)d, n);
}

static void get_uuid(unmarshaller* u, uint8_t* dst) {
    get_puchar(u, dst, 16);
}

static int64_t get_cword(unmarshaller* u) {
    int64_t res = 0;
    while (1) {
        uint8_t* d = eat_bytes(u, 1);
        if (NULL == d) {
            return 0;
        }
        uint8_t b = *d;
        res <<= 7;
        if (b >= 0x80) {
            res += b & 0x7f;
        } else {
            return res + b;
        }
    }
}

static bool unmarshal_itsf_header(unmarshaller* u, itsf_hdr* hdr) {
    get_pchar(u, hdr->signature, 4);
    hdr->version = get_int32(u);
    hdr->header_len = get_int32(u);
    hdr->unknown_000c = get_int32(u);
    hdr->last_modified = get_uint32(u);
    hdr->lang_id = get_uint32(u);
    get_uuid(u, hdr->dir_uuid);
    get_uuid(u, hdr->stream_uuid);
    hdr->unknown_offset = get_uint64(u);
    hdr->unknown_len = get_uint64(u);
    hdr->dir_offset = get_uint64(u);
    hdr->dir_len = get_uint64(u);

    int ver = hdr->version;
    if (!(ver == 2 || ver == 3)) {
        dbgprintf("invalid ver %d\n", ver);
        return false;
    }

    if (ver == 3) {
        hdr->data_offset = get_uint64(u);
    } else {
        hdr->data_offset = hdr->dir_offset + hdr->dir_len;
    }

    if (!u->ok) {
        return false;
    }

    /* TODO: should also check UUIDs, probably, though with a version 3 file,
     * current MS tools do not seem to use them.
     */
    if (!memeq(hdr->signature, "ITSF", 4)) {
        return false;
    }

    if (ver == 2 && hdr->header_len < CHM_ITSF_V2_LEN) {
        return false;
    }

    if (ver == 3 && hdr->header_len < CHM_ITSF_V3_LEN) {
        return false;
    }

    /* sanity check (huge values are usually due to broken files) */
    if (hdr->dir_offset > UINT_MAX || hdr->dir_len > UINT_MAX) {
        return false;
    }

    return true;
}

static bool unmarshal_itsp_header(unmarshaller* u, itsp_hdr* hdr) {
    get_pchar(u, hdr->signature, 4);
    hdr->version = get_int32(u);
    hdr->header_len = get_int32(u);
    hdr->unknown_000c = get_int32(u);
    hdr->block_len = get_uint32(u);
    hdr->blockidx_intvl = get_int32(u);
    hdr->index_depth = get_int32(u);
    hdr->index_root = get_int32(u);
    hdr->index_head = get_int32(u);
    hdr->unknown_0024 = get_int32(u);
    hdr->num_blocks = get_uint32(u);
    hdr->unknown_002c = get_int32(u);
    hdr->lang_id = get_uint32(u);
    get_uuid(u, hdr->system_uuid);
    get_puchar(u, hdr->unknown_0044, 16);

    if (!u->ok) {
        return false;
    }
    if (!memeq(hdr->signature, "ITSP", 4)) {
        return false;
    }
    if (hdr->version != 1) {
        return false;
    }
    if (hdr->header_len != CHM_ITSP_V1_LEN) {
        return false;
    }
    if (hdr->block_len == 0) {
        return false;
    }
    return true;
}

static bool unmarshal_pmgl_header(unmarshaller* u, unsigned int blockLen, pgml_hdr* hdr) {
    if (blockLen < CHM_PMGL_LEN)
        return false;

    get_pchar(u, hdr->signature, 4);
    hdr->free_space = get_uint32(u);
    hdr->unknown_0008 = get_uint32(u);
    hdr->block_prev = get_int32(u);
    hdr->block_next = get_int32(u);

    if (!memeq(hdr->signature, _chm_pmgl_marker, 4)) {
        return false;
    }
    if (hdr->free_space > blockLen - CHM_PMGL_LEN) {
        return false;
    }

    return true;
}

static bool unmarshal_lzxc_reset_table(unmarshaller* u, lzxc_reset_table* dest) {
    dest->version = get_uint32(u);
    dest->block_count = get_uint32(u);
    dest->unknown = get_uint32(u);
    dest->table_offset = get_uint32(u);
    dest->uncompressed_len = get_int64(u);
    dest->compressed_len = get_int64(u);
    dest->block_len = get_int64(u);

    if (!u->ok) {
        return false;
    }

    if (dest->version != 2) {
        return false;
    }
    /* sanity check (huge values are usually due to broken files) */
    if (dest->uncompressed_len > UINT_MAX || dest->compressed_len > UINT_MAX) {
        return false;
    }
    if (dest->block_len == 0 || dest->block_len > UINT_MAX) {
        return false;
    }

    return true;
}

static bool unmarshal_lzxc_control_data(unmarshaller* u, chmLzxcControlData* dest) {
    dest->size = get_uint32(u);
    get_pchar(u, dest->signature, 4);
    dest->version = get_uint32(u);
    dest->resetInterval = get_uint32(u);
    dest->windowSize = get_uint32(u);
    dest->windowsPerReset = get_uint32(u);

    dest->unknown_18 = 0;
    if (u->ok && u->bytesLeft >= 4) {
        dest->unknown_18 = get_uint32(u);
    }
    if (!u->ok) {
        return false;
    }

    if (dest->version == 2) {
        dest->resetInterval *= 0x8000;
        dest->windowSize *= 0x8000;
    }
    if (dest->windowSize == 0 || dest->resetInterval == 0) {
        return false;
    }

    /* for now, only support resetInterval a multiple of windowSize/2 */
    if (dest->windowSize == 1) {
        return false;
    }

    if ((dest->resetInterval % (dest->windowSize / 2)) != 0) {
        return false;
    }

    if (!memeq(dest->signature, "LZXC", 4)) {
        return false;
    }

    return true;
}

static bool parse_lzxc_control_data(chm_file* h, chm_entry* e, int64_t n,
                                    chmLzxcControlData* ctl_data) {
    uint8_t buf[256];
    if (n > (int64_t)sizeof(buf)) {
        return false;
    }
    if (chm_retrieve_entry(h, e, buf, 0, n) != n) {
        return false;
    }
    unmarshaller u;
    unmarshaller_init(&u, buf, n);
    return unmarshal_lzxc_control_data(&u, ctl_data);
}

static int64_t read_bytes(chm_file* h, uint8_t* buf, int64_t off, int64_t len) {
    int64_t n = h->read_func(h->read_ctx, buf, off, len);
    /*printf("read_bytes: %d@%d => %d\n", (int)len, (int)off, (int)n); */
    return n;
}

static bool is_null_or_compressed(chm_entry* e) {
    return (e == NULL) || (e->space == CHM_COMPRESSED);
}

static void free_entries(chm_entry* first) {
    chm_entry* next;
    chm_entry* e = first;
    while (e != NULL) {
        next = e->next;
        free(e);
        e = next;
    }
}

/* close an ITS archive */
void chm_close(chm_file* h) {
    if (h == NULL) {
        return;
    }

    if (h->lzx_state)
        LZXteardown(h->lzx_state);

    for (int i = 0; i < h->cache_num_blocks; i++) {
        free(h->cache_blocks[i]);
    }
    if (h->entries != NULL) {
        if (h->n_entries > 0) {
            free_entries(h->entries[0]);
        }
        free(h->entries);
    }
}

/*
 *  how many decompressed blocks should be cached?  A simple
 *  caching scheme is used, wherein the index of the block is
 *  used as a hash value, and hash collision results in the
 *  invalidation of the previously cached block.
 */
void chm_set_cache_size(chm_file* h, int nCacheBlocks) {
    if (nCacheBlocks == h->cache_num_blocks) {
        return;
    }
    if (nCacheBlocks > MAX_CACHE_BLOCKS) {
        nCacheBlocks = MAX_CACHE_BLOCKS;
    }
    uint8_t* newBlocks[MAX_CACHE_BLOCKS] = {0};
    int64_t newIndices[MAX_CACHE_BLOCKS] = {0};

    /* re-distribute old cached blocks */
    for (int i = 0; i < h->cache_num_blocks; i++) {
        int newSlot = (int)(h->cache_block_indices[i] % nCacheBlocks);

        if (h->cache_blocks[i]) {
            /* in case of collision, destroy newcomer */
            if (newBlocks[newSlot]) {
                free(h->cache_blocks[i]);
                h->cache_blocks[i] = NULL;
            } else {
                newBlocks[newSlot] = h->cache_blocks[i];
                newIndices[newSlot] = h->cache_block_indices[i];
            }
        }
    }

    memcpy(h->cache_blocks, newBlocks, sizeof(newBlocks));
    memcpy(h->cache_block_indices, newIndices, sizeof(newIndices));
    h->cache_num_blocks = nCacheBlocks;
}

static uint8_t* get_cached_block(chm_file* h, int64_t nBlock) {
    int idx = (int)nBlock % h->cache_num_blocks;
    if (h->cache_blocks[idx] != NULL && h->cache_block_indices[idx] == nBlock) {
        return h->cache_blocks[idx];
    }
    return NULL;
}

static uint8_t* alloc_cached_block(chm_file* h, int64_t nBlock) {
    int idx = (int)(nBlock % h->cache_num_blocks);
    if (!h->cache_blocks[idx]) {
        size_t blockSize = h->reset_table.block_len;
        h->cache_blocks[idx] = (uint8_t*)malloc(blockSize);
    }
    if (h->cache_blocks[idx]) {
        h->cache_block_indices[idx] = nBlock;
    }
    return h->cache_blocks[idx];
}

static int flags_from_path(char* path) {
    int flags = 0;
    size_t n = strlen(path);

    if (path[n - 1] == '/')
        flags |= CHM_ENUMERATE_DIRS;
    else
        flags |= CHM_ENUMERATE_FILES;

    if (n > 0 && path[0] == '/') {
        if (n > 1 && (path[1] == '#' || path[1] == '$'))
            flags |= CHM_ENUMERATE_SPECIAL;
        else
            flags |= CHM_ENUMERATE_NORMAL;
    } else
        flags |= CHM_ENUMERATE_META;
    return flags;
}

static chm_entry* parse_pmgl_entry(unmarshaller* u) {
    size_t pathLen = (size_t)get_cword(u);
    if (pathLen > CHM_MAX_PATHLEN || !u->ok) {
        return NULL;
    }
    size_t n = sizeof(chm_entry) + pathLen + 1; /* +1 to nul-terminate */
    chm_entry* e = (chm_entry*)calloc(1, n);
    if (e == NULL) {
        return NULL;
    }
    e->path = (char*)e + sizeof(chm_entry);
    get_pchar(u, e->path, (int)pathLen);
    e->space = (int)get_cword(u);
    e->start = get_cword(u);
    e->length = get_cword(u);
    if (!u->ok) {
        return NULL;
    }
    e->flags = flags_from_path(e->path);
    return e;
}

static bool get_int64_at_off(chm_file* h, int64_t off, int64_t* n_out) {
    uint8_t buf[8];
    if (read_bytes(h, buf, off, 8) != 8) {
        return false;
    }
    unmarshaller u;
    unmarshaller_init(&u, buf, 8);
    uint64_t n = get_int64(&u);
    if (!u.ok) {
        return false;
    }
    *n_out = n;
    return true;
}

/* get the bounds of a compressed block.  return false on failure */
static bool get_cmpblock_bounds(chm_file* h, int64_t block, int64_t* start, int64_t* len) {
    int64_t off = (int64_t)h->itsf.data_offset + (int64_t)h->rt_unit->start +
                  (int64_t)h->reset_table.table_offset + (int64_t)block * 8;
    if (!get_int64_at_off(h, off, start)) {
        return false;
    }

    /* for all but the last block, use the reset table */
    int64_t end = h->reset_table.compressed_len;
    if (block < h->reset_table.block_count - 1) {
        off += 8;
        if (!get_int64_at_off(h, off, &end)) {
            return false;
        }
    }

    *len = end - *start;
    *start += h->itsf.data_offset + h->cn_unit->start;
    return true;
}

static uint8_t* uncompress_block(chm_file* h, int64_t nBlock) {
    size_t blockSize = h->reset_table.block_len;
    // TODO: cache buf on chm_file

    if (h->lzx_last_block == nBlock) {
        return h->lzx_last_block_data;
    }

    if (nBlock % h->reset_blkcount == 0) {
        LZXreset(h->lzx_state);
    }

    uint8_t* buf = malloc(blockSize + 6144);
    if (buf == NULL)
        return NULL;

    uint8_t* uncompressed = alloc_cached_block(h, nBlock);
    if (!uncompressed) {
        goto Error;
    }

    dbgprintf("Decompressing block #%4d (EXTRA)\n", nBlock);
    int64_t cmpStart, cmpLen;
    if (!get_cmpblock_bounds(h, nBlock, &cmpStart, &cmpLen)) {
        goto Error;
    }
    if (cmpLen < 0 || cmpLen > (int64_t)blockSize + 6144) {
        goto Error;
    }

    if (read_bytes(h, buf, cmpStart, cmpLen) != cmpLen) {
        goto Error;
    }

    int res = LZXdecompress(h->lzx_state, buf, uncompressed, (int)cmpLen, (int)blockSize);
    if (res != DECR_OK) {
        dbgprintf("   (DECOMPRESS FAILED!)\n");
        goto Error;
    }

    h->lzx_last_block = nBlock;
    h->lzx_last_block_data = uncompressed;
    free(buf);
    return uncompressed;
Error:
    free(buf);
    return NULL;
}

static int64_t decompress_block(chm_file* h, int64_t nBlock, uint8_t** ubuffer) {
    uint32_t blockAlign = ((uint32_t)nBlock % h->reset_blkcount); /* reset intvl. aln. */

    /* let the caching system pull its weight! */
    if (nBlock - blockAlign <= h->lzx_last_block && nBlock >= h->lzx_last_block)
        blockAlign = ((uint32_t)nBlock - h->lzx_last_block);

    /* check if we need previous blocks */
    if (blockAlign != 0) {
        /* fetch all required previous blocks since last reset */
        for (uint32_t i = blockAlign; i > 0; i--) {
            uint8_t* d = uncompress_block(h, nBlock - i);
            if (!d) {
                return 0;
            }
        }
    }
    *ubuffer = uncompress_block(h, nBlock);
    if (!*ubuffer) {
        return 0;
    }

    /* XXX: modify LZX routines to return the length of the data they
     * decompressed and return that instead, for an extra sanity check.
     */
    return h->reset_table.block_len;
}

/* grab a region from a compressed block */
static int64_t decompress_region(chm_file* h, uint8_t* buf, int64_t start, int64_t len) {
    uint8_t* ubuffer;

    if (len <= 0)
        return (int64_t)0;

    /* figure out what we need to read */
    int64_t nBlock = start / h->reset_table.block_len;
    int64_t nOffset = start % h->reset_table.block_len;
    int64_t nLen = len;
    if (nLen > (h->reset_table.block_len - nOffset))
        nLen = h->reset_table.block_len - nOffset;

    uint8_t* cached_block = get_cached_block(h, nBlock);
    if (cached_block != NULL) {
        memcpy(buf, cached_block + nOffset, (size_t)nLen);
        return nLen;
    }

    if (!h->lzx_state) {
        int window_size = ffs(h->window_size) - 1;
        h->lzx_last_block = -1;
        h->lzx_state = LZXinit(window_size);
    }

    int64_t gotLen = decompress_block(h, nBlock, &ubuffer);
    if (gotLen == (int64_t)-1) {
        return 0;
    }
    if (gotLen < nLen)
        nLen = gotLen;
    memcpy(buf, ubuffer + nOffset, (unsigned int)nLen);
    return nLen;
}

int64_t chm_retrieve_entry(chm_file* h, chm_entry* e, unsigned char* buf, int64_t addr,
                           int64_t len) {
    if (h == NULL)
        return (int64_t)0;

    /* starting address must be in correct range */
    if (addr >= e->length)
        return (int64_t)0;

    /* clip length */
    if (addr + len > e->length)
        len = e->length - addr;

    if (e->space == CHM_UNCOMPRESSED) {
        return read_bytes(h, buf, (int64_t)h->itsf.data_offset + (int64_t)e->start + (int64_t)addr,
                          len);
    }
    if (e->space != CHM_COMPRESSED) {
        return 0;
    }

    int64_t swath = 0, total = 0;

    /* if compression is not enabled for this file... */
    if (!h->compression_enabled)
        return total;

    do {
        swath = decompress_region(h, buf, e->start + addr, len);

        if (swath == 0)
            return total;

        /* update stats */
        total += swath;
        len -= swath;
        addr += swath;
        buf += swath;

    } while (len != 0);

    return total;
}

static bool parse_entries(chm_file* h) {
    pgml_hdr pgml;

    int n_entries = 0;
    chm_entry* e;
    chm_entry* last_entry = NULL;
    uint8_t* buf = malloc((size_t)h->itsp.block_len);
    if (buf == NULL) {
        goto Error;
    }

    int32_t cur_page = h->itsp.index_head;

    while (cur_page != -1) {
        int64_t n = h->itsp.block_len;
        if (read_bytes(h, buf, (int64_t)h->dir_offset + (int64_t)cur_page * n, n) != n) {
            goto Error;
        }

        unmarshaller u;
        unmarshaller_init(&u, buf, n);
        if (!unmarshal_pmgl_header(&u, h->itsp.block_len, &pgml)) {
            goto Error;
        }
        u.bytesLeft -= pgml.free_space;

        /* decode all entries in this page */
        while (u.bytesLeft > 0) {
            e = parse_pmgl_entry(&u);
            if (e == NULL) {
                goto Error;
            }
            e->next = last_entry;
            last_entry = e;
            n_entries++;
        }
        cur_page = pgml.block_next;
    }
    if (0 == n_entries) {
        goto Error;
    }

Exit:
    if (n_entries > 0) {
        h->n_entries = n_entries;
        chm_entry** entries = (chm_entry**)calloc(n_entries, sizeof(chm_entry*));
        if (entries != NULL) {
            h->entries = entries;
            e = last_entry;
            int n = n_entries - 1;
            while (e != NULL) {
                entries[n] = e;
                --n;
                e = e->next;
            }
        }
    }
    free(buf);
    if (h->parse_entries_failed || n_entries == 0) {
        return false;
    }
    return true;
Error:
    h->parse_entries_failed = true;
    goto Exit;
}

static bool parse_lzxc_reset_table(chm_file* h) {
    /* read reset table info */
    if (!h->compression_enabled) {
        return true;
    }
    int64_t n = CHM_LZXC_RESETTABLE_V1_LEN;
    uint8_t buf[CHM_LZXC_RESETTABLE_V1_LEN];
    if (chm_retrieve_entry(h, h->rt_unit, buf, 0, n) != n) {
        h->compression_enabled = false;
        return false;
    }
    unmarshaller u;
    unmarshaller_init(&u, buf, n);
    if (!unmarshal_lzxc_reset_table(&u, &h->reset_table)) {
        h->compression_enabled = false;
        return false;
    }
    return true;
}

bool chm_parse(chm_file* h, chm_reader read_func, void* read_ctx) {
    unsigned char buf[256];
    unsigned int n;
    chm_entry* uiLzxc = NULL;
    chmLzxcControlData ctlData;
    unmarshaller u;

    memzero(h, sizeof(chm_file));
    h->read_func = read_func;
    h->read_ctx = read_ctx;

    /* read and verify header */
    n = CHM_ITSF_V3_LEN;
    if (read_bytes(h, buf, 0, n) != n) {
        goto Error;
    }

    unmarshaller_init(&u, (uint8_t*)buf, n);
    if (!unmarshal_itsf_header(&u, &h->itsf)) {
        dbgprintf("unmarshal_itsf_header() failed\n");
        goto Error;
    }

    n = CHM_ITSP_V1_LEN;
    if (read_func(read_ctx, buf, (int64_t)h->itsf.dir_offset, n) != n) {
        goto Error;
    }
    unmarshaller_init(&u, (uint8_t*)buf, n);
    if (!unmarshal_itsp_header(&u, &h->itsp)) {
        goto Error;
    }

    h->dir_offset = h->itsf.dir_offset;
    h->dir_offset += h->itsp.header_len;

    h->dir_len = h->itsf.dir_len;
    h->dir_len -= h->itsp.header_len;

    /* if the index root is -1, this means we don't have any PMGI blocks.
     * as a result, we must use the sole PMGL block as the index root
     */
    if (h->itsp.index_root <= -1)
        h->itsp.index_root = h->itsp.index_head;

    h->compression_enabled = true;

    parse_entries(h);
    if (h->n_entries == 0) {
        goto Error;
    }

    for (int i = 0; i < h->n_entries; i++) {
        chm_entry* e = h->entries[i];
        if (streq(e->path, CHMU_RESET_TABLE)) {
            h->rt_unit = e;
        } else if (streq(e->path, CHMU_CONTENT)) {
            h->cn_unit = e;
        } else if (streq(e->path, CHMU_LZXC_CONTROLDATA)) {
            uiLzxc = e;
        }
    }
    if (is_null_or_compressed(h->rt_unit) || is_null_or_compressed(h->cn_unit) ||
        is_null_or_compressed(uiLzxc)) {
        h->compression_enabled = false;
    }

    parse_lzxc_reset_table(h);

    /* read control data */
    if (h->compression_enabled) {
        int64_t n2 = (int64_t)uiLzxc->length;

        if (!parse_lzxc_control_data(h, uiLzxc, n2, &ctlData)) {
            h->compression_enabled = false;
        } else {
            /* prevent division by zero */
            h->window_size = ctlData.windowSize;
            h->reset_interval = ctlData.resetInterval;
            h->reset_blkcount = h->reset_interval / (h->window_size / 2) * ctlData.windowsPerReset;
        }
    }
    chm_set_cache_size(h, CHM_MAX_BLOCKS_CACHED);

    return true;
Error:
    chm_close(h);
    return false;
}

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
 * switches (Linux only):                                                  *
 *              CHM_USE_PREAD: compile library to use pread instead of     *
 *                             lseek/read                                  *
 *              CHM_USE_IO64:  compile library to support full 64-bit I/O  *
 *                             as is needed to properly deal with the      *
 *                             64-bit file offsets.                        *
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

#ifdef WIN32
#include <windows.h>
#include <malloc.h>
#define strcasecmp stricmp
#define strncasecmp strnicmp
#else
/* basic Linux system includes */
/* #define _XOPEN_SOURCE 500 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/* #include <dmalloc.h> */
#endif

#include "chm_lib.h"
#include "lzx.h"

//#define CHM_DEBUG 1

#ifndef CHM_MAX_BLOCKS_CACHED
#define CHM_MAX_BLOCKS_CACHED 5
#endif

/* names of sections essential to decompression */
static const char CHMU_RESET_TABLE[] =
    "::DataSpace/Storage/MSCompressed/Transform/"
    "{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/"
    "InstanceData/ResetTable";
static const char CHMU_LZXC_CONTROLDATA[] = "::DataSpace/Storage/MSCompressed/ControlData";
static const char CHMU_CONTENT[] = "::DataSpace/Storage/MSCompressed/Content";

/* structure of ITSF headers */
#define CHM_ITSF_V2_LEN 0x58
#define CHM_ITSF_V3_LEN 0x60
typedef struct itsf_hdr {
    char signature[4];       /*  0 (ITSF) */
    int32_t version;         /*  4 */
    int32_t header_len;      /*  8 */
    int32_t unknown_000c;    /*  c */
    uint32_t last_modified;  /* 10 */
    uint32_t lang_id;        /* 14 */
    uint8_t dir_uuid[16];    /* 18 */
    uint8_t stream_uuid[16]; /* 28 */
    int64_t unknown_offset;  /* 38 */
    int64_t unknown_len;     /* 40 */
    int64_t dir_offset;      /* 48 */
    int64_t dir_len;         /* 50 */
    int64_t data_offset;     /* 58 (Not present before V3) */
} itsf_hdr;

/* structure of ITSP headers */
#define CHM_ITSP_V1_LEN 0x54
typedef struct itsp_hdr {
    char signature[4];        /*  0 (ITSP) */
    int32_t version;          /*  4 */
    int32_t header_len;       /*  8 */
    int32_t unknown_000c;     /*  c */
    uint32_t block_len;       /* 10 */
    int32_t blockidx_intvl;   /* 14 */
    int32_t index_depth;      /* 18 */
    int32_t index_root;       /* 1c */
    int32_t index_head;       /* 20 */
    int32_t unknown_0024;     /* 24 */
    uint32_t num_blocks;      /* 28 */
    int32_t unknown_002c;     /* 2c */
    uint32_t lang_id;         /* 30 */
    uint8_t system_uuid[16];  /* 34 */
    uint8_t unknown_0044[16]; /* 44 */
} itsp_hdr;

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
struct chmLzxcResetTable {
    uint32_t version;
    uint32_t block_count;
    uint32_t unknown;
    uint32_t table_offset;
    int64_t uncompressed_len;
    int64_t compressed_len;
    int64_t block_len;
}; /* __attribute__ ((aligned (1))); */

/* structure of LZXC control data block */
#define CHM_LZXC_MIN_LEN 0x18
#define CHM_LZXC_V2_LEN 0x1c
struct chmLzxcControlData {
    uint32_t size;            /*  0        */
    char signature[4];        /*  4 (LZXC) */
    uint32_t version;         /*  8        */
    uint32_t resetInterval;   /*  c        */
    uint32_t windowSize;      /* 10        */
    uint32_t windowsPerReset; /* 14        */
    uint32_t unknown_18;      /* 18        */
};

#define MAX_CACHE_BLOCKS 128

/* the structure used for chm file handles */
typedef struct chm_file {
#ifdef WIN32
    HANDLE fd;
#else
    int fd;
#endif

    itsf_hdr itsf;
    itsp_hdr itsp;

    int64_t dir_offset;
    int64_t dir_len;

    chm_unit_info rt_unit;
    chm_unit_info cn_unit;
    struct chmLzxcResetTable reset_table;

    /* LZX control data */
    int compression_enabled;
    uint32_t window_size;
    uint32_t reset_interval;
    uint32_t reset_blkcount;

    /* decompressor state */
    struct LZXstate* lzx_state;
    int lzx_last_block;
    uint8_t* lzx_last_block_data;

    /* cache for decompressed blocks */
    uint8_t* cache_blocks[MAX_CACHE_BLOCKS];
    int64_t cache_block_indices[MAX_CACHE_BLOCKS];
    int cache_num_blocks;

    chm_entry **entries_cached;
    int n_entries_cached;
} chm_file;

/* structure of PMGI headers */
static const char _chm_pmgi_marker[4] = "PMGI";
#define CHM_PMGI_LEN 0x08
struct chmPmgiHeader {
    char signature[4];   /*  0 (PMGI) */
    uint32_t free_space; /*  4 */
};                       /* __attribute__ ((aligned (1))); */

#if defined(WIN32)
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

#if defined(CHM_DEBUG)
#define dbgprintf(...) fprintf(stderr, __VA_ARGS__);
#else
static void dbgprintf(const char* fmt, ...) {
    (void)fmt;
}
#endif

static int memeq(const void* d1, const void* d2, size_t n) {
    return memcmp(d1, d2, n) == 0;
}

#if 0
static void hexprint(uint8_t* d, int n) {
    for (int i = 0; i < n; i++) {
        dbgprintf("%x ", (int)d[i]);
    }
    dbgprintf("\n");
}
#endif

typedef struct unmarshaller {
    uint8_t* d;
    int bytesLeft;
    int err;
} unmarshaller;

static void unmarshaller_init(unmarshaller* u, uint8_t* d, int dLen) {
    u->d = d;
    u->bytesLeft = dLen;
    u->err = 0;
}

static uint8_t* eat_bytes(unmarshaller* u, int n) {
    if (u->err != 0) {
        return NULL;
    }
    if (u->bytesLeft < n) {
        u->err = 1;
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

#if 0
static uint64_t get_int64(unmarshaller* u) {
    return (int64_t)get_uint64(u);
}
#endif

static uint32_t get_uint32(unmarshaller* u) {
    return get_uint_n(u, sizeof(uint32_t));
}

static int32_t get_int32(unmarshaller* u) {
    return (int32_t)get_uint32(u);
}

static void get_pchar(unmarshaller* u, char* dst, int nBytes) {
    uint8_t* d = eat_bytes(u, nBytes);
    if (d == NULL) {
        return;
    }
    memcpy(dst, (char*)d, nBytes);
}

static void get_puchar(unmarshaller* u, uint8_t* dst, int nBytes) {
    uint8_t* d = eat_bytes(u, nBytes);
    if (d == NULL) {
        return;
    }
    memcpy((char*)dst, (char*)d, nBytes);
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

/* utilities for unmarshalling data */
static int _unmarshal_char_array(unsigned char** pData, unsigned int* pLenRemain, char* dest,
                                 int count) {
    if (count <= 0 || (unsigned int)count > *pLenRemain)
        return 0;
    memcpy(dest, (*pData), count);
    *pData += count;
    *pLenRemain -= count;
    return 1;
}

static int _unmarshal_int32(unsigned char** pData, unsigned int* pLenRemain, int32_t* dest) {
    if (4 > *pLenRemain)
        return 0;
    *dest = (*pData)[0] | (*pData)[1] << 8 | (*pData)[2] << 16 | (*pData)[3] << 24;
    *pData += 4;
    *pLenRemain -= 4;
    return 1;
}

static int _unmarshal_uint32(unsigned char** pData, unsigned int* pLenRemain, uint32_t* dest) {
    if (4 > *pLenRemain)
        return 0;
    *dest = (*pData)[0] | (*pData)[1] << 8 | (*pData)[2] << 16 | (*pData)[3] << 24;
    *pData += 4;
    *pLenRemain -= 4;
    return 1;
}

static int _unmarshal_int64(unsigned char** pData, unsigned int* pLenRemain, int64_t* dest) {
    int64_t temp;
    int i;
    if (8 > *pLenRemain)
        return 0;
    temp = 0;
    for (i = 8; i > 0; i--) {
        temp <<= 8;
        temp |= (*pData)[i - 1];
    }
    *dest = temp;
    *pData += 8;
    *pLenRemain -= 8;
    return 1;
}

static int _unmarshal_uint64(unsigned char** pData, unsigned int* pLenRemain, int64_t* dest) {
    int64_t temp;
    int i;
    if (8 > *pLenRemain)
        return 0;
    temp = 0;
    for (i = 8; i > 0; i--) {
        temp <<= 8;
        temp |= (*pData)[i - 1];
    }
    *dest = temp;
    *pData += 8;
    *pLenRemain -= 8;
    return 1;
}

/* returns 0 on error */
static int unmarshal_itsf_header(unmarshaller* u, itsf_hdr* hdr) {
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
        return 0;
    }

    if (ver == 3) {
        hdr->data_offset = get_uint64(u);
    } else {
        hdr->data_offset = hdr->dir_offset + hdr->dir_len;
    }

    if (u->err != 0) {
        return 0;
    }

    /* TODO: should also check UUIDs, probably, though with a version 3 file,
     * current MS tools do not seem to use them.
     */
    if (!memeq(hdr->signature, "ITSF", 4)) {
        return 0;
    }

    if (ver == 2 && hdr->header_len < CHM_ITSF_V2_LEN) {
        return 0;
    }

    if (ver == 3 && hdr->header_len < CHM_ITSF_V3_LEN) {
        return 0;
    }

    /* SumatraPDF: sanity check (huge values are usually due to broken files) */
    if (hdr->dir_offset > UINT_MAX || hdr->dir_len > UINT_MAX) {
        return 0;
    }

    return 1;
}

static int unmarshal_itsp_header(unmarshaller* u, itsp_hdr* hdr) {
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

    if (u->err != 0) {
        return 0;
    }
    if (!memeq(hdr->signature, "ITSP", 4)) {
        return 0;
    }
    if (hdr->version != 1) {
        return 0;
    }
    if (hdr->header_len != CHM_ITSP_V1_LEN) {
        return 0;
    }
    /* SumatraPDF: sanity check */
    if (hdr->block_len == 0) {
        return 0;
    }
    return 1;
}

static int unmarshal_pmgl_header(unmarshaller* u, unsigned int blockLen, pgml_hdr* hdr) {
    /* SumatraPDF: sanity check */
    if (blockLen < CHM_PMGL_LEN)
        return 0;

    get_pchar(u, hdr->signature, 4);
    hdr->free_space = get_uint32(u);
    hdr->unknown_0008 = get_uint32(u);
    hdr->block_prev = get_int32(u);
    hdr->block_next = get_int32(u);

    if (!memeq(hdr->signature, _chm_pmgl_marker, 4)) {
        return 0;
    }
    /* SumatraPDF: sanity check */
    if (hdr->free_space > blockLen - CHM_PMGL_LEN) {
        return 0;
    }

    return 1;
}

static int _unmarshal_pmgl_header(unsigned char** pData, unsigned int* pDataLen,
                                  unsigned int blockLen, pgml_hdr* dest) {
    /* we only know how to deal with a 0x14 byte structures */
    if (*pDataLen != CHM_PMGL_LEN)
        return 0;
    /* SumatraPDF: sanity check */
    if (blockLen < CHM_PMGL_LEN)
        return 0;

    /* unmarshal fields */
    _unmarshal_char_array(pData, pDataLen, dest->signature, 4);
    _unmarshal_uint32(pData, pDataLen, &dest->free_space);
    _unmarshal_uint32(pData, pDataLen, &dest->unknown_0008);
    _unmarshal_int32(pData, pDataLen, &dest->block_prev);
    _unmarshal_int32(pData, pDataLen, &dest->block_next);

    if (!memeq(dest->signature, _chm_pmgl_marker, 4))
        return 0;

    /* SumatraPDF: sanity check */
    if (dest->free_space > blockLen - CHM_PMGL_LEN)
        return 0;

    return 1;
}

static int _unmarshal_pmgi_header(unsigned char** pData, unsigned int* pDataLen,
                                  unsigned int blockLen, struct chmPmgiHeader* dest) {
    /* we only know how to deal with a 0x8 byte structures */
    if (*pDataLen != CHM_PMGI_LEN)
        return 0;
    /* SumatraPDF: sanity check */
    if (blockLen < CHM_PMGI_LEN)
        return 0;

    /* unmarshal fields */
    _unmarshal_char_array(pData, pDataLen, dest->signature, 4);
    _unmarshal_uint32(pData, pDataLen, &dest->free_space);

    if (!memeq(dest->signature, _chm_pmgi_marker, 4))
        return 0;
    /* SumatraPDF: sanity check */
    if (dest->free_space > blockLen - CHM_PMGI_LEN)
        return 0;

    return 1;
}

static int _unmarshal_lzxc_reset_table(unsigned char** pData, unsigned int* pDataLen,
                                       struct chmLzxcResetTable* dest) {
    /* we only know how to deal with a 0x28 byte structures */
    if (*pDataLen != CHM_LZXC_RESETTABLE_V1_LEN)
        return 0;

    /* unmarshal fields */
    _unmarshal_uint32(pData, pDataLen, &dest->version);
    _unmarshal_uint32(pData, pDataLen, &dest->block_count);
    _unmarshal_uint32(pData, pDataLen, &dest->unknown);
    _unmarshal_uint32(pData, pDataLen, &dest->table_offset);
    _unmarshal_uint64(pData, pDataLen, &dest->uncompressed_len);
    _unmarshal_uint64(pData, pDataLen, &dest->compressed_len);
    _unmarshal_uint64(pData, pDataLen, &dest->block_len);

    /* check structure */
    if (dest->version != 2)
        return 0;
    /* SumatraPDF: sanity check (huge values are usually due to broken files) */
    if (dest->uncompressed_len > UINT_MAX || dest->compressed_len > UINT_MAX)
        return 0;
    if (dest->block_len == 0 || dest->block_len > UINT_MAX)
        return 0;

    return 1;
}

static int _unmarshal_lzxc_control_data(unsigned char** pData, unsigned int* pDataLen,
                                        struct chmLzxcControlData* dest) {
    /* we want at least 0x18 bytes */
    if (*pDataLen < CHM_LZXC_MIN_LEN)
        return 0;

    /* unmarshal fields */
    _unmarshal_uint32(pData, pDataLen, &dest->size);
    _unmarshal_char_array(pData, pDataLen, dest->signature, 4);
    _unmarshal_uint32(pData, pDataLen, &dest->version);
    _unmarshal_uint32(pData, pDataLen, &dest->resetInterval);
    _unmarshal_uint32(pData, pDataLen, &dest->windowSize);
    _unmarshal_uint32(pData, pDataLen, &dest->windowsPerReset);

    if (*pDataLen >= CHM_LZXC_V2_LEN)
        _unmarshal_uint32(pData, pDataLen, &dest->unknown_18);
    else
        dest->unknown_18 = 0;

    if (dest->version == 2) {
        dest->resetInterval *= 0x8000;
        dest->windowSize *= 0x8000;
    }
    if (dest->windowSize == 0 || dest->resetInterval == 0)
        return 0;

    /* for now, only support resetInterval a multiple of windowSize/2 */
    if (dest->windowSize == 1)
        return 0;
    if ((dest->resetInterval % (dest->windowSize / 2)) != 0)
        return 0;

    if (!memeq(dest->signature, "LZXC", 4))
        return 0;

    return 1;
}

#ifdef WIN32
static void close_file(HANDLE h) {
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

static int64_t read_bytes(chm_file* h, uint8_t* buf, int64_t os, int64_t len) {
    int64_t readLen = 0, oldOs = 0;
    if (h->fd == INVALID_HANDLE_VALUE)
        return readLen;

    /* NOTE: this might be better done with CreateFileMapping, et cetera... */
    DWORD origOffsetLo = 0, origOffsetHi = 0;
    DWORD offsetLo, offsetHi;
    DWORD actualLen = 0;

    /* awkward Win32 Seek/Tell */
    offsetLo = (unsigned int)(os & 0xffffffffL);
    offsetHi = (unsigned int)((os >> 32) & 0xffffffffL);
    origOffsetLo = SetFilePointer(h->fd, 0, &origOffsetHi, FILE_CURRENT);
    offsetLo = SetFilePointer(h->fd, offsetLo, &offsetHi, FILE_BEGIN);

    /* read the data */
    if (ReadFile(h->fd, buf, (DWORD)len, &actualLen, NULL) == TRUE)
        readLen = actualLen;
    else
        readLen = 0;

    /* restore original position */
    SetFilePointer(h->fd, origOffsetLo, &origOffsetHi, FILE_BEGIN);
    return readLen;
}
#else
static void close_file(int fd) {
    if (fd != -1) {
        close(-1);
    }
}

static int64_t read_bytes(chm_file* h, uint8_t* buf, int64_t os, int64_t len) {
    int64_t readLen = 0, oldOs = 0;
    if (h->fd == -1)
        return readLen;

#ifdef CHM_USE_PREAD
#ifdef CHM_USE_IO64
    readLen = pread64(h->fd, buf, (long)len, os);
#else
    readLen = pread(h->fd, buf, (long)len, (unsigned int)os);
#endif
#else
#ifdef CHM_USE_IO64
    oldOs = lseek64(h->fd, 0, SEEK_CUR);
    lseek64(h->fd, os, SEEK_SET);
    readLen = read(h->fd, buf, len);
    lseek64(h->fd, oldOs, SEEK_SET);
#else
    oldOs = lseek(h->fd, 0, SEEK_CUR);
    lseek(h->fd, (long)os, SEEK_SET);
    readLen = read(h->fd, buf, len);
    lseek(h->fd, (long)oldOs, SEEK_SET);
#endif
#endif
    return readLen;
}
#endif

/* open an ITS archive */
#ifdef PPC_BSTR
chm_file* chm_open(BSTR filename)
#else
chm_file* chm_open(const char* filename)
#endif
{
    unsigned char buf[256];
    unsigned int n;
    unsigned char* tmp;
    chm_file* h = NULL;
    chm_unit_info uiLzxc;
    struct chmLzxcControlData ctlData;
    unmarshaller u;

    /* allocate handle */
    h = (chm_file*)calloc(1, sizeof(chm_file));
    if (h == NULL)
        return NULL;

/* open file */
#ifdef WIN32
#ifdef PPC_BSTR
    if ((h->fd = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
        free(h);
        return NULL;
    }
#else
    if ((h->fd = CreateFileA(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                             NULL)) == INVALID_HANDLE_VALUE) {
        free(h);
        return NULL;
    }
#endif
#else
    h->fd = open(filename, O_RDONLY);
    if (h->fd == -1) {
        free(h);
        return NULL;
    }
#endif

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
    if (read_bytes(h, buf, (int64_t)h->itsf.dir_offset, n) != n) {
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

    /* By default, compression is enabled. */
    h->compression_enabled = 1;

    /* prefetch most commonly needed unit infos */
    if (CHM_RESOLVE_SUCCESS != chm_resolve_object(h, CHMU_RESET_TABLE, &h->rt_unit) ||
        h->rt_unit.space == CHM_COMPRESSED ||
        CHM_RESOLVE_SUCCESS != chm_resolve_object(h, CHMU_CONTENT, &h->cn_unit) ||
        h->cn_unit.space == CHM_COMPRESSED ||
        CHM_RESOLVE_SUCCESS != chm_resolve_object(h, CHMU_LZXC_CONTROLDATA, &uiLzxc) ||
        uiLzxc.space == CHM_COMPRESSED) {
        h->compression_enabled = 0;
    }

    /* read reset table info */
    if (h->compression_enabled) {
        n = CHM_LZXC_RESETTABLE_V1_LEN;
        tmp = buf;
        if (chm_retrieve_object(h, &h->rt_unit, buf, 0, n) != n ||
            !_unmarshal_lzxc_reset_table(&tmp, &n, &h->reset_table)) {
            h->compression_enabled = 0;
        }
    }

    /* read control data */
    if (h->compression_enabled) {
        n = (unsigned int)uiLzxc.length;
        if (uiLzxc.length > (int64_t)sizeof(buf)) {
            goto Error;
        }

        tmp = buf;
        if (chm_retrieve_object(h, &uiLzxc, buf, 0, n) != n ||
            !_unmarshal_lzxc_control_data(&tmp, &n, &ctlData)) {
            h->compression_enabled = 0;
        } else /* SumatraPDF: prevent division by zero */
        {
            h->window_size = ctlData.windowSize;
            h->reset_interval = ctlData.resetInterval;

            h->reset_blkcount = h->reset_interval / (h->window_size / 2) * ctlData.windowsPerReset;
        }
    }

    chm_set_cache_size(h, CHM_MAX_BLOCKS_CACHED);
    return h;
Error:
    chm_close(h);
    return NULL;
}

static void free_entries(chm_entry *first) {
  chm_entry *next;
  chm_entry *e = first;
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
    close_file(h->fd);

    if (h->lzx_state)
        LZXteardown(h->lzx_state);

    for (int i = 0; i < h->cache_num_blocks; i++) {
        free(h->cache_blocks[i]);
    }
    if (h->entries_cached != NULL) {
      free_entries(h->entries_cached[0]);
      free(h->entries_cached);
    }
    free(h);
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

/* skip a compressed dword */
static void _chm_skip_cword(uint8_t** pEntry) {
    while (*(*pEntry)++ >= 0x80) {
    }
}

/* skip the data from a PMGL entry */
static void _chm_skip_PMGL_entry_data(uint8_t** pEntry) {
    _chm_skip_cword(pEntry);
    _chm_skip_cword(pEntry);
    _chm_skip_cword(pEntry);
}

/* parse a compressed dword */
static int64_t _chm_parse_cword(uint8_t** pEntry) {
    int64_t accum = 0;
    uint8_t temp;
    while ((temp = *(*pEntry)++) >= 0x80) {
        accum <<= 7;
        accum += temp & 0x7f;
    }

    return (accum << 7) + temp;
}

/* parse a utf-8 string into an ASCII char buffer */
static int _chm_parse_UTF8(uint8_t** pEntry, int64_t count, char* path) {
    /* XXX: implement UTF-8 support, including a real mapping onto
     *      ISO-8859-1?  probably there is a library to do this?  As is
     *      immediately apparent from the below code, I'm presently not doing
     *      any special handling for files in which none of the strings contain
     *      UTF-8 multi-byte characters.
     */
    while (count != 0) {
        *path++ = (char)(*(*pEntry)++);
        --count;
    }

    *path = '\0';
    return 1;
}

/* copy n bytes out of u into dst and zero-terminate dst
   return 0 on failure */
static int copy_string(unmarshaller* u, int n, char* dst) {
    uint8_t* d = eat_bytes(u, n);
    if (d == NULL) {
        return 0;
    }
    memcpy(dst, d, n);
    dst[n] = 0;
    return 1;
}

/* parse a PMGL entry into a chmUnitInfo struct; return 1 on success. */
static int _chm_parse_pmgl_entry(uint8_t** pEntry, chm_unit_info* ui) {
    int64_t strLen;

    /* parse str len */
    strLen = _chm_parse_cword(pEntry);
    if (strLen > CHM_MAX_PATHLEN)
        return 0;

    /* parse path */
    if (!_chm_parse_UTF8(pEntry, strLen, ui->path))
        return 0;

    /* parse info */
    ui->space = (int)_chm_parse_cword(pEntry);
    ui->start = _chm_parse_cword(pEntry);
    ui->length = _chm_parse_cword(pEntry);
    return 1;
}

static int chm_parse_pmgl_entry(unmarshaller* u, chm_unit_info* ui) {
    int n = (int)get_cword(u);
    if (n > CHM_MAX_PATHLEN || u->err != 0) {
        return 0;
    }

    if (!copy_string(u, n, ui->path)) {
        return 0;
    }

    ui->space = (int)get_cword(u);
    ui->start = get_cword(u);
    ui->length = get_cword(u);

    if (u->err != 0) {
        return 0;
    }
    return 1;
}

/* find an exact entry in PMGL; return NULL if we fail */
static uint8_t* _chm_find_in_PMGL(uint8_t* page_buf, uint32_t block_len, const char* objPath) {
    /* XXX: modify this to do a binary search using the nice index structure
     *      that is provided for us.
     */
    pgml_hdr header;
    // unmarshaller u;
    unsigned int hremain;
    uint8_t* end;
    uint8_t* cur;
    uint8_t* temp;
    int64_t strLen;
    char buffer[CHM_MAX_PATHLEN + 1];

    /* figure out where to start and end */
    cur = page_buf;
    hremain = CHM_PMGL_LEN;

    // unmarshaller_init(&u, page_buf, hremain);
    if (!_unmarshal_pmgl_header(&cur, &hremain, block_len, &header))
        return NULL;
    end = page_buf + block_len - (header.free_space);

    /* now, scan progressively */
    while (cur < end) {
        /* grab the name */
        temp = cur;
        strLen = _chm_parse_cword(&cur);
        if (strLen > CHM_MAX_PATHLEN)
            return NULL;
        if (!_chm_parse_UTF8(&cur, strLen, buffer))
            return NULL;

        /* check if it is the right name */
        if (!strcasecmp(buffer, objPath))
            return temp;

        _chm_skip_PMGL_entry_data(&cur);
    }

    return NULL;
}

/* find which block should be searched next for the entry; -1 if no block */
static int32_t _chm_find_in_PMGI(uint8_t* page_buf, uint32_t block_len, const char* objPath) {
    struct chmPmgiHeader header;
    unsigned int hremain;
    int page = -1;
    uint8_t* end;
    uint8_t* cur;
    int64_t strLen;
    char buffer[CHM_MAX_PATHLEN + 1];

    /* figure out where to start and end */
    cur = page_buf;
    hremain = CHM_PMGI_LEN;
    if (!_unmarshal_pmgi_header(&cur, &hremain, block_len, &header))
        return -1;
    end = page_buf + block_len - header.free_space;

    /* now, scan progressively */
    while (cur < end) {
        /* grab the name */
        strLen = _chm_parse_cword(&cur);
        if (strLen > CHM_MAX_PATHLEN)
            return -1;
        if (!_chm_parse_UTF8(&cur, strLen, buffer))
            return -1;

        /* check if it is the right name */
        if (strcasecmp(buffer, objPath) > 0)
            return page;

        /* load next value for path */
        page = (int)_chm_parse_cword(&cur);
    }

    return page;
}

/* resolve a particular object from the archive */
int chm_resolve_object(chm_file* h, const char* objPath, chm_unit_info* ui) {
    int32_t curPage;

    /* buffer to hold whatever page we're looking at */
    uint8_t* page_buf = malloc(h->itsp.block_len);
    if (page_buf == NULL)
        return CHM_RESOLVE_FAILURE;

    /* starting page */
    curPage = h->itsp.index_root;

    /* until we have either returned or given up */
    while (curPage != -1) {
        /* try to fetch the index page */
        int64_t n = h->itsp.block_len;
        if (read_bytes(h, page_buf, (int64_t)h->dir_offset + (int64_t)curPage * n, n) != n) {
            free(page_buf);
            return CHM_RESOLVE_FAILURE;
        }

        /* now, if it is a leaf node: */
        if (memeq(page_buf, _chm_pmgl_marker, 4)) {
            /* scan block */
            uint8_t* pEntry = _chm_find_in_PMGL(page_buf, h->itsp.block_len, objPath);
            if (pEntry == NULL) {
                free(page_buf);
                return CHM_RESOLVE_FAILURE;
            }

            _chm_parse_pmgl_entry(&pEntry, ui);
            free(page_buf);
            return CHM_RESOLVE_SUCCESS;
        }

        /* else, if it is a branch node: */
        else if (memeq(page_buf, _chm_pmgi_marker, 4))
            curPage = _chm_find_in_PMGI(page_buf, h->itsp.block_len, objPath);

        /* else, we are confused.  give up. */
        else {
            free(page_buf);
            return CHM_RESOLVE_FAILURE;
        }
    }

    /* didn't find anything.  fail. */
    free(page_buf);
    return CHM_RESOLVE_FAILURE;
}

/*
 * utility methods for dealing with compressed data
 */

/* get the bounds of a compressed block.  return 0 on failure */
static int _chm_get_cmpblock_bounds(chm_file* h, int64_t block, int64_t* start, int64_t* len) {
    uint8_t buffer[8], *dummy;
    unsigned int remain;

    /* for all but the last block, use the reset table */
    if (block < h->reset_table.block_count - 1) {
        /* unpack the start address */
        dummy = buffer;
        remain = 8;
        if (read_bytes(h, buffer, (int64_t)h->itsf.data_offset + (int64_t)h->rt_unit.start +
                                      (int64_t)h->reset_table.table_offset + (int64_t)block * 8,
                       remain) != remain ||
            !_unmarshal_uint64(&dummy, &remain, start))
            return 0;

        /* unpack the end address */
        dummy = buffer;
        remain = 8;
        if (read_bytes(h, buffer, (int64_t)h->itsf.data_offset + (int64_t)h->rt_unit.start +
                                      (int64_t)h->reset_table.table_offset + (int64_t)block * 8 + 8,
                       remain) != remain ||
            !_unmarshal_int64(&dummy, &remain, len))
            return 0;
    }

    /* for the last block, use the span in addition to the reset table */
    else {
        /* unpack the start address */
        dummy = buffer;
        remain = 8;
        if (read_bytes(h, buffer, (int64_t)h->itsf.data_offset + (int64_t)h->rt_unit.start +
                                      (int64_t)h->reset_table.table_offset + (int64_t)block * 8,
                       remain) != remain ||
            !_unmarshal_uint64(&dummy, &remain, start))
            return 0;

        *len = h->reset_table.compressed_len;
    }

    /* compute the length and absolute start address */
    *len -= *start;
    *start += h->itsf.data_offset + h->cn_unit.start;

    return 1;
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
    if (!_chm_get_cmpblock_bounds(h, nBlock, &cmpStart, &cmpLen)) {
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

static int64_t _chm_decompress_block(chm_file* h, int64_t nBlock, uint8_t** ubuffer) {
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
static int64_t _chm_decompress_region(chm_file* h, uint8_t* buf, int64_t start, int64_t len) {
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

    int64_t gotLen = _chm_decompress_block(h, nBlock, &ubuffer);
    /* SumatraPDF: check return value */
    if (gotLen == (int64_t)-1) {
        return 0;
    }
    if (gotLen < nLen)
        nLen = gotLen;
    memcpy(buf, ubuffer + nOffset, (unsigned int)nLen);
    return nLen;
}



int64_t chm_retrieve_entry(chm_file* h, chm_entry *e, unsigned char* buf, int64_t addr,
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
        swath = _chm_decompress_region(h, buf, e->start + addr, len);

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

/* retrieve (part of) an object */
int64_t chm_retrieve_object(chm_file* h, chm_unit_info* ui, unsigned char* buf, int64_t addr,
                            int64_t len) {
    if (h == NULL)
        return (int64_t)0;

    /* starting address must be in correct range */
    if (addr >= ui->length)
        return (int64_t)0;

    /* clip length */
    if (addr + len > ui->length)
        len = ui->length - addr;

    if (ui->space == CHM_UNCOMPRESSED) {
        return read_bytes(h, buf, (int64_t)h->itsf.data_offset + (int64_t)ui->start + (int64_t)addr,
                          len);
    }
    if (ui->space != CHM_COMPRESSED) {
        return 0;
    }

    int64_t swath = 0, total = 0;

    /* if compression is not enabled for this file... */
    if (!h->compression_enabled)
        return total;

    do {
        swath = _chm_decompress_region(h, buf, ui->start + addr, len);

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

/* enumerate the objects in the .chm archive */
int chm_enumerate(chm_file* h, int what, CHM_ENUMERATOR e, void* context) {
    pgml_hdr pgml;

    uint8_t* buf = malloc((size_t)h->itsp.block_len);
    if (buf == NULL)
        return 0;

    chm_unit_info ui;
    int type_bits = (what & 0x7);
    int filter_bits = (what & 0xF8);

    int32_t curPage = h->itsp.index_head;

    while (curPage != -1) {
        int64_t n = h->itsp.block_len;
        if (read_bytes(h, buf, (int64_t)h->dir_offset + (int64_t)curPage * n, n) != n) {
            free(buf);
            return 0;
        }

        unmarshaller u;
        unmarshaller_init(&u, buf, n);

        if (!unmarshal_pmgl_header(&u, h->itsp.block_len, &pgml)) {
            free(buf);
            return 0;
        }
        u.bytesLeft -= pgml.free_space;

        /* decode all entries in this page */
        while (u.bytesLeft > 0) {
            if (!chm_parse_pmgl_entry(&u, &ui)) {
                free(buf);
                return 0;
            }

            ui.flags = flags_from_path(ui.path);

            if (!(type_bits & ui.flags))
                continue;

            if (filter_bits && !(filter_bits & ui.flags))
                continue;

            int status = (*e)(h, &ui, context);
            switch (status) {
                case CHM_ENUMERATOR_FAILURE:
                    free(buf);
                    return 0;
                case CHM_ENUMERATOR_CONTINUE:
                    break;
                case CHM_ENUMERATOR_SUCCESS:
                    free(buf);
                    return 1;
                default:
                    break;
            }
        }

        curPage = pgml.block_next;
    }

    free(buf);
    return 1;
}

static chm_entry* entry_from_ui(chm_unit_info* ui) {
    size_t pathLen = strlen(ui->path);
    size_t n = sizeof(chm_entry) + pathLen + 1;
    chm_entry* res = (chm_entry*)calloc(1, n);
    if (res == NULL) {
        return NULL;
    }
    res->start = ui->start;
    res->length = ui->length;
    res->space = ui->space;
    res->flags = ui->flags;
    res->path = (char*)res + sizeof(chm_entry);
    memcpy(res->path, ui->path, pathLen + 1);
    return res;
}

chm_entry **chm_parse(chm_file* h, int* n_entries_out) {
    pgml_hdr pgml;
    chm_unit_info ui;

    if (h->entries_cached != NULL) {
      n_entries_out = h->n_entries_cached;
      return h->entries_cached;
    }
    chm_entry **res = NULL;
    uint8_t* buf = malloc((size_t)h->itsp.block_len);
    if (buf == NULL)
        return 0;

    int nEntries = 0;
    chm_entry* e;
    chm_entry* last_entry = NULL;

    int32_t curPage = h->itsp.index_head;

    while (curPage != -1) {
        int64_t n = h->itsp.block_len;
        if (read_bytes(h, buf, (int64_t)h->dir_offset + (int64_t)curPage * n, n) != n) {
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
            if (!chm_parse_pmgl_entry(&u, &ui)) {
                goto Error;
            }
            ui.flags = flags_from_path(ui.path);
            e = entry_from_ui(&ui);
            if (e == NULL) {
                goto Error;
            }
            e->next = last_entry;
            last_entry = e;
            nEntries++;
        }
        curPage = pgml.block_next;
    }
    if (0 == nEntries) {
      goto Error;
    }
    *n_entries_out = nEntries;
    res = (chm_entry**)calloc(nEntries, sizeof(chm_entry*));
    if (res == NULL) {
      goto Error;
    }
    e = last_entry;
    int n = nEntries - 1;
    while (e != NULL) {
      res[n] = e;
      --n;
      e = e->next;
    }
    free(buf);
    return res;
Error:
    free_entries(last_entry);
    free(res);
    free(buf);
    return NULL;
}

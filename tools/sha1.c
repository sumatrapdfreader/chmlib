/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 *
 * Tom St Denis, tomstdenis@gmail.com, http://libtom.org
 */

/* extracted from https://github.com/libtom/libtomcrypt,
   modified to be stand-alone and clang-formatted */

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

#include "sha1.h"

/* from tomcrypt_macros.h */
#define ROL(x, y)                                                    \
    ((((ulong32)(x) << (ulong32)((y)&31)) |                          \
      (((ulong32)(x) & 0xFFFFFFFFUL) >> (ulong32)(32 - ((y)&31)))) & \
     0xFFFFFFFFUL)
#define ROLc(x, y)                                                   \
    ((((ulong32)(x) << (ulong32)((y)&31)) |                          \
      (((ulong32)(x) & 0xFFFFFFFFUL) >> (ulong32)(32 - ((y)&31)))) & \
     0xFFFFFFFFUL)

#define LOAD32H(x, y)                                                           \
    do {                                                                        \
        x = ((ulong32)((y)[0] & 255) << 24) | ((ulong32)((y)[1] & 255) << 16) | \
            ((ulong32)((y)[2] & 255) << 8) | ((ulong32)((y)[3] & 255));         \
    } while (0)

#define STORE32H(x, y)                               \
    do {                                             \
        (y)[0] = (unsigned char)(((x) >> 24) & 255); \
        (y)[1] = (unsigned char)(((x) >> 16) & 255); \
        (y)[2] = (unsigned char)(((x) >> 8) & 255);  \
        (y)[3] = (unsigned char)((x)&255);           \
    } while (0)

#define STORE64H(x, y)                               \
    do {                                             \
        (y)[0] = (unsigned char)(((x) >> 56) & 255); \
        (y)[1] = (unsigned char)(((x) >> 48) & 255); \
        (y)[2] = (unsigned char)(((x) >> 40) & 255); \
        (y)[3] = (unsigned char)(((x) >> 32) & 255); \
        (y)[4] = (unsigned char)(((x) >> 24) & 255); \
        (y)[5] = (unsigned char)(((x) >> 16) & 255); \
        (y)[6] = (unsigned char)(((x) >> 8) & 255);  \
        (y)[7] = (unsigned char)((x)&255);           \
    } while (0)

#define F0(x, y, z) (z ^ (x & (y ^ z)))
#define F1(x, y, z) (x ^ y ^ z)
#define F2(x, y, z) ((x & y) | (z & (x | y)))
#define F3(x, y, z) (x ^ y ^ z)

static unsigned long ulmin(unsigned long n1, unsigned long n2) {
    if (n1 < n2) {
        return n1;
    }
    return n2;
}

static int sha1_compress(sha1_state* state, const unsigned char* buf) {
    ulong32 a, b, c, d, e, W[80], i;
    ulong32 t;

    /* copy the state into 512-bits into W[0..15] */
    for (i = 0; i < 16; i++) {
        LOAD32H(W[i], buf + (4 * i));
    }

    /* copy state */
    a = state->state[0];
    b = state->state[1];
    c = state->state[2];
    d = state->state[3];
    e = state->state[4];

    /* expand it */
    for (i = 16; i < 80; i++) {
        W[i] = ROL(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);
    }

/* compress */
/* round one */
#define FF0(a, b, c, d, e, i)                                 \
    e = (ROLc(a, 5) + F0(b, c, d) + e + W[i] + 0x5a827999UL); \
    b = ROLc(b, 30);
#define FF1(a, b, c, d, e, i)                                 \
    e = (ROLc(a, 5) + F1(b, c, d) + e + W[i] + 0x6ed9eba1UL); \
    b = ROLc(b, 30);
#define FF2(a, b, c, d, e, i)                                 \
    e = (ROLc(a, 5) + F2(b, c, d) + e + W[i] + 0x8f1bbcdcUL); \
    b = ROLc(b, 30);
#define FF3(a, b, c, d, e, i)                                 \
    e = (ROLc(a, 5) + F3(b, c, d) + e + W[i] + 0xca62c1d6UL); \
    b = ROLc(b, 30);

    for (i = 0; i < 20;) {
        FF0(a, b, c, d, e, i++);
        t = e;
        e = d;
        d = c;
        c = b;
        b = a;
        a = t;
    }

    for (; i < 40;) {
        FF1(a, b, c, d, e, i++);
        t = e;
        e = d;
        d = c;
        c = b;
        b = a;
        a = t;
    }

    for (; i < 60;) {
        FF2(a, b, c, d, e, i++);
        t = e;
        e = d;
        d = c;
        c = b;
        b = a;
        a = t;
    }

    for (; i < 80;) {
        FF3(a, b, c, d, e, i++);
        t = e;
        e = d;
        d = c;
        c = b;
        b = a;
        a = t;
    }

#undef FF0
#undef FF1
#undef FF2
#undef FF3

    /* store */
    state->state[0] = state->state[0] + a;
    state->state[1] = state->state[1] + b;
    state->state[2] = state->state[2] + c;
    state->state[3] = state->state[3] + d;
    state->state[4] = state->state[4] + e;

    return CRYPT_OK;
}

/**
   Initialize the hash state
   @param state   The hash state you wish to initialize
*/
void sha1_init(sha1_state* state) {
    state->state[0] = 0x67452301UL;
    state->state[1] = 0xefcdab89UL;
    state->state[2] = 0x98badcfeUL;
    state->state[3] = 0x10325476UL;
    state->state[4] = 0xc3d2e1f0UL;
    state->curlen = 0;
    state->length = 0;
}

/**
   Process a block of memory though the hash
   @param state   The hash state
   @param in     The data to hash
   @param inlen  The length of the data (octets)
   @return CRYPT_OK if successful
*/
int sha1_process(sha1_state* state, const unsigned char* in, unsigned long inlen) {
    unsigned long n;
    int err;
    if (state->curlen > sizeof(state->buf)) {
        return CRYPT_INVALID_ARG;
    }
    if ((state->length + inlen) < state->length) {
        return CRYPT_HASH_OVERFLOW;
    }
    while (inlen > 0) {
        if (state->curlen == 0 && inlen >= 64) {
            if ((err = sha1_compress(state, in)) != CRYPT_OK) {
                return err;
            }
            state->length += 64 * 8;
            in += 64;
            inlen -= 64;
        } else {
            n = ulmin(inlen, (64 - state->curlen));
            memcpy(state->buf + state->curlen, in, (size_t)n);
            state->curlen += n;
            in += n;
            inlen -= n;
            if (state->curlen == 64) {
                if ((err = sha1_compress(state, state->buf)) != CRYPT_OK) {
                    return err;
                }
                state->length += 8 * 64;
                state->curlen = 0;
            }
        }
    }
    return CRYPT_OK;
}

/**
   Terminate the hash to get the digest
   @param state The hash state
   @param out [out] The destination of the hash (20 bytes)
   @return CRYPT_OK if successful
*/
int sha1_done(sha1_state* state, unsigned char* out) {
    int i;

    if (state->curlen >= sizeof(state->buf)) {
        return CRYPT_INVALID_ARG;
    }

    /* increase the length of the message */
    state->length += state->curlen * 8;

    /* append the '1' bit */
    state->buf[state->curlen++] = (unsigned char)0x80;

    /* if the length is currently above 56 bytes we append zeros
     * then compress.  Then we can fall back to padding zeros and length
     * encoding like normal.
     */
    if (state->curlen > 56) {
        while (state->curlen < 64) {
            state->buf[state->curlen++] = (unsigned char)0;
        }
        sha1_compress(state, state->buf);
        state->curlen = 0;
    }

    /* pad upto 56 bytes of zeroes */
    while (state->curlen < 56) {
        state->buf[state->curlen++] = (unsigned char)0;
    }

    /* store length */
    STORE64H(state->length, state->buf + 56);
    sha1_compress(state, state->buf);

    /* copy output */
    for (i = 0; i < 5; i++) {
        STORE32H(state->state[i], out + (4 * i));
    }
    return CRYPT_OK;
}

int sha1_process_all(const unsigned char* in, unsigned long inlen, unsigned char* hash) {
    sha1_state state;
    sha1_init(&state);
    int err = sha1_process(&state, in, inlen);
    if (err != CRYPT_OK) {
        return err;
    }
    return sha1_done(&state, hash);
}

/**
  Self-test the hash
  @return CRYPT_OK if successful, CRYPT_NOP if self-tests have been disabled
*/
int sha1_test(void) {
    static const struct {
        char* msg;
        unsigned char hash[20];
    } tests[] = {{"abc",
                  {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78,
                   0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d}},
                 {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
                  {0x84, 0x98, 0x3E, 0x44, 0x1C, 0x3B, 0xD2, 0x6E, 0xBA, 0xAE, 0x4A, 0xA1, 0xF9,
                   0x51, 0x29, 0xE5, 0xE5, 0x46, 0x70, 0xF1}}};

    int i;
    unsigned char tmp[20];
    sha1_state state;

    for (i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
        sha1_init(&state);
        sha1_process(&state, (unsigned char*)tests[i].msg, (unsigned long)strlen(tests[i].msg));
        sha1_done(&state, tmp);
        if (memcpy(tmp, tests[i].hash, 20) != 0) {
            return CRYPT_FAIL_TESTVECTOR;
        }
    }
    return CRYPT_OK;
}

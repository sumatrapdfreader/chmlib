#ifndef SHA1_H__
#define SHA1_H__

#include <stdint.h>

/* TODO: switch ulong64 and ulong32 => uint64_t, uint32_t */
typedef uint64_t ulong64;
typedef uint32_t ulong32;

enum {
   CRYPT_OK=0,             /* Result OK */
   CRYPT_INVALID_ARG,      /* Generic invalid argument */
   CRYPT_HASH_OVERFLOW,     /* Hash applied to too many bits */
   CRYPT_FAIL_TESTVECTOR   /* Algorithm failed test vectors */
};

typedef struct sha1_state {
    ulong64 length;
    ulong32 state[5], curlen;
    unsigned char buf[64];
} sha1_state;

void sha1_init(sha1_state* state);
int sha1_process(sha1_state* state, const unsigned char *in, unsigned long inlen);
int sha1_done(sha1_state *state, unsigned char *out);
int sha1_test(void);

int sha1_process_all(const unsigned char *in, unsigned long inlen, unsigned char *hash);

#endif

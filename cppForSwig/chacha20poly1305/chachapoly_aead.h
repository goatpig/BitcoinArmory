#ifndef CHACHA20_POLY_AEAD_H
#define CHACHA20_POLY_AEAD_H

#include "chacha.h"

#define CHACHA_KEYLEN 32 /* 2 x 256 bit keys */

struct chachapolyaead_ctx {
  struct chacha_ctx main_ctx, header_ctx;
};

int chacha20poly1305_init(struct chachapolyaead_ctx *cpctx, const uint8_t *key,
                          int keylen);
int chacha20poly1305_crypt(struct chachapolyaead_ctx *ctx, uint32_t seqnr,
                           uint8_t *dest, const uint8_t *src, uint32_t len,
                           uint32_t aadlen, int is_encrypt);
int chacha20poly1305_get_length(struct chachapolyaead_ctx *ctx,
                                uint32_t *len_out, uint32_t seqnr,
                                const uint8_t *ciphertext, uint32_t len);
#endif /* CHACHA20_POLY_AEAD_H */
#include "chachapoly_aead.h"

#define __STDC_WANT_LIB_EXT1__ 1
#include "poly1305.h"
#include <string.h>

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) &&                 \
    !defined(__WINDOWS__)
#define __WINDOWS__
#endif

#if defined(__linux__) || defined(__CYGWIN__)
#include <endian.h>

#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32(x) OSSwapHostToLittleInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined(__OpenBSD__)
#include <sys/endian.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/endian.h>
#define le32toh(x) letoh32(x)
#define le64toh(x) letoh64(x)

#elif defined(__WINDOWS__)
#include <sys/param.h>
#include <winsock2.h>

#if BYTE_ORDER == LITTLE_ENDIAN
#define htole32(x) (x)
#define le32toh(x) (x)

#define htole64(x) (x)
#define le64toh(x) (x)

#elif BYTE_ORDER == BIG_ENDIAN
#define htole32(x) __builtin_bswap32(x)
#define le32toh(x) __builtin_bswap32(x)

#define htole64(x) __builtin_bswap64(x)
#define le64toh(x) __builtin_bswap64(x)

#else
#error byte order not supported
#endif /* endif byteorder */
#else

#error platform not supported

#endif /* endif platform */

#ifndef HAVE_TIMINGSAFE_BCMP

int timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
  const unsigned char *p1 = b1, *p2 = b2;
  int ret = 0;

  for (; n > 0; n--)
    ret |= *p1++ ^ *p2++;
  return (ret != 0);
}

#endif /* TIMINGSAFE_BCMP */

#ifndef HAVE_MEMSET_S
void memory_cleanse(void *p, size_t n) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  memset(p, 0, n);
#endif
#endif
}

#else /* no memset_s available */
void memory_cleanse(void *p, size_t n) { (void)memset_s(p, n, 0, n); }
#endif

int chacha20poly1305_init(struct chachapolyaead_ctx *ctx, const uint8_t *key,
                          int keylen) {
  if (keylen != (32 + 32)) /* 2 x 256 bit keys */
    return -1;
  chacha_keysetup(&ctx->main_ctx, key, 256);
  chacha_keysetup(&ctx->header_ctx, key + 32, 256);
  return 0;
}

int chacha20poly1305_crypt(struct chachapolyaead_ctx *ctx, uint32_t seqnr,
                           uint8_t *dest, const uint8_t *src, uint32_t len,
                           uint32_t aadlen, int is_encrypt) {
  uint8_t seqbuf[8];
  const uint8_t one[8] = {1, 0, 0, 0, 0, 0, 0, 0}; /* NB little-endian */
  uint8_t expected_tag[POLY1305_TAGLEN], poly_key[POLY1305_KEYLEN];
  int r = -1;

  uint64_t chacha_iv = htole64(seqnr);
  memset(poly_key, 0, sizeof(poly_key));
  chacha_ivsetup(&ctx->main_ctx, (uint8_t *)&chacha_iv, NULL);
  chacha_encrypt_bytes(&ctx->main_ctx, poly_key, poly_key, sizeof(poly_key));

  if (!is_encrypt) {
    const uint8_t *tag = src + aadlen + len;

    poly1305_auth(expected_tag, src, aadlen + len, poly_key);
    if (timingsafe_bcmp(expected_tag, tag, POLY1305_TAGLEN) != 0) {
      r = -1;
      goto out;
    }
  }

  if (aadlen) {
    chacha_ivsetup(&ctx->header_ctx, (uint8_t *)&chacha_iv, NULL);
    chacha_encrypt_bytes(&ctx->header_ctx, src, dest, aadlen);
  }

  /* Set Chacha's block counter to 1 */
  chacha_ivsetup(&ctx->main_ctx, (uint8_t *)&chacha_iv, one);
  chacha_encrypt_bytes(&ctx->main_ctx, src + aadlen, dest + aadlen, len);

  /* If encrypting, calculate and append tag */
  if (is_encrypt) {
    poly1305_auth(dest + aadlen + len, dest, aadlen + len, poly_key);
  }
  r = 0;
out:
  memory_cleanse(expected_tag, sizeof(expected_tag));
  memory_cleanse(seqbuf, sizeof(seqbuf));
  memory_cleanse(&chacha_iv, sizeof(chacha_iv));
  memory_cleanse(poly_key, sizeof(poly_key));
  return r;
}

int chacha20poly1305_get_length(struct chachapolyaead_ctx *ctx,
                                uint32_t *len_out, uint32_t seqnr,
                                const uint8_t *ciphertext, uint32_t len) {
  uint8_t buf[4], seqbuf[8];

  if (len < 4)
    return -1;
  uint64_t seqnr64 = seqnr;
  seqnr64 = htole64(seqnr64);
  chacha_ivsetup(&ctx->header_ctx, (uint8_t *)&seqnr64, NULL);
  chacha_encrypt_bytes(&ctx->header_ctx, ciphertext, buf, 4);
  *len_out = le32toh(buf[0]);
  return 0;
}
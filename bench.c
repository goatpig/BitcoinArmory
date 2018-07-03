#include "sys/time.h"
#include <math.h>
#include <stdio.h>

#include "chachapoly_aead.h"
#include "poly1305.h"

static const uint8_t testkey[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

static const uint8_t testnonce[32] = {0x00, 0x01, 0x02, 0x03,
                                      0x04, 0x05, 0x06, 0x07};

static const uint8_t testdata[12] = {0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
                                     0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21};

static const uint64_t BUFFER_SIZE = 1000 * 1000;

static const uint8_t aead_keys[64] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static double gettimedouble(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_usec * 0.000001 + tv.tv_sec;
}

static void print_number(double x) {
  double y = x;
  int c = 0;
  if (y < 0.0) {
    y = -y;
  }
  while (y < 100.0) {
    y *= 10.0;
    c++;
  }
  printf("%.*f", c, x);
}

static void run_benchmark(char *name, void (*benchmark)(void *),
                          void (*setup)(void *), void (*teardown)(void *),
                          void *data, int count, int iter) {
  int i;
  double min = HUGE_VAL;
  double sum = 0.0;
  double max = 0.0;
  for (i = 0; i < count; i++) {
    double begin, total;
    if (setup != NULL) {
      setup(data);
    }
    begin = gettimedouble();
    benchmark(data);
    total = gettimedouble() - begin;
    if (teardown != NULL) {
      teardown(data);
    }
    if (total < min) {
      min = total;
    }
    if (total > max) {
      max = total;
    }
    sum += total;
  }
  printf("%s: min ", name);
  print_number(min * 1000000000.0 / iter);
  printf("ns / avg ");
  print_number((sum / count) * 1000000000.0 / iter);
  printf("ns / max ");
  print_number(max * 1000000000.0 / iter);
  printf("ns\n");
}

static void bench_chacha_ivsetup(void *data) {
  struct chacha_ctx *ctx = (struct chacha_ctx *)data;
  int i;
  for (i = 0; i < 50000; i++) {
    chacha_ivsetup(ctx, testnonce, NULL);
  }
}

static void bench_chacha_keysetup(void *data) {
  struct chacha_ctx *ctx = (struct chacha_ctx *)data;
  int i;
  for (i = 0; i < 50000; i++) {
    chacha_keysetup(ctx, testkey, 256);
  }
}

static void bench_chacha_encrypt(void *data) {
  struct chacha_ctx *ctx = (struct chacha_ctx *)data;
  uint8_t scratch[16] = {0};
  int i;
  for (i = 0; i < 4000000 / 16; i++) {
    chacha_encrypt_bytes(ctx, scratch, scratch, 16);
  }
}

static void bench_poly1305_auth(void *data) {
  struct chacha_ctx *ctx = (struct chacha_ctx *)data;
  uint8_t poly1305_tag[16] = {0};
  int i;
  for (i = 0; i < 4000000 / 12; i++) {
    poly1305_auth(poly1305_tag, testdata, 12, testkey);
  }
}

static void bench_chacha20poly1305_init(void *data) {
  struct chachapolyaead_ctx *ctx = (struct chachapolyaead_ctx *)data;
  int i;
  for (i = 0; i < 50000; i++) {
    chacha20poly1305_init(ctx, aead_keys, 64);
  }
}

static void bench_chacha20poly1305_crypt(void *data) {
  struct chachapolyaead_ctx *ctx = (struct chachapolyaead_ctx *)data;
  int i;
  uint32_t seqnr = 0;

  uint8_t buffer[BUFFER_SIZE + 16];
  for (i = 0; i < 30; i++) {
    chacha20poly1305_crypt(ctx, seqnr, buffer, buffer, BUFFER_SIZE - 4, 4, 1);
  }
}

int main(void) {
  struct chacha_ctx ctx_chacha;
  struct chachapolyaead_ctx aead_ctx;
  run_benchmark("chacha_ivsetup", bench_chacha_ivsetup, NULL, NULL, &ctx_chacha,
                20, 50000);
  run_benchmark("chacha_keysetup", bench_chacha_keysetup, NULL, NULL,
                &ctx_chacha, 20, 50000);
  run_benchmark("chacha_encrypt", bench_chacha_encrypt, NULL, NULL, &ctx_chacha,
                20, 4000000);
  run_benchmark("poly1305_auth", bench_poly1305_auth, NULL, NULL, &ctx_chacha,
                20, 4000000);
  run_benchmark("chacha20poly1305_init", bench_chacha20poly1305_init, NULL,
                NULL, &aead_ctx, 20, 4000000);
  run_benchmark("chacha20poly1305_crypt 1MB", bench_chacha20poly1305_crypt,
                NULL, NULL, &aead_ctx, 20, 30);
  return 0;
}
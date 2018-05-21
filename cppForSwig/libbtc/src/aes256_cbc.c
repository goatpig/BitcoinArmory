
#include <btc/aes256_cbc.h>
#include <btc/ctaes.h>

#include <string.h>

int aes256_cbc_encrypt(const unsigned char aes_key[32], const unsigned char iv[AES_BLOCK_SIZE], const unsigned char* data, int size, int pad, unsigned char* out)
{
    int written = 0;
    int padsize = size % AES_BLOCK_SIZE;
    unsigned char mixed[AES_BLOCK_SIZE];

    if (!data || !size || !out)
        return 0;

    if (!pad && padsize != 0)
        return 0;

    AES256_ctx aes_ctx;
    // Set cipher key
    AES256_init(&aes_ctx, aes_key);

    memcpy(mixed, iv, AES_BLOCK_SIZE);

    // Write all but the last block
    while (written + AES_BLOCK_SIZE <= size) {
        for (int i = 0; i != AES_BLOCK_SIZE; i++)
            mixed[i] ^= *data++;
        AES256_encrypt(&aes_ctx, 1, out + written, mixed);
        memcpy(mixed, out + written, AES_BLOCK_SIZE);
        written += AES_BLOCK_SIZE;
    }
    if (pad) {
        // For all that remains, pad each byte with the value of the remaining
        // space. If there is none, pad by a full block.
        for (int i = 0; i != padsize; i++)
            mixed[i] ^= *data++;
        for (int i = padsize; i != AES_BLOCK_SIZE; i++)
            mixed[i] ^= AES_BLOCK_SIZE - padsize;
        AES256_encrypt(&aes_ctx, 1, out + written, mixed);
        written += AES_BLOCK_SIZE;
    }
    return written;
}

int aes256_cbc_decrypt(const unsigned char aes_key[32], const unsigned char iv[AES_BLOCK_SIZE], const unsigned char* data, int size, int pad, unsigned char* out)
{
    unsigned char padsize = 0;
    int written = 0;
    int fail = 0;
    const unsigned char* prev = iv;

    if (!data || !size || !out)
        return 0;

    if (size % AES_BLOCK_SIZE != 0)
        return 0;

    AES256_ctx aes_ctx;
    // Set cipher key
    AES256_init(&aes_ctx, aes_key);

    // Decrypt all data. Padding will be checked in the output.
    while (written != size) {
        AES256_decrypt(&aes_ctx, 1, out, data + written);
        for (int i = 0; i != AES_BLOCK_SIZE; i++)
            *out++ ^= prev[i];
        prev = data + written;
        written += AES_BLOCK_SIZE;
    }

    // When decrypting padding, attempt to run in constant-time
    if (pad) {
        // If used, padding size is the value of the last decrypted byte. For
        // it to be valid, It must be between 1 and AES_BLOCK_SIZE.
        padsize = *--out;
        fail = !padsize | (padsize > AES_BLOCK_SIZE);

        // If not well-formed, treat it as though there's no padding.
        padsize *= !fail;

        // All padding must equal the last byte otherwise it's not well-formed
        for (int i = AES_BLOCK_SIZE; i != 0; i--)
            fail |= ((i > AES_BLOCK_SIZE - padsize) & (*out-- != padsize));

        written -= padsize;
    }
    return written * !fail;
}

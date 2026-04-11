#include "crypto/nj_crypto.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <cstring>
#include <string>

// Internal implementation shared by both random-IV and explicit-IV variants
static EncryptedPayload encrypt_payload_impl(const unsigned char* plaintext, int plaintext_len,
                                              const unsigned char key[32],
                                              const unsigned char iv[16])
{
    EncryptedPayload result;

    // Guard against oversized allocations (DoS prevention)
    if (plaintext_len > NJ_CRYPTO_MAX_PLAINTEXT || plaintext_len < 0) return result;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return result;

    // Output buffer: IV (16) + ciphertext (plaintext_len + up to 16 padding)
    result.data.resize(NJ_CRYPTO_IV_LEN + plaintext_len + EVP_MAX_BLOCK_LENGTH);
    memcpy(result.data.data(), iv, NJ_CRYPTO_IV_LEN);  // prepend IV per D-08

    int out1 = 0, out2 = 0;
    bool success = true;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) != 1) {
        success = false;
    }

    // For zero-length plaintext: skip Update, just call Final to get padding block
    if (success && plaintext_len > 0) {
        if (EVP_EncryptUpdate(ctx, result.data.data() + NJ_CRYPTO_IV_LEN, &out1,
                              plaintext, plaintext_len) != 1) {
            success = false;
        }
    }

    if (success) {
        if (EVP_EncryptFinal_ex(ctx, result.data.data() + NJ_CRYPTO_IV_LEN + out1, &out2) != 1) {
            success = false;
        }
    }

    EVP_CIPHER_CTX_free(ctx);

    if (!success) {
        result.data.clear();
        return result;
    }

    result.data.resize(NJ_CRYPTO_IV_LEN + out1 + out2);
    result.ok = true;
    return result;
}

EncryptedPayload encrypt_payload(const unsigned char* plaintext, int plaintext_len,
                                  const unsigned char key[32])
{
    unsigned char iv[NJ_CRYPTO_IV_LEN];
    if (RAND_bytes(iv, NJ_CRYPTO_IV_LEN) != 1) {
        EncryptedPayload result;
        return result;
    }
    return encrypt_payload_impl(plaintext, plaintext_len, key, iv);
}

EncryptedPayload encrypt_payload_with_iv(const unsigned char* plaintext, int plaintext_len,
                                          const unsigned char key[32],
                                          const unsigned char iv[16])
{
    return encrypt_payload_impl(plaintext, plaintext_len, key, iv);
}

DecryptedPayload decrypt_payload(const unsigned char* encrypted, int encrypted_len,
                                  const unsigned char key[32])
{
    DecryptedPayload result;

    // Need at least IV (16) + one AES block (16) = 32 bytes minimum
    if (encrypted_len < NJ_CRYPTO_IV_LEN + NJ_CRYPTO_BLOCK_LEN) return result;

    // Ciphertext after IV must be block-aligned (multiple of 16)
    int ciphertext_len = encrypted_len - NJ_CRYPTO_IV_LEN;
    if (ciphertext_len % NJ_CRYPTO_BLOCK_LEN != 0) return result;

    const unsigned char* iv = encrypted;
    const unsigned char* ciphertext = encrypted + NJ_CRYPTO_IV_LEN;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return result;

    result.data.resize(ciphertext_len + EVP_MAX_BLOCK_LENGTH);
    int out1 = 0, out2 = 0;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv) != 1 ||
        EVP_DecryptUpdate(ctx, result.data.data(), &out1, ciphertext, ciphertext_len) != 1 ||
        EVP_DecryptFinal_ex(ctx, result.data.data() + out1, &out2) != 1)
    {
        EVP_CIPHER_CTX_free(ctx);
        result.data.clear();  // NO partial plaintext on failure (review concern #9)
        return result;
    }
    EVP_CIPHER_CTX_free(ctx);
    result.data.resize(out1 + out2);
    result.ok = true;
    return result;
}

void derive_encryption_key(const char* password, const unsigned char challenge[8],
                            unsigned char key_out[32])
{
    // Per D-03: SHA-256(password + challenge)
    // Challenge serves as salt (from MESSAGE_SERVER_AUTH_CHALLENGE)
    std::string phrase(password);
    phrase.append(reinterpret_cast<const char*>(challenge), 8);

    SHA256(reinterpret_cast<const unsigned char*>(phrase.data()),
           phrase.size(), key_out);
}

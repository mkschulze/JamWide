#include "crypto/nj_crypto.h"
#include <cstring>
#include <string>

// ── Platform crypto backends ──
// Windows: BCrypt (built into Windows, no external dependency)
// macOS/Linux: OpenSSL

#if defined(_WIN32)
  #include <windows.h>
  #include <bcrypt.h>
  #ifndef NT_SUCCESS
    #define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
  #endif
#else
  #include <openssl/evp.h>
  #include <openssl/sha.h>
  #include <openssl/rand.h>
#endif

// Internal implementation shared by both random-IV and explicit-IV variants
static EncryptedPayload encrypt_payload_impl(const unsigned char* plaintext, int plaintext_len,
                                              const unsigned char key[32],
                                              const unsigned char iv[16])
{
    EncryptedPayload result;

    // Guard against oversized allocations (DoS prevention)
    if (plaintext_len > NJ_CRYPTO_MAX_PLAINTEXT || plaintext_len < 0) return result;

#if defined(_WIN32)
    // Output buffer: IV(16) + ciphertext (plaintext + up to 16-byte PKCS#7 padding)
    int max_ct = plaintext_len + NJ_CRYPTO_BLOCK_LEN;
    result.data.resize(NJ_CRYPTO_IV_LEN + max_ct);
    memcpy(result.data.data(), iv, NJ_CRYPTO_IV_LEN);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
        result.data.clear(); return result;
    }
    if (!NT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                       (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                                       sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        result.data.clear(); return result;
    }
    if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                                (PUCHAR)key, 32, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        result.data.clear(); return result;
    }

    // BCrypt modifies IV in-place during operation
    unsigned char iv_copy[NJ_CRYPTO_IV_LEN];
    memcpy(iv_copy, iv, NJ_CRYPTO_IV_LEN);

    ULONG out_len = 0;
    unsigned char dummy = 0;
    NTSTATUS status = BCryptEncrypt(
        hKey,
        plaintext_len > 0 ? (PUCHAR)plaintext : &dummy,
        (ULONG)plaintext_len,
        nullptr,
        iv_copy, NJ_CRYPTO_IV_LEN,
        result.data.data() + NJ_CRYPTO_IV_LEN,
        (ULONG)max_ct,
        &out_len,
        BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!NT_SUCCESS(status)) {
        result.data.clear(); return result;
    }
    result.data.resize(NJ_CRYPTO_IV_LEN + out_len);
    result.ok = true;

#else  // macOS / Linux: OpenSSL
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
#endif

    return result;
}

EncryptedPayload encrypt_payload(const unsigned char* plaintext, int plaintext_len,
                                  const unsigned char key[32])
{
#if defined(_WIN32)
    unsigned char iv[NJ_CRYPTO_IV_LEN];
    if (!NT_SUCCESS(BCryptGenRandom(nullptr, iv, NJ_CRYPTO_IV_LEN,
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        EncryptedPayload result;
        return result;
    }
#else
    unsigned char iv[NJ_CRYPTO_IV_LEN];
    if (RAND_bytes(iv, NJ_CRYPTO_IV_LEN) != 1) {
        EncryptedPayload result;
        return result;
    }
#endif
    return encrypt_payload_impl(plaintext, plaintext_len, key, iv);
}

#ifdef JAMWIDE_BUILD_TESTS
EncryptedPayload encrypt_payload_with_iv(const unsigned char* plaintext, int plaintext_len,
                                          const unsigned char key[32],
                                          const unsigned char iv[16])
{
    return encrypt_payload_impl(plaintext, plaintext_len, key, iv);
}
#endif

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

#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;

    if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return result;
    if (!NT_SUCCESS(BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
                                       (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                                       sizeof(BCRYPT_CHAIN_MODE_CBC), 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }
    if (!NT_SUCCESS(BCryptGenerateSymmetricKey(hAlg, &hKey, nullptr, 0,
                                                (PUCHAR)key, 32, 0))) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return result;
    }

    unsigned char iv_copy[NJ_CRYPTO_IV_LEN];
    memcpy(iv_copy, iv, NJ_CRYPTO_IV_LEN);

    result.data.resize(ciphertext_len);
    ULONG out_len = 0;
    NTSTATUS status = BCryptDecrypt(
        hKey,
        (PUCHAR)ciphertext, (ULONG)ciphertext_len,
        nullptr,
        iv_copy, NJ_CRYPTO_IV_LEN,
        result.data.data(),
        (ULONG)ciphertext_len,
        &out_len,
        BCRYPT_BLOCK_PADDING);

    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAlg, 0);

    if (!NT_SUCCESS(status)) {
        result.data.clear();  // NO partial plaintext on failure
        return result;
    }
    result.data.resize(out_len);
    result.ok = true;

#else  // macOS / Linux: OpenSSL
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
#endif

    return result;
}

void derive_encryption_key(const char* password, const unsigned char challenge[8],
                            unsigned char key_out[32])
{
    // Per D-03: SHA-256(password + challenge)
    // Challenge serves as salt (from MESSAGE_SERVER_AUTH_CHALLENGE)
    std::string phrase(password);
    phrase.append(reinterpret_cast<const char*>(challenge), 8);

#if defined(_WIN32)
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;

    if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        if (NT_SUCCESS(BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0))) {
            BCryptHashData(hHash, (PUCHAR)phrase.data(), (ULONG)phrase.size(), 0);
            BCryptFinishHash(hHash, key_out, 32, 0);
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }
#else
    SHA256(reinterpret_cast<const unsigned char*>(phrase.data()),
           phrase.size(), key_out);
#endif
}

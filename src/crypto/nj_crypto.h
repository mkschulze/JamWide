#ifndef NJ_CRYPTO_H
#define NJ_CRYPTO_H

#include <vector>

// AES-256-CBC constants
#define NJ_CRYPTO_IV_LEN  16
#define NJ_CRYPTO_BLOCK_LEN 16
// Maximum encryption overhead: 16-byte IV + up to 16-byte PKCS#7 padding
#define NJ_CRYPTO_OVERHEAD (NJ_CRYPTO_IV_LEN + NJ_CRYPTO_BLOCK_LEN)
// Maximum plaintext size we will encrypt (reject larger to prevent DoS allocation)
#define NJ_CRYPTO_MAX_PLAINTEXT 16384

struct EncryptedPayload {
    std::vector<unsigned char> data;  // [IV:16][ciphertext:N]
    bool ok = false;
};

struct DecryptedPayload {
    std::vector<unsigned char> data;
    bool ok = false;
};

// Encrypt plaintext using AES-256-CBC with random IV.
// Returns NEW buffer: [16-byte IV][ciphertext] in result.data.
// Zero-length plaintext (plaintext_len == 0) IS encrypted: produces 32 bytes
// (16-byte IV + 16-byte PKCS#7 padding of empty block).
// Rejects plaintext_len > NJ_CRYPTO_MAX_PLAINTEXT (returns ok=false).
// On failure: result.ok=false AND result.data is empty (guaranteed).
// Per D-02: Uses OpenSSL EVP_aes_256_cbc().
// Per D-04: Random IV via RAND_bytes per message.
// Per D-08: IV prepended to payload.
EncryptedPayload encrypt_payload(const unsigned char* plaintext, int plaintext_len,
                                  const unsigned char key[32]);

// Test-only variant that accepts an explicit IV instead of generating randomly.
// Used for deterministic known-vector tests. MUST NOT be used in production code.
EncryptedPayload encrypt_payload_with_iv(const unsigned char* plaintext, int plaintext_len,
                                          const unsigned char key[32],
                                          const unsigned char iv[16]);

// Decrypt payload produced by encrypt_payload.
// Input: [16-byte IV][ciphertext]. Minimum valid length is 32 (IV + one AES block).
// Returns NEW buffer: decrypted plaintext in result.data.
// On failure: result.ok=false AND result.data is empty (no partial plaintext EVER).
// Ciphertext after IV must be a multiple of 16 bytes (AES block size).
DecryptedPayload decrypt_payload(const unsigned char* encrypted, int encrypted_len,
                                  const unsigned char key[32]);

// Derive 32-byte AES key from password + 8-byte server challenge.
// Per D-03: SHA-256(password + challenge). No PBKDF2.
void derive_encryption_key(const char* password, const unsigned char challenge[8],
                            unsigned char key_out[32]);

#endif // NJ_CRYPTO_H

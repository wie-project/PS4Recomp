// SPDX-License-Identifier: GPL-2.0-or-later

// P2P payload protection: shadPS4 core/net/p2p_crypto.cpp on CommonCrypto.

#include "ps4_net_p2p_crypto.h"

#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <stdlib.h>
#include <string.h>

#define IV_SEED_SIZE 4
#define SIGNATURE_SIZE 4

// AES-128-CFB128 in place, with the seed repeated over the 16-byte IV.
static bool crypt_in_place(uint8_t *payload, size_t len, const uint8_t key[PS4_P2P_KEY_SIZE],
                           const uint8_t seed[IV_SEED_SIZE], bool encrypt) {
    uint8_t iv[16];
    for (size_t i = 0; i < sizeof(iv); i++) iv[i] = seed[i & 3];
    CCCryptorRef cryptor = NULL;
    if (CCCryptorCreateWithMode(encrypt ? kCCEncrypt : kCCDecrypt, kCCModeCFB, kCCAlgorithmAES, ccNoPadding, iv, key,
                                PS4_P2P_KEY_SIZE, NULL, 0, 0, 0, &cryptor) != kCCSuccess) {
        return false;
    }
    size_t moved = 0;
    size_t final_moved = 0;
    const bool ok = CCCryptorUpdate(cryptor, payload, len, payload, len, &moved) == kCCSuccess &&
                    CCCryptorFinal(cryptor, payload + moved, len - moved, &final_moved) == kCCSuccess;
    CCCryptorRelease(cryptor);
    return ok && moved + final_moved == len;
}

static void sign(const uint8_t *body, size_t len, const uint8_t key[PS4_P2P_KEY_SIZE], uint8_t out[SIGNATURE_SIZE]) {
    uint8_t digest[CC_SHA1_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA1, key, PS4_P2P_KEY_SIZE, body, len, digest);
    memcpy(out, digest, SIGNATURE_SIZE);
}

void ps4_p2p_derive_communication_id(const uint8_t value[16], uint8_t out[4]) {
    uint8_t digest[CC_SHA1_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA1, value, 16, NULL, 0, digest);
    memcpy(out, digest, 4);
}

bool ps4_p2p_protect(const uint8_t *data, size_t len, bool encrypt, bool sign_body,
                     const uint8_t key[PS4_P2P_KEY_SIZE], uint8_t *out, size_t *out_len) {
    const size_t prefix = (encrypt ? IV_SEED_SIZE : 0) + (sign_body ? SIGNATURE_SIZE : 0);
    uint8_t *body = out + prefix;
    memmove(body, data, len);
    if (encrypt) {
        uint8_t seed[IV_SEED_SIZE];
        arc4random_buf(seed, sizeof(seed));
        if (!crypt_in_place(body, len, key, seed, true)) return false;
        memcpy(out, seed, IV_SEED_SIZE);
    }
    if (sign_body) sign(body, len, key, out + (encrypt ? IV_SEED_SIZE : 0)); // signs the ciphertext
    *out_len = prefix + len;
    return true;
}

bool ps4_p2p_unprotect(const uint8_t *data, size_t len, bool encrypt, bool sign_body,
                       const uint8_t key[PS4_P2P_KEY_SIZE], uint8_t *out, size_t *out_len) {
    const size_t prefix = (encrypt ? IV_SEED_SIZE : 0) + (sign_body ? SIGNATURE_SIZE : 0);
    if (len < prefix) return false;
    uint8_t seed[IV_SEED_SIZE] = {0};
    uint8_t signature[SIGNATURE_SIZE] = {0};
    size_t offset = 0;
    if (encrypt) {
        memcpy(seed, data, IV_SEED_SIZE);
        offset += IV_SEED_SIZE;
    }
    if (sign_body) {
        memcpy(signature, data + offset, SIGNATURE_SIZE);
        offset += SIGNATURE_SIZE;
    }
    const size_t body_len = len - offset;
    memmove(out, data + offset, body_len);
    if (sign_body) {
        uint8_t expected[SIGNATURE_SIZE];
        sign(out, body_len, key, expected);
        if (timingsafe_bcmp(signature, expected, SIGNATURE_SIZE) != 0) return false;
    }
    if (encrypt && !crypt_in_place(out, body_len, key, seed, false)) return false;
    *out_len = body_len;
    return true;
}

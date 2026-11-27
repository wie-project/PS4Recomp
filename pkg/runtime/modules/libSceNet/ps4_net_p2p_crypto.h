// SPDX-License-Identifier: GPL-2.0-or-later

// P2P payload protection, after shadPS4 (core/net/p2p_crypto.cpp): AES-128-CFB128 with a 4-byte
// IV seed repeated over the IV, and a signature that is the first 4 bytes of HMAC-SHA1 over the
// (encrypted) body. CommonCrypto stands in for OpenSSL.
//
// Protected layout: [iv seed 4, if encrypted][signature 4, if signed][body]

#ifndef PS4_NET_P2P_CRYPTO_H
#define PS4_NET_P2P_CRYPTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS4_P2P_KEY_SIZE 16
#define PS4_P2P_PROTECT_OVERHEAD 8 // iv seed + signature, at most

// out must hold len + PS4_P2P_PROTECT_OVERHEAD bytes; *out_len gets the protected size.
bool ps4_p2p_protect(const uint8_t *data, size_t len, bool encrypt, bool sign, const uint8_t key[PS4_P2P_KEY_SIZE],
                     uint8_t *out, size_t *out_len);
// Fails on a short packet, a bad signature or a cipher error. out must hold len bytes.
bool ps4_p2p_unprotect(const uint8_t *data, size_t len, bool encrypt, bool sign, const uint8_t key[PS4_P2P_KEY_SIZE],
                       uint8_t *out, size_t *out_len);
// The first 4 bytes of HMAC-SHA1 over an empty message, keyed with the title's 16-byte value.
void ps4_p2p_derive_communication_id(const uint8_t value[16], uint8_t out[4]);

#ifdef __cplusplus
}
#endif

#endif // PS4_NET_P2P_CRYPTO_H

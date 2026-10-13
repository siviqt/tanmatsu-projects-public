// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { uint8_t secret[128], public_key[65]; size_t secret_size; } sb_net_key_t;
void sb_net_wipe(void *p, size_t n);
bool sb_net_equal(const void *a, const void *b, size_t n);
bool sb_net_random(void *p, size_t n);
bool sb_net_hash(const void *p, size_t n, uint8_t out[32]);
bool sb_net_keygen(sb_net_key_t *key);
bool sb_net_shared(const sb_net_key_t *key, const uint8_t peer[65], uint8_t out[32]);
bool sb_net_kdf(const uint8_t shared[32], const uint8_t transcript[32], uint8_t out[72]);
bool sb_net_aead(bool encrypt, const uint8_t key[16], const uint8_t nonce[12],
                 const uint8_t *aad, size_t aad_n, const uint8_t *input, size_t n,
                 uint8_t *output, uint8_t tag[16]);

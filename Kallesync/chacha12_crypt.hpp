#pragma once
#include <cstdint>
#include <cstring>
#include "rolling_key.hpp"

static inline void chacha12_crypt_inplace(const uint8_t key[32], const uint8_t nonce[12],
                                          unsigned char* data, size_t len) {
    auto rotl32 = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    
    auto quarter_round = [&](uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
        a += b; d ^= a; d = rotl32(d, 16);
        c += d; b ^= c; b = rotl32(b, 12);
        a += b; d ^= a; d = rotl32(d, 8);
        c += d; b ^= c; b = rotl32(b, 7);
    };

    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    std::memcpy(&state[4], key, 32);
    state[12] = 0;
    state[13] = static_cast<uint32_t>(nonce[0]) | (static_cast<uint32_t>(nonce[1]) << 8)
              | (static_cast<uint32_t>(nonce[2]) << 16) | (static_cast<uint32_t>(nonce[3]) << 24);
    state[14] = static_cast<uint32_t>(nonce[4]) | (static_cast<uint32_t>(nonce[5]) << 8)
              | (static_cast<uint32_t>(nonce[6]) << 16) | (static_cast<uint32_t>(nonce[7]) << 24);
    state[15] = static_cast<uint32_t>(nonce[8]) | (static_cast<uint32_t>(nonce[9]) << 8)
              | (static_cast<uint32_t>(nonce[10]) << 16) | (static_cast<uint32_t>(nonce[11]) << 24);

    size_t offset = 0;
    while (offset < len) {
        uint32_t working[16];
        std::memcpy(working, state, 64);
        for (int i = 0; i < 6; ++i) {
            quarter_round(working[0], working[4], working[8],  working[12]);
            quarter_round(working[1], working[5], working[9],  working[13]);
            quarter_round(working[2], working[6], working[10], working[14]);
            quarter_round(working[3], working[7], working[11], working[15]);
            quarter_round(working[0], working[5], working[10], working[15]);
            quarter_round(working[1], working[6], working[11], working[12]);
            quarter_round(working[2], working[7], working[8],  working[13]);
            quarter_round(working[3], working[4], working[9],  working[14]);
        }
        for (int i = 0; i < 16; ++i) working[i] += state[i];

        uint8_t* keystream = reinterpret_cast<uint8_t*>(working);
        size_t block_len = (len - offset < 64) ? (len - offset) : 64;
        for (size_t i = 0; i < block_len; ++i) {
            data[offset + i] ^= keystream[i];
        }
        offset += block_len;
        state[12]++;
    }
}

inline void encrypt_packets(RollingKeyManager& mgr, unsigned char* data, 
                            size_t payload_size, size_t packet_count) {
    uint8_t working_key[32];
    uint8_t nonce[12];
    
    for (size_t i = 0; i < packet_count; ++i) {
        mgr.derive_working_key(working_key);
        mgr.get_current_nonce(nonce);
        chacha12_crypt_inplace(working_key, nonce, data, payload_size);
        mgr.advance();
    }
}

inline void decrypt_packets(RollingKeyManager& mgr, unsigned char* data,
                            size_t payload_size, size_t packet_count) {
    uint8_t working_key[32];
    uint8_t nonce[12];
    
    for (size_t i = 0; i < packet_count; ++i) {
        mgr.derive_working_key(working_key);
        mgr.get_current_nonce(nonce);
        chacha12_crypt_inplace(working_key, nonce, data, payload_size);
        mgr.advance();
    }
}

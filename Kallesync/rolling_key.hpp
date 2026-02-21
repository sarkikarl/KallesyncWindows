#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <iostream>

class RollingKeyManager {
public:
    static constexpr size_t MASTER_KEY_SIZE = 1024;
    static constexpr size_t WINDOW_SIZE = 256;
    static constexpr size_t WORKING_KEY_SIZE = 32;
    static constexpr size_t NONCE_SIZE = 12;

private:
    std::array<uint8_t, MASTER_KEY_SIZE + 256> master_key_;
    uint64_t derive_nonce_;
    uint64_t packet_count_;

    static inline uint32_t rotl32(uint32_t x, int n) {
        return (x << n) | (x >> (32 - n));
    }

    static void chacha_quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
        a += b; d ^= a; d = rotl32(d, 16);
        c += d; b ^= c; b = rotl32(b, 12);
        a += b; d ^= a; d = rotl32(d, 8);
        c += d; b ^= c; b = rotl32(b, 7);
    }

    static void chacha12_block(const uint32_t input[16], uint32_t output[16]) {
        std::memcpy(output, input, 64);
        for (int i = 0; i < 6; ++i) { // 12 rounds = 6 double-rounds
            // column rounds
            chacha_quarter_round(output[0], output[4], output[8],  output[12]);
            chacha_quarter_round(output[1], output[5], output[9],  output[13]);
            chacha_quarter_round(output[2], output[6], output[10], output[14]);
            chacha_quarter_round(output[3], output[7], output[11], output[15]);
            // diagonal rounds
            chacha_quarter_round(output[0], output[5], output[10], output[15]);
            chacha_quarter_round(output[1], output[6], output[11], output[12]);
            chacha_quarter_round(output[2], output[7], output[8],  output[13]);
            chacha_quarter_round(output[3], output[4], output[9],  output[14]);
        }
        for (int i = 0; i < 16; ++i) {
            output[i] += input[i];
        }
    }

    void build_nonce_bytes(uint64_t nonce_val, uint8_t out[NONCE_SIZE]) const {
        // bytes 0-7: uint64 little-endian
        for (int i = 0; i < 8; ++i) {
            out[i] = static_cast<uint8_t>(nonce_val >> (i * 8));
        }
        // bytes 8-11: XOR fold of upper and lower 4 bytes
        uint32_t lower = static_cast<uint32_t>(nonce_val & 0xFFFFFFFF);
        uint32_t upper = static_cast<uint32_t>(nonce_val >> 32);
        uint32_t fold = lower ^ upper;
        for (int i = 0; i < 4; ++i) {
            out[8 + i] = static_cast<uint8_t>(fold >> (i * 8));
        }
    }

    void chacha12_keystream(const uint8_t key[32], const uint8_t nonce[12], uint8_t* out, size_t len) const {
        uint32_t state[16];
        // "expand 32-byte k"
        state[0] = 0x61707865;
        state[1] = 0x3320646e;
        state[2] = 0x79622d32;
        state[3] = 0x6b206574;

        // key
        std::memcpy(&state[4], key, 32);

        // counter
        state[12] = 0;

        // nonce
        state[13] = static_cast<uint32_t>(nonce[0])       | (static_cast<uint32_t>(nonce[1]) << 8)
                  | (static_cast<uint32_t>(nonce[2]) << 16) | (static_cast<uint32_t>(nonce[3]) << 24);
        state[14] = static_cast<uint32_t>(nonce[4])       | (static_cast<uint32_t>(nonce[5]) << 8)
                  | (static_cast<uint32_t>(nonce[6]) << 16) | (static_cast<uint32_t>(nonce[7]) << 24);
        state[15] = static_cast<uint32_t>(nonce[8])       | (static_cast<uint32_t>(nonce[9]) << 8)
                  | (static_cast<uint32_t>(nonce[10]) << 16) | (static_cast<uint32_t>(nonce[11]) << 24);

        size_t offset = 0;
        while (offset < len) {
            uint32_t block[16];
            chacha12_block(state, block);
            size_t to_copy = (len - offset < 64) ? (len - offset) : 64;
            std::memcpy(out + offset, block, to_copy);
            offset += to_copy;
            state[12]++; // increment counter
        }
    }

public:
    RollingKeyManager(const uint8_t* master_key, uint64_t initial_derive_nonce)
        : derive_nonce_(initial_derive_nonce), packet_count_(0) {
        std::memcpy(master_key_.data(), master_key, MASTER_KEY_SIZE);
    }

    void derive_working_key(uint8_t out_key[WORKING_KEY_SIZE]) {
        // select window
        //size_t window_index = packet_count_ % 4;//what happens if we change 4 to a different input
        size_t window_index = (packet_count_ * 111 % 769) % (MASTER_KEY_SIZE);// / WINDOW_SIZE);
        size_t start = window_index;
        const uint8_t* window = master_key_.data() + start;
        //std::cout << "window_index: " << window_index << "\n";

        // original key is first 32 bytes of master
        const uint8_t* original_key = master_key_.data();

        // build 12-byte nonce from derive_nonce_
        uint8_t nonce_bytes[NONCE_SIZE];
        build_nonce_bytes(derive_nonce_, nonce_bytes);

        // generate keystream
        uint8_t keystream[WINDOW_SIZE];
        chacha12_keystream(original_key, nonce_bytes, keystream, WINDOW_SIZE);

        // XOR window with keystream, take first 32 bytes as working key
        for (size_t i = 0; i < WORKING_KEY_SIZE; ++i) {
            out_key[i] = window[i] ^ keystream[i];
        }
    }

    void get_current_nonce(uint8_t out_nonce[NONCE_SIZE]) const {
        build_nonce_bytes(derive_nonce_, out_nonce);
    }

    void advance() {
        packet_count_++;
        derive_nonce_ = (((derive_nonce_ * 3) / 4) + 173481);
    }

    uint64_t get_packet_count() const { return packet_count_; }
    uint64_t get_derive_nonce() const { return derive_nonce_; }

    void sync_to_packet(uint64_t target_packet, uint64_t initial_derive_nonce) {
        // rebuild state from scratch to target packet
        derive_nonce_ = initial_derive_nonce;
        packet_count_ = 0;
        while (packet_count_ < target_packet) {
            advance();
        }
    }
};

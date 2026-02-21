#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <array>
#include <random>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define HAVE_SSE2 1
#if defined(__AVX2__)
#define HAVE_AVX2 1
#endif
#endif

namespace crypto {

constexpr size_t KEY_SIZE   = 32;  // 256 bits
constexpr size_t NONCE_SIZE = 12;  // 96 bits (IETF variant)
constexpr size_t TAG_SIZE   = 16;  // 128 bits
constexpr size_t OVERHEAD   = NONCE_SIZE + TAG_SIZE;  // 28 bytes

// ============================================================================
// ChaCha20 Core
// ============================================================================

namespace detail {

inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

inline void quarter_round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d ^= a; d = rotl32(d, 16);
    c += d; b ^= c; b = rotl32(b, 12);
    a += b; d ^= a; d = rotl32(d, 8);
    c += d; b ^= c; b = rotl32(b, 7);
}

inline uint32_t load32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

inline void store32_le(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

inline void store64_le(uint8_t* p, uint64_t v) {
    store32_le(p, static_cast<uint32_t>(v));
    store32_le(p + 4, static_cast<uint32_t>(v >> 32));
}

#ifdef HAVE_AVX2

// AVX2 optimized ChaCha20 - processes 4 blocks (256 bytes) in parallel
class ChaCha20 {
public:
    void set_key(const uint8_t* key) {
        state_[0] = 0x61707865;
        state_[1] = 0x3320646e;
        state_[2] = 0x79622d32;
        state_[3] = 0x6b206574;
        for (int i = 0; i < 8; i++) {
            state_[4 + i] = load32_le(key + i * 4);
        }
    }
    
    void set_nonce(const uint8_t* nonce, uint32_t counter = 0) {
        state_[12] = counter;
        state_[13] = load32_le(nonce);
        state_[14] = load32_le(nonce + 4);
        state_[15] = load32_le(nonce + 8);
    }
    
    void block(uint8_t* out) {
        uint32_t x[16];
        std::memcpy(x, state_, sizeof(x));
        
        for (int i = 0; i < 10; i++) {
            quarter_round(x[0], x[4], x[8], x[12]);
            quarter_round(x[1], x[5], x[9], x[13]);
            quarter_round(x[2], x[6], x[10], x[14]);
            quarter_round(x[3], x[7], x[11], x[15]);
            quarter_round(x[0], x[5], x[10], x[15]);
            quarter_round(x[1], x[6], x[11], x[12]);
            quarter_round(x[2], x[7], x[8], x[13]);
            quarter_round(x[3], x[4], x[9], x[14]);
        }
        
        for (int i = 0; i < 16; i++) {
            store32_le(out + i * 4, x[i] + state_[i]);
        }
        state_[12]++;
    }
    
    void encrypt(const uint8_t* in, uint8_t* out, size_t len) {
        // Process 4 blocks at a time with AVX2
        while (len >= 256) {
            // Load state into 4 AVX2 vectors (transposed for parallel processing)
            // Each row: same element from 4 consecutive blocks
            __m256i row0 = _mm256_set1_epi32(state_[0]);
            __m256i row1 = _mm256_set1_epi32(state_[1]);
            __m256i row2 = _mm256_set1_epi32(state_[2]);
            __m256i row3 = _mm256_set1_epi32(state_[3]);
            __m256i row4 = _mm256_set1_epi32(state_[4]);
            __m256i row5 = _mm256_set1_epi32(state_[5]);
            __m256i row6 = _mm256_set1_epi32(state_[6]);
            __m256i row7 = _mm256_set1_epi32(state_[7]);
            __m256i row8 = _mm256_set1_epi32(state_[8]);
            __m256i row9 = _mm256_set1_epi32(state_[9]);
            __m256i row10 = _mm256_set1_epi32(state_[10]);
            __m256i row11 = _mm256_set1_epi32(state_[11]);
            // Counter: 4 different values
            __m256i row12 = _mm256_set_epi32(
                state_[12]+7, state_[12]+6, state_[12]+5, state_[12]+4,
                state_[12]+3, state_[12]+2, state_[12]+1, state_[12]);
            __m256i row13 = _mm256_set1_epi32(state_[13]);
            __m256i row14 = _mm256_set1_epi32(state_[14]);
            __m256i row15 = _mm256_set1_epi32(state_[15]);
            
            __m256i orig0 = row0, orig1 = row1, orig2 = row2, orig3 = row3;
            __m256i orig4 = row4, orig5 = row5, orig6 = row6, orig7 = row7;
            __m256i orig8 = row8, orig9 = row9, orig10 = row10, orig11 = row11;
            __m256i orig12 = row12, orig13 = row13, orig14 = row14, orig15 = row15;
            
            #define QROUND_AVX2(a, b, c, d) \
                a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); \
                d = _mm256_or_si256(_mm256_slli_epi32(d, 16), _mm256_srli_epi32(d, 16)); \
                c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); \
                b = _mm256_or_si256(_mm256_slli_epi32(b, 12), _mm256_srli_epi32(b, 20)); \
                a = _mm256_add_epi32(a, b); d = _mm256_xor_si256(d, a); \
                d = _mm256_or_si256(_mm256_slli_epi32(d, 8), _mm256_srli_epi32(d, 24)); \
                c = _mm256_add_epi32(c, d); b = _mm256_xor_si256(b, c); \
                b = _mm256_or_si256(_mm256_slli_epi32(b, 7), _mm256_srli_epi32(b, 25));
            
            for (int i = 0; i < 10; i++) {
                QROUND_AVX2(row0, row4, row8, row12)
                QROUND_AVX2(row1, row5, row9, row13)
                QROUND_AVX2(row2, row6, row10, row14)
                QROUND_AVX2(row3, row7, row11, row15)
                QROUND_AVX2(row0, row5, row10, row15)
                QROUND_AVX2(row1, row6, row11, row12)
                QROUND_AVX2(row2, row7, row8, row13)
                QROUND_AVX2(row3, row4, row9, row14)
            }
            #undef QROUND_AVX2
            
            row0 = _mm256_add_epi32(row0, orig0);
            row1 = _mm256_add_epi32(row1, orig1);
            row2 = _mm256_add_epi32(row2, orig2);
            row3 = _mm256_add_epi32(row3, orig3);
            row4 = _mm256_add_epi32(row4, orig4);
            row5 = _mm256_add_epi32(row5, orig5);
            row6 = _mm256_add_epi32(row6, orig6);
            row7 = _mm256_add_epi32(row7, orig7);
            row8 = _mm256_add_epi32(row8, orig8);
            row9 = _mm256_add_epi32(row9, orig9);
            row10 = _mm256_add_epi32(row10, orig10);
            row11 = _mm256_add_epi32(row11, orig11);
            row12 = _mm256_add_epi32(row12, orig12);
            row13 = _mm256_add_epi32(row13, orig13);
            row14 = _mm256_add_epi32(row14, orig14);
            row15 = _mm256_add_epi32(row15, orig15);
            
            // Transpose and store (complex operation - output 8 blocks)
            // For simplicity, extract and XOR block by block
            alignas(32) uint32_t blocks[8][16];
            
            #define EXTRACT_BLOCK(idx) \
                blocks[idx][0] = _mm256_extract_epi32(row0, idx); \
                blocks[idx][1] = _mm256_extract_epi32(row1, idx); \
                blocks[idx][2] = _mm256_extract_epi32(row2, idx); \
                blocks[idx][3] = _mm256_extract_epi32(row3, idx); \
                blocks[idx][4] = _mm256_extract_epi32(row4, idx); \
                blocks[idx][5] = _mm256_extract_epi32(row5, idx); \
                blocks[idx][6] = _mm256_extract_epi32(row6, idx); \
                blocks[idx][7] = _mm256_extract_epi32(row7, idx); \
                blocks[idx][8] = _mm256_extract_epi32(row8, idx); \
                blocks[idx][9] = _mm256_extract_epi32(row9, idx); \
                blocks[idx][10] = _mm256_extract_epi32(row10, idx); \
                blocks[idx][11] = _mm256_extract_epi32(row11, idx); \
                blocks[idx][12] = _mm256_extract_epi32(row12, idx); \
                blocks[idx][13] = _mm256_extract_epi32(row13, idx); \
                blocks[idx][14] = _mm256_extract_epi32(row14, idx); \
                blocks[idx][15] = _mm256_extract_epi32(row15, idx);
            
            EXTRACT_BLOCK(0) EXTRACT_BLOCK(1) EXTRACT_BLOCK(2) EXTRACT_BLOCK(3)
            EXTRACT_BLOCK(4) EXTRACT_BLOCK(5) EXTRACT_BLOCK(6) EXTRACT_BLOCK(7)
            #undef EXTRACT_BLOCK
            
            for (int b = 0; b < 8; b++) {
                const uint32_t* inp = reinterpret_cast<const uint32_t*>(in + b * 64);
                uint32_t* outp = reinterpret_cast<uint32_t*>(out + b * 64);
                for (int j = 0; j < 16; j++) {
                    outp[j] = inp[j] ^ blocks[b][j];
                }
            }
            
            state_[12] += 8;
            in += 512;
            out += 512;
            len -= 512;
        }
        
        // Process remaining with scalar
        while (len >= 64) {
            uint8_t block_buf[64];
            block(block_buf);
            for (int i = 0; i < 64; i++) {
                out[i] = in[i] ^ block_buf[i];
            }
            in += 64;
            out += 64;
            len -= 64;
        }
        
        if (len > 0) {
            uint8_t block_buf[64];
            block(block_buf);
            for (size_t i = 0; i < len; i++) {
                out[i] = in[i] ^ block_buf[i];
            }
        }
    }
    
private:
    uint32_t state_[16];
};

#elif defined(HAVE_SSE2)

// SSE2 optimized ChaCha20 - processes data using 128-bit SIMD
class ChaCha20 {
public:
    void set_key(const uint8_t* key) {
        state_[0] = 0x61707865;
        state_[1] = 0x3320646e;
        state_[2] = 0x79622d32;
        state_[3] = 0x6b206574;
        for (int i = 0; i < 8; i++) {
            state_[4 + i] = load32_le(key + i * 4);
        }
    }
    
    void set_nonce(const uint8_t* nonce, uint32_t counter = 0) {
        state_[12] = counter;
        state_[13] = load32_le(nonce);
        state_[14] = load32_le(nonce + 4);
        state_[15] = load32_le(nonce + 8);
    }
    
    void block(uint8_t* out) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[0]));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[4]));
        __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[8]));
        __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[12]));
        
        __m128i a_orig = a, b_orig = b, c_orig = c, d_orig = d;
        
        for (int i = 0; i < 10; i++) {
            // Column round
            a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
            d = _mm_or_si128(_mm_slli_epi32(d, 16), _mm_srli_epi32(d, 16));
            c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
            b = _mm_or_si128(_mm_slli_epi32(b, 12), _mm_srli_epi32(b, 20));
            a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
            d = _mm_or_si128(_mm_slli_epi32(d, 8), _mm_srli_epi32(d, 24));
            c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
            b = _mm_or_si128(_mm_slli_epi32(b, 7), _mm_srli_epi32(b, 25));
            
            // Shuffle for diagonal round
            b = _mm_shuffle_epi32(b, _MM_SHUFFLE(0, 3, 2, 1));
            c = _mm_shuffle_epi32(c, _MM_SHUFFLE(1, 0, 3, 2));
            d = _mm_shuffle_epi32(d, _MM_SHUFFLE(2, 1, 0, 3));
            
            // Diagonal round
            a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
            d = _mm_or_si128(_mm_slli_epi32(d, 16), _mm_srli_epi32(d, 16));
            c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
            b = _mm_or_si128(_mm_slli_epi32(b, 12), _mm_srli_epi32(b, 20));
            a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
            d = _mm_or_si128(_mm_slli_epi32(d, 8), _mm_srli_epi32(d, 24));
            c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
            b = _mm_or_si128(_mm_slli_epi32(b, 7), _mm_srli_epi32(b, 25));
            
            // Unshuffle
            b = _mm_shuffle_epi32(b, _MM_SHUFFLE(2, 1, 0, 3));
            c = _mm_shuffle_epi32(c, _MM_SHUFFLE(1, 0, 3, 2));
            d = _mm_shuffle_epi32(d, _MM_SHUFFLE(0, 3, 2, 1));
        }
        
        a = _mm_add_epi32(a, a_orig);
        b = _mm_add_epi32(b, b_orig);
        c = _mm_add_epi32(c, c_orig);
        d = _mm_add_epi32(d, d_orig);
        
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out),      a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16), b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 32), c);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 48), d);
        
        state_[12]++;
    }
    
    void encrypt(const uint8_t* in, uint8_t* out, size_t len) {
        while (len >= 64) {
            __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[0]));
            __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[4]));
            __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[8]));
            __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[12]));
            
            __m128i a_orig = a, b_orig = b, c_orig = c, d_orig = d;
            
            for (int i = 0; i < 10; i++) {
                a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
                d = _mm_or_si128(_mm_slli_epi32(d, 16), _mm_srli_epi32(d, 16));
                c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
                b = _mm_or_si128(_mm_slli_epi32(b, 12), _mm_srli_epi32(b, 20));
                a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
                d = _mm_or_si128(_mm_slli_epi32(d, 8), _mm_srli_epi32(d, 24));
                c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
                b = _mm_or_si128(_mm_slli_epi32(b, 7), _mm_srli_epi32(b, 25));
                
                b = _mm_shuffle_epi32(b, _MM_SHUFFLE(0, 3, 2, 1));
                c = _mm_shuffle_epi32(c, _MM_SHUFFLE(1, 0, 3, 2));
                d = _mm_shuffle_epi32(d, _MM_SHUFFLE(2, 1, 0, 3));
                
                a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
                d = _mm_or_si128(_mm_slli_epi32(d, 16), _mm_srli_epi32(d, 16));
                c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
                b = _mm_or_si128(_mm_slli_epi32(b, 12), _mm_srli_epi32(b, 20));
                a = _mm_add_epi32(a, b); d = _mm_xor_si128(d, a);
                d = _mm_or_si128(_mm_slli_epi32(d, 8), _mm_srli_epi32(d, 24));
                c = _mm_add_epi32(c, d); b = _mm_xor_si128(b, c);
                b = _mm_or_si128(_mm_slli_epi32(b, 7), _mm_srli_epi32(b, 25));
                
                b = _mm_shuffle_epi32(b, _MM_SHUFFLE(2, 1, 0, 3));
                c = _mm_shuffle_epi32(c, _MM_SHUFFLE(1, 0, 3, 2));
                d = _mm_shuffle_epi32(d, _MM_SHUFFLE(0, 3, 2, 1));
            }
            
            a = _mm_add_epi32(a, a_orig);
            b = _mm_add_epi32(b, b_orig);
            c = _mm_add_epi32(c, c_orig);
            d = _mm_add_epi32(d, d_orig);
            
            __m128i p0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in));
            __m128i p1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 16));
            __m128i p2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 32));
            __m128i p3 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + 48));
            
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out),      _mm_xor_si128(a, p0));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16), _mm_xor_si128(b, p1));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 32), _mm_xor_si128(c, p2));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 48), _mm_xor_si128(d, p3));
            
            state_[12]++;
            in += 64;
            out += 64;
            len -= 64;
        }
        
        if (len > 0) {
            uint8_t block_buf[64];
            block(block_buf);
            for (size_t i = 0; i < len; i++) {
                out[i] = in[i] ^ block_buf[i];
            }
        }
    }
    
private:
    uint32_t state_[16];
};

#else

class ChaCha20 {
public:
    void set_key(const uint8_t* key) {
        // "expand 32-byte k"
        state_[0] = 0x61707865;
        state_[1] = 0x3320646e;
        state_[2] = 0x79622d32;
        state_[3] = 0x6b206574;
        
        for (int i = 0; i < 8; i++) {
            state_[4 + i] = load32_le(key + i * 4);
        }
    }
    
    void set_nonce(const uint8_t* nonce, uint32_t counter = 0) {
        state_[12] = counter;
        state_[13] = load32_le(nonce);
        state_[14] = load32_le(nonce + 4);
        state_[15] = load32_le(nonce + 8);
    }
    
    void block(uint8_t* out) {
        uint32_t x[16];
        std::memcpy(x, state_, sizeof(x));
        
        for (int i = 0; i < 10; i++) {
            // Column rounds
            quarter_round(x[0], x[4], x[8],  x[12]);
            quarter_round(x[1], x[5], x[9],  x[13]);
            quarter_round(x[2], x[6], x[10], x[14]);
            quarter_round(x[3], x[7], x[11], x[15]);
            // Diagonal rounds
            quarter_round(x[0], x[5], x[10], x[15]);
            quarter_round(x[1], x[6], x[11], x[12]);
            quarter_round(x[2], x[7], x[8],  x[13]);
            quarter_round(x[3], x[4], x[9],  x[14]);
        }
        
        for (int i = 0; i < 16; i++) {
            store32_le(out + i * 4, x[i] + state_[i]);
        }
        
        state_[12]++;  // increment counter
    }
    
    void encrypt(const uint8_t* in, uint8_t* out, size_t len) {
        uint8_t block_buf[64];
        
        while (len >= 64) {
            block(block_buf);
            for (int i = 0; i < 64; i++) {
                out[i] = in[i] ^ block_buf[i];
            }
            in += 64;
            out += 64;
            len -= 64;
        }
        
        if (len > 0) {
            block(block_buf);
            for (size_t i = 0; i < len; i++) {
                out[i] = in[i] ^ block_buf[i];
            }
        }
    }
    
private:
    uint32_t state_[16];
};

#endif // HAVE_SSE2

// ============================================================================
// Poly1305 MAC
// ============================================================================

class Poly1305 {
public:
    void init(const uint8_t* key) {
        // r = key[0..15] with clamping
        r_[0] = load32_le(key)      & 0x0fffffff;
        r_[1] = load32_le(key + 4)  & 0x0ffffffc;
        r_[2] = load32_le(key + 8)  & 0x0ffffffc;
        r_[3] = load32_le(key + 12) & 0x0ffffffc;
        
        // s = key[16..31]
        s_[0] = load32_le(key + 16);
        s_[1] = load32_le(key + 20);
        s_[2] = load32_le(key + 24);
        s_[3] = load32_le(key + 28);
        
        // Initialize accumulator
        h_[0] = h_[1] = h_[2] = h_[3] = h_[4] = 0;
        
        leftover_ = 0;
    }
    
    void update(const uint8_t* data, size_t len) {
        // Handle leftover
        if (leftover_) {
            size_t want = 16 - leftover_;
            if (want > len) want = len;
            std::memcpy(buffer_ + leftover_, data, want);
            leftover_ += want;
            data += want;
            len -= want;
            if (leftover_ < 16) return;
            process_block(buffer_, 16);
            leftover_ = 0;
        }
        
        // Process full blocks
        while (len >= 16) {
            process_block(data, 16);
            data += 16;
            len -= 16;
        }
        
        // Store leftover
        if (len > 0) {
            std::memcpy(buffer_, data, len);
            leftover_ = len;
        }
    }
    
    void finish(uint8_t* tag) {
        // Process final block if any
        if (leftover_ > 0) {
            buffer_[leftover_] = 1;
            std::memset(buffer_ + leftover_ + 1, 0, 16 - leftover_ - 1);
            process_block(buffer_, leftover_);
        }
        
        // Fully reduce h
        uint32_t h0 = h_[0], h1 = h_[1], h2 = h_[2], h3 = h_[3], h4 = h_[4];
        
        uint32_t c = (h4 >> 2) + (h4 & ~3u);
        h4 &= 3;
        h0 += c;
        c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;
        c = h1 >> 26; h1 &= 0x3ffffff;
        h2 += c;
        c = h2 >> 26; h2 &= 0x3ffffff;
        h3 += c;
        c = h3 >> 26; h3 &= 0x3ffffff;
        h4 += c;
        c = (h4 >> 2) + (h4 & ~3u);
        h4 &= 3;
        h0 += c;
        
        // Compute h - p
        uint32_t g0 = h0 + 5;
        c = g0 >> 26; g0 &= 0x3ffffff;
        uint32_t g1 = h1 + c;
        c = g1 >> 26; g1 &= 0x3ffffff;
        uint32_t g2 = h2 + c;
        c = g2 >> 26; g2 &= 0x3ffffff;
        uint32_t g3 = h3 + c;
        c = g3 >> 26; g3 &= 0x3ffffff;
        uint32_t g4 = h4 + c - 4;
        
        // Select h or h-p
        uint32_t mask = (g4 >> 31) - 1;
        g0 &= mask;
        g1 &= mask;
        g2 &= mask;
        g3 &= mask;
        g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0;
        h1 = (h1 & mask) | g1;
        h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3;
        h4 = (h4 & mask) | g4;
        
        // h = h % (2^128)
        uint64_t f0 = (h0 | (h1 << 26)) + static_cast<uint64_t>(s_[0]);
        uint64_t f1 = ((h1 >> 6) | (h2 << 20)) + static_cast<uint64_t>(s_[1]);
        uint64_t f2 = ((h2 >> 12) | (h3 << 14)) + static_cast<uint64_t>(s_[2]);
        uint64_t f3 = ((h3 >> 18) | (h4 << 8)) + static_cast<uint64_t>(s_[3]);
        
        f1 += f0 >> 32;
        f2 += f1 >> 32;
        f3 += f2 >> 32;
        
        store32_le(tag,      static_cast<uint32_t>(f0));
        store32_le(tag + 4,  static_cast<uint32_t>(f1));
        store32_le(tag + 8,  static_cast<uint32_t>(f2));
        store32_le(tag + 12, static_cast<uint32_t>(f3));
    }
    
private:
    void process_block(const uint8_t* data, size_t len) {
        uint32_t hibit = (len < 16) ? 0 : (1 << 24);
        
        uint32_t t0 = load32_le(data);
        uint32_t t1 = load32_le(data + 4);
        uint32_t t2 = load32_le(data + 8);
        uint32_t t3 = load32_le(data + 12);
        
        h_[0] += t0 & 0x3ffffff;
        h_[1] += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
        h_[2] += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
        h_[3] += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
        h_[4] += (t3 >> 8) | hibit;
        
        // h *= r
        uint64_t d0 = static_cast<uint64_t>(h_[0]) * r_[0]
                    + static_cast<uint64_t>(h_[1]) * (r_[3] * 5)
                    + static_cast<uint64_t>(h_[2]) * (r_[2] * 5)
                    + static_cast<uint64_t>(h_[3]) * (r_[1] * 5)
                    + static_cast<uint64_t>(h_[4]) * (r_[0] * 5);
        
        uint64_t d1 = static_cast<uint64_t>(h_[0]) * r_[1]
                    + static_cast<uint64_t>(h_[1]) * r_[0]
                    + static_cast<uint64_t>(h_[2]) * (r_[3] * 5)
                    + static_cast<uint64_t>(h_[3]) * (r_[2] * 5)
                    + static_cast<uint64_t>(h_[4]) * (r_[1] * 5);
        
        uint64_t d2 = static_cast<uint64_t>(h_[0]) * r_[2]
                    + static_cast<uint64_t>(h_[1]) * r_[1]
                    + static_cast<uint64_t>(h_[2]) * r_[0]
                    + static_cast<uint64_t>(h_[3]) * (r_[3] * 5)
                    + static_cast<uint64_t>(h_[4]) * (r_[2] * 5);
        
        uint64_t d3 = static_cast<uint64_t>(h_[0]) * r_[3]
                    + static_cast<uint64_t>(h_[1]) * r_[2]
                    + static_cast<uint64_t>(h_[2]) * r_[1]
                    + static_cast<uint64_t>(h_[3]) * r_[0]
                    + static_cast<uint64_t>(h_[4]) * (r_[3] * 5);
        
        uint64_t d4 = static_cast<uint64_t>(h_[4]) * (r_[0] & 3);
        
        // Partial reduction
        uint64_t c;
        c = d0 >> 26; h_[0] = d0 & 0x3ffffff;
        d1 += c;
        c = d1 >> 26; h_[1] = d1 & 0x3ffffff;
        d2 += c;
        c = d2 >> 26; h_[2] = d2 & 0x3ffffff;
        d3 += c;
        c = d3 >> 26; h_[3] = d3 & 0x3ffffff;
        d4 += c;
        c = d4 >> 26; h_[4] = d4 & 0x3ffffff;
        h_[0] += c * 5;
        c = h_[0] >> 26; h_[0] &= 0x3ffffff;
        h_[1] += c;
    }
    
    uint32_t r_[4];
    uint32_t s_[4];
    uint32_t h_[5];
    uint8_t buffer_[16];
    size_t leftover_;
};

// ============================================================================
// Random bytes (using system CSPRNG)
// ============================================================================

inline void random_bytes(uint8_t* buf, size_t len) {
    std::random_device rd;
    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<uint8_t>(rd());
    }
}

} // namespace detail

// ============================================================================
// Public API
// ============================================================================

inline std::vector<uint8_t> generate_key() {
    std::vector<uint8_t> key(KEY_SIZE);
    detail::random_bytes(key.data(), KEY_SIZE);
    return key;
}

// Encrypt plaintext
// Returns: [nonce (12)] [ciphertext (N)] [tag (16)]
inline std::vector<uint8_t> encrypt(
    const uint8_t* plaintext,
    size_t plaintext_len,
    const uint8_t* key,
    const uint8_t* aad = nullptr,
    size_t aad_len = 0
) {
    std::vector<uint8_t> packet(NONCE_SIZE + plaintext_len + TAG_SIZE);
    
    uint8_t* nonce      = packet.data();
    uint8_t* ciphertext = packet.data() + NONCE_SIZE;
    uint8_t* tag        = packet.data() + NONCE_SIZE + plaintext_len;
    
    // Generate random nonce
    detail::random_bytes(nonce, NONCE_SIZE);
    
    // Generate Poly1305 key (block 0)
    detail::ChaCha20 chacha;
    chacha.set_key(key);
    chacha.set_nonce(nonce, 0);
    
    uint8_t poly_key[64];
    chacha.block(poly_key);  // First 32 bytes are the Poly1305 key
    
    // Encrypt (starting from block 1)
    chacha.set_nonce(nonce, 1);
    chacha.encrypt(plaintext, ciphertext, plaintext_len);
    
    // Compute tag
    detail::Poly1305 poly;
    poly.init(poly_key);
    
    // AAD with padding
    if (aad && aad_len > 0) {
        poly.update(aad, aad_len);
        if (aad_len % 16 != 0) {
            uint8_t pad[16] = {0};
            poly.update(pad, 16 - (aad_len % 16));
        }
    }
    
    // Ciphertext with padding
    poly.update(ciphertext, plaintext_len);
    if (plaintext_len % 16 != 0) {
        uint8_t pad[16] = {0};
        poly.update(pad, 16 - (plaintext_len % 16));
    }
    
    // Lengths (little-endian)
    uint8_t lengths[16];
    detail::store64_le(lengths, aad_len);
    detail::store64_le(lengths + 8, plaintext_len);
    poly.update(lengths, 16);
    
    poly.finish(tag);
    
    return packet;
}

// Convenience overload
inline std::vector<uint8_t> encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& aad = {}
) {
    if (key.size() != KEY_SIZE) {
        throw std::invalid_argument("key must be 32 bytes");
    }
    return encrypt(
        plaintext.data(), plaintext.size(),
        key.data(),
        aad.empty() ? nullptr : aad.data(), aad.size()
    );
}

// Decrypt packet
// Input: [nonce (12)] [ciphertext (N)] [tag (16)]
inline std::vector<uint8_t> decrypt(
    const uint8_t* packet,
    size_t packet_len,
    const uint8_t* key,
    const uint8_t* aad = nullptr,
    size_t aad_len = 0
) {
    if (packet_len < OVERHEAD) {
        throw std::invalid_argument("packet too small");
    }
    
    const uint8_t* nonce      = packet;
    const uint8_t* ciphertext = packet + NONCE_SIZE;
    size_t ciphertext_len     = packet_len - NONCE_SIZE - TAG_SIZE;
    const uint8_t* tag        = packet + packet_len - TAG_SIZE;
    
    // Generate Poly1305 key
    detail::ChaCha20 chacha;
    chacha.set_key(key);
    chacha.set_nonce(nonce, 0);
    
    uint8_t poly_key[64];
    chacha.block(poly_key);
    
    // Verify tag BEFORE decrypting
    detail::Poly1305 poly;
    poly.init(poly_key);
    
    if (aad && aad_len > 0) {
        poly.update(aad, aad_len);
        if (aad_len % 16 != 0) {
            uint8_t pad[16] = {0};
            poly.update(pad, 16 - (aad_len % 16));
        }
    }
    
    poly.update(ciphertext, ciphertext_len);
    if (ciphertext_len % 16 != 0) {
        uint8_t pad[16] = {0};
        poly.update(pad, 16 - (ciphertext_len % 16));
    }
    
    uint8_t lengths[16];
    detail::store64_le(lengths, aad_len);
    detail::store64_le(lengths + 8, ciphertext_len);
    poly.update(lengths, 16);
    
    uint8_t computed_tag[16];
    poly.finish(computed_tag);
    
    // Constant-time comparison
    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) {
        diff |= computed_tag[i] ^ tag[i];
    }
    if (diff != 0) {
        throw std::runtime_error("decryption failed: authentication error");
    }
    
    // Decrypt
    std::vector<uint8_t> plaintext(ciphertext_len);
    chacha.set_nonce(nonce, 1);
    chacha.encrypt(ciphertext, plaintext.data(), ciphertext_len);
    
    return plaintext;
}

// Convenience overload
inline std::vector<uint8_t> decrypt(
    const std::vector<uint8_t>& packet,
    const std::vector<uint8_t>& key,
    const std::vector<uint8_t>& aad = {}
) {
    if (key.size() != KEY_SIZE) {
        throw std::invalid_argument("key must be 32 bytes");
    }
    return decrypt(
        packet.data(), packet.size(),
        key.data(),
        aad.empty() ? nullptr : aad.data(), aad.size()
    );
}

} // namespace crypto
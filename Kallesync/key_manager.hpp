#pragma once

#include "chacha20_poly1305.hpp"
#include <string>
#include <map>
#include <fstream>
#include <cstdint>

namespace crypto {

    // ============================================================================
    // SHA-256 (needed for PBKDF2)
    // ============================================================================

    namespace detail {

        class SHA256 {
        public:
            SHA256() { reset(); }

            void reset() {
                h_[0] = 0x6a09e667; h_[1] = 0xbb67ae85;
                h_[2] = 0x3c6ef372; h_[3] = 0xa54ff53a;
                h_[4] = 0x510e527f; h_[5] = 0x9b05688c;
                h_[6] = 0x1f83d9ab; h_[7] = 0x5be0cd19;
                total_len_ = 0;
                buf_len_ = 0;
            }

            void update(const uint8_t* data, size_t len) {
                total_len_ += len;

                if (buf_len_ > 0) {
                    size_t fill = 64 - buf_len_;
                    if (len < fill) {
                        std::memcpy(buf_ + buf_len_, data, len);
                        buf_len_ += len;
                        return;
                    }
                    std::memcpy(buf_ + buf_len_, data, fill);
                    transform(buf_);
                    data += fill;
                    len -= fill;
                    buf_len_ = 0;
                }

                while (len >= 64) {
                    transform(data);
                    data += 64;
                    len -= 64;
                }

                if (len > 0) {
                    std::memcpy(buf_, data, len);
                    buf_len_ = len;
                }
            }

            void finish(uint8_t* out) {
                uint64_t bits = total_len_ * 8;

                uint8_t pad = 0x80;
                update(&pad, 1);

                pad = 0x00;
                while (buf_len_ != 56) {
                    update(&pad, 1);
                }

                uint8_t len_be[8];
                for (int i = 7; i >= 0; i--) {
                    len_be[i] = static_cast<uint8_t>(bits);
                    bits >>= 8;
                }
                update(len_be, 8);

                for (int i = 0; i < 8; i++) {
                    out[i * 4 + 0] = static_cast<uint8_t>(h_[i] >> 24);
                    out[i * 4 + 1] = static_cast<uint8_t>(h_[i] >> 16);
                    out[i * 4 + 2] = static_cast<uint8_t>(h_[i] >> 8);
                    out[i * 4 + 3] = static_cast<uint8_t>(h_[i]);
                }
            }

        private:
            static constexpr uint32_t K[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
                0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
                0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
                0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
                0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
                0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
                0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
                0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
                0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
                0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
                0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
                0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
            };

            static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
            static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
            static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
            static uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
            static uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
            static uint32_t gam0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
            static uint32_t gam1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

            void transform(const uint8_t* block) {
                uint32_t w[64];

                for (int i = 0; i < 16; i++) {
                    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24)
                        | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                        | (static_cast<uint32_t>(block[i * 4 + 2]) << 8)
                        | static_cast<uint32_t>(block[i * 4 + 3]);
                }

                for (int i = 16; i < 64; i++) {
                    w[i] = gam1(w[i - 2]) + w[i - 7] + gam0(w[i - 15]) + w[i - 16];
                }

                uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
                uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];

                for (int i = 0; i < 64; i++) {
                    uint32_t t1 = h + sig1(e) + ch(e, f, g) + K[i] + w[i];
                    uint32_t t2 = sig0(a) + maj(a, b, c);
                    h = g; g = f; f = e; e = d + t1;
                    d = c; c = b; b = a; a = t1 + t2;
                }

                h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
                h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
            }

            uint32_t h_[8];
            uint8_t buf_[64];
            size_t buf_len_;
            uint64_t total_len_;
        };

        constexpr uint32_t SHA256::K[64];

        // ============================================================================
        // HMAC-SHA256
        // ============================================================================

        inline void hmac_sha256(
            const uint8_t* key, size_t key_len,
            const uint8_t* data, size_t data_len,
            uint8_t* out
        ) {
            uint8_t k_pad[64];

            if (key_len > 64) {
                SHA256 h;
                h.update(key, key_len);
                h.finish(k_pad);
                std::memset(k_pad + 32, 0, 32);
            }
            else {
                std::memcpy(k_pad, key, key_len);
                std::memset(k_pad + key_len, 0, 64 - key_len);
            }

            uint8_t i_pad[64], o_pad[64];
            for (int i = 0; i < 64; i++) {
                i_pad[i] = k_pad[i] ^ 0x36;
                o_pad[i] = k_pad[i] ^ 0x5c;
            }

            uint8_t inner[32];
            SHA256 h1;
            h1.update(i_pad, 64);
            h1.update(data, data_len);
            h1.finish(inner);

            SHA256 h2;
            h2.update(o_pad, 64);
            h2.update(inner, 32);
            h2.finish(out);
        }

        // ============================================================================
        // PBKDF2-HMAC-SHA256
        // ============================================================================

        inline void pbkdf2_sha256(
            const uint8_t* password, size_t password_len,
            const uint8_t* salt, size_t salt_len,
            uint32_t iterations,
            uint8_t* out, size_t out_len
        ) {
            uint32_t block_num = 1;
            size_t offset = 0;

            while (offset < out_len) {
                // U_1 = HMAC(password, salt || block_num_be)
                std::vector<uint8_t> salt_block(salt_len + 4);
                std::memcpy(salt_block.data(), salt, salt_len);
                salt_block[salt_len + 0] = static_cast<uint8_t>(block_num >> 24);
                salt_block[salt_len + 1] = static_cast<uint8_t>(block_num >> 16);
                salt_block[salt_len + 2] = static_cast<uint8_t>(block_num >> 8);
                salt_block[salt_len + 3] = static_cast<uint8_t>(block_num);

                uint8_t u[32], t[32];
                hmac_sha256(password, password_len, salt_block.data(), salt_block.size(), u);
                std::memcpy(t, u, 32);

                for (uint32_t i = 1; i < iterations; i++) {
                    hmac_sha256(password, password_len, u, 32, u);
                    for (int j = 0; j < 32; j++) {
                        t[j] ^= u[j];
                    }
                }

                size_t to_copy = std::min(static_cast<size_t>(32), out_len - offset);
                std::memcpy(out + offset, t, to_copy);

                offset += 32;
                block_num++;
            }
        }

    } // namespace detail

    // ============================================================================
    // Constants
    // ============================================================================

    constexpr size_t SALT_SIZE = 16;
    constexpr uint32_t PBKDF2_ITERATIONS = 100000;

    // ============================================================================
    // Encrypt with explicit nonce (for counter-based nonce)
    // ============================================================================

    inline std::vector<uint8_t> encrypt_with_nonce(
        const std::vector<uint8_t>& plaintext,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& nonce,
        const std::vector<uint8_t>& aad = {}
    ) {
        if (key.size() != KEY_SIZE) {
            throw std::invalid_argument("key must be 32 bytes");
        }
        if (nonce.size() != NONCE_SIZE) {
            throw std::invalid_argument("nonce must be 12 bytes");
        }

        std::vector<uint8_t> packet(NONCE_SIZE + plaintext.size() + TAG_SIZE);

        uint8_t* nonce_out = packet.data();
        uint8_t* ciphertext = packet.data() + NONCE_SIZE;
        uint8_t* tag = packet.data() + NONCE_SIZE + plaintext.size();

        // Copy nonce to packet
        std::memcpy(nonce_out, nonce.data(), NONCE_SIZE);

        // Generate Poly1305 key (block 0)
        detail::ChaCha20 chacha;
        chacha.set_key(key.data());
        chacha.set_nonce(nonce.data(), 0);

        uint8_t poly_key[64];
        chacha.block(poly_key);

        // Encrypt (starting from block 1)
        chacha.set_nonce(nonce.data(), 1);
        chacha.encrypt(plaintext.data(), ciphertext, plaintext.size());

        // Compute tag
        detail::Poly1305 poly;
        poly.init(poly_key);

        // AAD with padding
        if (!aad.empty()) {
            poly.update(aad.data(), aad.size());
            if (aad.size() % 16 != 0) {
                uint8_t pad[16] = { 0 };
                poly.update(pad, 16 - (aad.size() % 16));
            }
        }

        // Ciphertext with padding
        poly.update(ciphertext, plaintext.size());
        if (plaintext.size() % 16 != 0) {
            uint8_t pad[16] = { 0 };
            poly.update(pad, 16 - (plaintext.size() % 16));
        }

        // Lengths (little-endian)
        uint8_t lengths[16];
        detail::store64_le(lengths, aad.size());
        detail::store64_le(lengths + 8, plaintext.size());
        poly.update(lengths, 16);

        poly.finish(tag);

        return packet;
    }

    // ============================================================================
    // KeyStore - manages password-encrypted stream keys
    // ============================================================================

    class KeyStore {
    public:
        KeyStore() = default;

        // Generate a new random key for a stream (resets nonce counter)
        std::vector<uint8_t> generate_stream_key(uint32_t stream_id) {
            std::vector<uint8_t> key = generate_key();
            stream_keys_[stream_id] = key;
            nonce_counters_[stream_id] = 0;
            return key;
        }

        // Set a stream key directly (resets nonce counter)
        void set_stream_key(uint32_t stream_id, const std::vector<uint8_t>& key) {
            if (key.size() != KEY_SIZE) {
                throw std::invalid_argument("key must be 32 bytes");
            }
            stream_keys_[stream_id] = key;
            nonce_counters_[stream_id] = 0;
        }

        // ========================================================================
        // Stream-based encrypt/decrypt (uses per-stream nonce counter)
        // ========================================================================

        // Encrypt with stream's key and counter-based nonce
        // Returns: [nonce (12)] [ciphertext (N)] [tag (16)]
        std::vector<uint8_t> encrypt_stream(
            uint32_t stream_id,
            const std::vector<uint8_t>& plaintext,
            const std::vector<uint8_t>& aad = {}
        ) {
            auto key_it = stream_keys_.find(stream_id);
            if (key_it == stream_keys_.end()) {
                throw std::runtime_error("stream key not found");
            }

            // Get and increment nonce counter
            uint64_t counter = nonce_counters_[stream_id]++;

            // Build 12-byte nonce: [stream_id:4][counter:8]
            std::vector<uint8_t> nonce(NONCE_SIZE);
            nonce[0] = static_cast<uint8_t>(stream_id);
            nonce[1] = static_cast<uint8_t>(stream_id >> 8);
            nonce[2] = static_cast<uint8_t>(stream_id >> 16);
            nonce[3] = static_cast<uint8_t>(stream_id >> 24);
            nonce[4] = static_cast<uint8_t>(counter);
            nonce[5] = static_cast<uint8_t>(counter >> 8);
            nonce[6] = static_cast<uint8_t>(counter >> 16);
            nonce[7] = static_cast<uint8_t>(counter >> 24);
            nonce[8] = static_cast<uint8_t>(counter >> 32);
            nonce[9] = static_cast<uint8_t>(counter >> 40);
            nonce[10] = static_cast<uint8_t>(counter >> 48);
            nonce[11] = static_cast<uint8_t>(counter >> 56);

            return encrypt_with_nonce(plaintext, key_it->second, nonce, aad);
        }

        // Decrypt using nonce from packet
        std::vector<uint8_t> decrypt_stream(
            uint32_t stream_id,
            const std::vector<uint8_t>& packet,
            const std::vector<uint8_t>& aad = {}
        ) {
            auto key_it = stream_keys_.find(stream_id);
            if (key_it == stream_keys_.end()) {
                throw std::runtime_error("stream key not found");
            }

            return decrypt(packet, key_it->second, aad);
        }

        // Get current nonce counter for a stream
        uint64_t get_nonce_counter(uint32_t stream_id) const {
            auto it = nonce_counters_.find(stream_id);
            if (it == nonce_counters_.end()) {
                return 0;
            }
            return it->second;
        }

        // Get a stream key (throws if not found)
        const std::vector<uint8_t>& get_stream_key(uint32_t stream_id) const {
            auto it = stream_keys_.find(stream_id);
            if (it == stream_keys_.end()) {
                throw std::runtime_error("stream key not found");
            }
            return it->second;
        }

        // Check if stream key exists
        bool has_stream_key(uint32_t stream_id) const {
            return stream_keys_.find(stream_id) != stream_keys_.end();
        }

        // Get all stream IDs
        std::vector<uint32_t> get_stream_ids() const {
            std::vector<uint32_t> ids;
            for (const auto& pair : stream_keys_) {
                ids.push_back(pair.first);
            }
            return ids;
        }

        // Number of streams
        size_t stream_count() const {
            return stream_keys_.size();
        }

        // Clear all keys and counters
        void clear() {
            for (auto& pair : stream_keys_) {
                secure_zero(pair.second.data(), pair.second.size());
            }
            stream_keys_.clear();
            nonce_counters_.clear();
        }

        // ========================================================================
        // File I/O (password-encrypted)
        // ========================================================================

        // Save to file encrypted with password
        // File format: [salt:16][encrypted_data][tag:16]
        // encrypted_data contains: [num_streams:4][stream_id:4][key:32]...
        bool save(const std::string& filepath, const std::string& password) {
            // Generate new salt
            std::vector<uint8_t> salt(SALT_SIZE);
            detail::random_bytes(salt.data(), SALT_SIZE);

            // Derive encryption key from password
            std::vector<uint8_t> derived_key(KEY_SIZE);
            detail::pbkdf2_sha256(
                reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                salt.data(), salt.size(),
                PBKDF2_ITERATIONS,
                derived_key.data(), KEY_SIZE
            );

            // Serialize keys
            std::vector<uint8_t> plaintext = serialize_keys();

            // Encrypt
            std::vector<uint8_t> encrypted = encrypt(plaintext, derived_key);

            // Write: salt + encrypted (which includes nonce + ciphertext + tag)
            std::ofstream file(filepath, std::ios::binary);
            if (!file) {
                return false;
            }

            file.write(reinterpret_cast<const char*>(salt.data()), SALT_SIZE);
            file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());

            secure_zero(derived_key.data(), derived_key.size());
            secure_zero(plaintext.data(), plaintext.size());

            return file.good();
        }

        // Load from file, decrypt with password
        bool load(const std::string& filepath, const std::string& password) {
            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            if (!file) {
                return false;
            }

            auto file_size = file.tellg();
            if (file_size < static_cast<std::streamoff>(SALT_SIZE + OVERHEAD)) {
                return false;
            }

            file.seekg(0);

            // Read salt
            std::vector<uint8_t> salt(SALT_SIZE);
            file.read(reinterpret_cast<char*>(salt.data()), SALT_SIZE);

            // Read encrypted data
            size_t encrypted_size = static_cast<size_t>(file_size) - SALT_SIZE;
            std::vector<uint8_t> encrypted(encrypted_size);
            file.read(reinterpret_cast<char*>(encrypted.data()), encrypted_size);

            if (!file) {
                return false;
            }

            // Derive key from password
            std::vector<uint8_t> derived_key(KEY_SIZE);
            detail::pbkdf2_sha256(
                reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                salt.data(), salt.size(),
                PBKDF2_ITERATIONS,
                derived_key.data(), KEY_SIZE
            );

            // Decrypt
            std::vector<uint8_t> plaintext;
            try {
                plaintext = decrypt(encrypted, derived_key);
            }
            catch (const std::runtime_error&) {
                secure_zero(derived_key.data(), derived_key.size());
                return false;  // Wrong password or corrupted file
            }

            secure_zero(derived_key.data(), derived_key.size());

            // Deserialize
            bool result = deserialize_keys(plaintext);
            secure_zero(plaintext.data(), plaintext.size());

            return result;
        }

        // ========================================================================
        // Key Exchange (for sending keys over a stream)
        // ========================================================================

        // Export specified stream keys encrypted with a transport key
        // Returns encrypted blob to send over the network
        std::vector<uint8_t> export_keys(
            const std::vector<uint32_t>& stream_ids,
            const std::vector<uint8_t>& transport_key
        ) const {
            std::vector<uint8_t> plaintext;

            // Write count
            uint32_t count = static_cast<uint32_t>(stream_ids.size());
            append_u32(plaintext, count);

            // Write each key
            for (uint32_t id : stream_ids) {
                auto it = stream_keys_.find(id);
                if (it == stream_keys_.end()) {
                    throw std::runtime_error("stream key not found for export");
                }
                append_u32(plaintext, id);
                plaintext.insert(plaintext.end(), it->second.begin(), it->second.end());
            }

            return encrypt(plaintext, transport_key);
        }

        // Import keys from encrypted blob received over network
        // Returns the stream IDs that were imported
        std::vector<uint32_t> import_keys(
            const std::vector<uint8_t>& encrypted_data,
            const std::vector<uint8_t>& transport_key
        ) {
            std::vector<uint8_t> plaintext = decrypt(encrypted_data, transport_key);

            if (plaintext.size() < 4) {
                throw std::runtime_error("invalid key data");
            }

            size_t offset = 0;
            uint32_t count = read_u32(plaintext, offset);

            std::vector<uint32_t> imported_ids;

            for (uint32_t i = 0; i < count; i++) {
                if (offset + 4 + KEY_SIZE > plaintext.size()) {
                    throw std::runtime_error("truncated key data");
                }

                uint32_t stream_id = read_u32(plaintext, offset);
                std::vector<uint8_t> key(plaintext.begin() + offset, plaintext.begin() + offset + KEY_SIZE);
                offset += KEY_SIZE;

                stream_keys_[stream_id] = std::move(key);
                nonce_counters_[stream_id] = 0;  // Reset counter for new key
                imported_ids.push_back(stream_id);
            }

            secure_zero(plaintext.data(), plaintext.size());
            return imported_ids;
        }

        // ========================================================================
        // Pending keys (for confirmation flow)
        // ========================================================================

        // Store keys as pending (not yet confirmed)
        void set_pending(const std::map<uint32_t, std::vector<uint8_t>>& keys) {
            pending_keys_ = keys;
        }

        // Get pending keys
        const std::map<uint32_t, std::vector<uint8_t>>& get_pending() const {
            return pending_keys_;
        }

        // Commit pending keys to active (resets nonce counters)
        void commit_pending() {
            for (auto& pair : pending_keys_) {
                stream_keys_[pair.first] = std::move(pair.second);
                nonce_counters_[pair.first] = 0;  // Reset counter for new key
            }
            pending_keys_.clear();
        }

        // Discard pending keys
        void discard_pending() {
            for (auto& pair : pending_keys_) {
                secure_zero(pair.second.data(), pair.second.size());
            }
            pending_keys_.clear();
        }

        // Check if there are pending keys
        bool has_pending() const {
            return !pending_keys_.empty();
        }

    private:
        std::map<uint32_t, std::vector<uint8_t>> stream_keys_;
        std::map<uint32_t, uint64_t> nonce_counters_;
        std::map<uint32_t, std::vector<uint8_t>> pending_keys_;

        static void secure_zero(uint8_t* data, size_t len) {
            volatile uint8_t* p = data;
            while (len--) {
                *p++ = 0;
            }
        }

        static void append_u32(std::vector<uint8_t>& v, uint32_t val) {
            v.push_back(static_cast<uint8_t>(val));
            v.push_back(static_cast<uint8_t>(val >> 8));
            v.push_back(static_cast<uint8_t>(val >> 16));
            v.push_back(static_cast<uint8_t>(val >> 24));
        }

        static void append_u64(std::vector<uint8_t>& v, uint64_t val) {
            v.push_back(static_cast<uint8_t>(val));
            v.push_back(static_cast<uint8_t>(val >> 8));
            v.push_back(static_cast<uint8_t>(val >> 16));
            v.push_back(static_cast<uint8_t>(val >> 24));
            v.push_back(static_cast<uint8_t>(val >> 32));
            v.push_back(static_cast<uint8_t>(val >> 40));
            v.push_back(static_cast<uint8_t>(val >> 48));
            v.push_back(static_cast<uint8_t>(val >> 56));
        }

        static uint32_t read_u32(const std::vector<uint8_t>& v, size_t& offset) {
            uint32_t val = static_cast<uint32_t>(v[offset])
                | (static_cast<uint32_t>(v[offset + 1]) << 8)
                | (static_cast<uint32_t>(v[offset + 2]) << 16)
                | (static_cast<uint32_t>(v[offset + 3]) << 24);
            offset += 4;
            return val;
        }

        static uint64_t read_u64(const std::vector<uint8_t>& v, size_t& offset) {
            uint64_t val = static_cast<uint64_t>(v[offset])
                | (static_cast<uint64_t>(v[offset + 1]) << 8)
                | (static_cast<uint64_t>(v[offset + 2]) << 16)
                | (static_cast<uint64_t>(v[offset + 3]) << 24)
                | (static_cast<uint64_t>(v[offset + 4]) << 32)
                | (static_cast<uint64_t>(v[offset + 5]) << 40)
                | (static_cast<uint64_t>(v[offset + 6]) << 48)
                | (static_cast<uint64_t>(v[offset + 7]) << 56);
            offset += 8;
            return val;
        }

        std::vector<uint8_t> serialize_keys() const {
            std::vector<uint8_t> data;

            uint32_t count = static_cast<uint32_t>(stream_keys_.size());
            append_u32(data, count);

            for (const auto& pair : stream_keys_) {
                append_u32(data, pair.first);
                data.insert(data.end(), pair.second.begin(), pair.second.end());

                // Include nonce counter
                uint64_t counter = 0;
                auto it = nonce_counters_.find(pair.first);
                if (it != nonce_counters_.end()) {
                    counter = it->second;
                }
                append_u64(data, counter);
            }

            return data;
        }

        bool deserialize_keys(const std::vector<uint8_t>& data) {
            if (data.size() < 4) {
                return false;
            }

            clear();

            size_t offset = 0;
            uint32_t count = read_u32(data, offset);

            for (uint32_t i = 0; i < count; i++) {
                if (offset + 4 + KEY_SIZE + 8 > data.size()) {
                    clear();
                    return false;
                }

                uint32_t stream_id = read_u32(data, offset);
                std::vector<uint8_t> key(data.begin() + offset, data.begin() + offset + KEY_SIZE);
                offset += KEY_SIZE;
                uint64_t counter = read_u64(data, offset);

                stream_keys_[stream_id] = std::move(key);
                nonce_counters_[stream_id] = counter;
            }

            return true;
        }
    };

    // ============================================================================
    // Message types for key exchange protocol
    // ============================================================================

    enum class KeyExchangeMessageType : uint8_t {
        NEW_KEYS = 0x01,  // Client -> Server: here are new keys
        KEYS_CONFIRMED = 0x02,  // Server -> Client: I saved the keys
    };

    // Build a key exchange message
    inline std::vector<uint8_t> build_key_exchange_message(
        KeyExchangeMessageType type,
        const std::vector<uint8_t>& payload = {}
    ) {
        std::vector<uint8_t> msg;
        msg.push_back(static_cast<uint8_t>(type));
        msg.insert(msg.end(), payload.begin(), payload.end());
        return msg;
    }

    // Parse a key exchange message
    inline KeyExchangeMessageType parse_key_exchange_message(
        const std::vector<uint8_t>& msg,
        std::vector<uint8_t>& payload
    ) {
        if (msg.empty()) {
            throw std::runtime_error("empty key exchange message");
        }

        KeyExchangeMessageType type = static_cast<KeyExchangeMessageType>(msg[0]);
        payload.assign(msg.begin() + 1, msg.end());
        return type;
    }

} // namespace crypto
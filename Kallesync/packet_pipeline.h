#pragma once

#include <atomic>
#include <array>
#include <chrono>
#include <winsock2.h>
#include <optional>

namespace PacketPipeline {

    using namespace std::chrono;

    // Configuration constants
    inline constexpr size_t MAX_PACKET_SIZE = 1413;
    inline constexpr size_t DEFAULT_RING_SIZE = 8192;  // Must be power of 2

    // Packet data structure
    struct Packet {
        std::array<char, MAX_PACKET_SIZE> data;
        size_t size;
        sockaddr_in addr;
        steady_clock::time_point timestamp;
        uint64_t sequence_num;
    };

    // Lock-free MPSC ring buffer (Multi-Producer, Single-Consumer)
    // Based on Dmitry Vyukov's bounded MPMC queue, simplified for single consumer
    template<size_t SIZE = DEFAULT_RING_SIZE>
    class RingBuffer {
        static_assert(SIZE && !(SIZE& (SIZE - 1)), "Ring buffer size must be power of 2");

    private:
        static constexpr size_t MASK = SIZE - 1;

        struct Slot {
            Packet packet;
            std::atomic<size_t> sequence;
        };

        alignas(64) std::atomic<size_t> write_pos{ 0 };
        alignas(64) std::atomic<size_t> read_pos{ 0 };
        alignas(64) std::array<Slot, SIZE> buffer;
        alignas(64) std::atomic<uint64_t> total_pushed{ 0 };
        alignas(64) std::atomic<uint64_t> total_dropped{ 0 };

    public:
        RingBuffer() {
            // Initialize sequence numbers - critical for correct operation
            for (size_t i = 0; i < SIZE; ++i) {
                buffer[i].sequence.store(i, std::memory_order_relaxed);
            }
        }

        // Producer methods (thread-safe for multiple producers)
        bool try_push(const void* data, size_t size, const sockaddr_in& addr) noexcept;

        // Consumer methods (single consumer only)
        bool try_pop(Packet& out) noexcept;
        size_t pop_batch(Packet* out, size_t max_count) noexcept;
        std::optional<Packet> peek() const noexcept;

        // Statistics
        size_t size() const noexcept;
        bool empty() const noexcept;
        uint64_t get_total_pushed() const noexcept;
        uint64_t get_total_dropped() const noexcept;
        void reset_stats() noexcept;
    };

    // Helper for IOCP integration
    class IOCPAdapter {
    private:
        void* target_buffer;
        std::atomic<bool> enabled{ true };

    public:
        explicit IOCPAdapter(void* buffer);

        void on_packet_received(const char* data, size_t bytes,
            const sockaddr_in& addr) noexcept;

        void pause() noexcept;
        void resume() noexcept;
        bool is_enabled() const noexcept;
    };

    // Consumer thread helper
    template<typename Callback>
    class Consumer {
    private:
        void* source;
        Callback callback;
        std::atomic<bool> running{ false };

    public:
        Consumer(void* buf, Callback cb);

        void consume_blocking();
        size_t consume_batch(size_t max_packets);
        void stop() noexcept;
    };

    // Explicit instantiations
    extern template class RingBuffer<8192>;
    extern template class RingBuffer<4096>;
    extern template class RingBuffer<2048>;

} // namespace PacketPipeline
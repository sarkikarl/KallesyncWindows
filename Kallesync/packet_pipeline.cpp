#include "packet_pipeline.h"
#include <cstring>
#include <intrin.h>  // For _mm_pause on MSVC
#include <functional>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PacketPipeline {

    // MPSC-safe try_push using CAS loop
    template<size_t SIZE>
    bool RingBuffer<SIZE>::try_push(const void* data, size_t size, const sockaddr_in& addr) noexcept {
        size_t pos = write_pos.load(std::memory_order_relaxed);

        for (;;) {
            Slot& slot = buffer[pos & MASK];
            size_t seq = slot.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot is available for writing - try to claim it
                if (write_pos.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                    // Successfully claimed the slot - write data
                    std::memcpy(slot.packet.data.data(), data, size);
                    slot.packet.size = size;
                    slot.packet.addr = addr;
                    slot.packet.timestamp = steady_clock::now();
                    slot.packet.sequence_num = total_pushed.fetch_add(1, std::memory_order_relaxed);

                    // Mark slot as written by advancing its sequence
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // CAS failed - another producer claimed it, loop will reload pos
            }
            else if (diff < 0) {
                // Buffer is full - consumer hasn't caught up
                total_dropped.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            else {
                // Another producer is ahead, reload write_pos
                pos = write_pos.load(std::memory_order_relaxed);
            }
        }
    }

    // Single-consumer try_pop
    template<size_t SIZE>
    bool RingBuffer<SIZE>::try_pop(Packet& out) noexcept {
        size_t pos = read_pos.load(std::memory_order_relaxed);
        Slot& slot = buffer[pos & MASK];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            // Slot has been written and is ready to read
            out = slot.packet;

            // Mark slot as available for producers (sequence = pos + SIZE)
            slot.sequence.store(pos + SIZE, std::memory_order_release);
            read_pos.store(pos + 1, std::memory_order_relaxed);
            return true;
        }

        // Slot not ready yet (either empty or producer still writing)
        return false;
    }

    template<size_t SIZE>
    size_t RingBuffer<SIZE>::pop_batch(Packet* out, size_t max_count) noexcept {
        size_t popped = 0;
        while (popped < max_count && try_pop(out[popped])) {
            ++popped;
        }
        return popped;
    }

    template<size_t SIZE>
    std::optional<Packet> RingBuffer<SIZE>::peek() const noexcept {
        size_t pos = read_pos.load(std::memory_order_relaxed);
        const Slot& slot = buffer[pos & MASK];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

        if (diff == 0) {
            return slot.packet;
        }
        return std::nullopt;
    }

    template<size_t SIZE>
    size_t RingBuffer<SIZE>::size() const noexcept {
        size_t w = write_pos.load(std::memory_order_acquire);
        size_t r = read_pos.load(std::memory_order_acquire);
        return (w - r) & MASK;
    }

    template<size_t SIZE>
    bool RingBuffer<SIZE>::empty() const noexcept {
        size_t pos = read_pos.load(std::memory_order_relaxed);
        const Slot& slot = buffer[pos & MASK];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        return static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1) != 0;
    }

    template<size_t SIZE>
    uint64_t RingBuffer<SIZE>::get_total_pushed() const noexcept {
        return total_pushed.load(std::memory_order_relaxed);
    }

    template<size_t SIZE>
    uint64_t RingBuffer<SIZE>::get_total_dropped() const noexcept {
        return total_dropped.load(std::memory_order_relaxed);
    }

    template<size_t SIZE>
    void RingBuffer<SIZE>::reset_stats() noexcept {
        total_dropped.store(0, std::memory_order_relaxed);
    }

    // IOCPAdapter implementation
    IOCPAdapter::IOCPAdapter(void* buffer) : target_buffer(buffer) {}

    void IOCPAdapter::on_packet_received(const char* data, size_t bytes,
        const sockaddr_in& addr) noexcept {
        if (enabled.load(std::memory_order_relaxed)) {
            static_cast<RingBuffer<>*>(target_buffer)->try_push(data, bytes, addr);
        }
    }

    void IOCPAdapter::pause() noexcept {
        enabled.store(false, std::memory_order_relaxed);
    }

    void IOCPAdapter::resume() noexcept {
        enabled.store(true, std::memory_order_relaxed);
    }

    bool IOCPAdapter::is_enabled() const noexcept {
        return enabled.load(std::memory_order_relaxed);
    }

    // Consumer implementation
    template<typename Callback>
    Consumer<Callback>::Consumer(void* buf, Callback cb) : source(buf), callback(cb) {}

    template<typename Callback>
    void Consumer<Callback>::consume_blocking() {
        running.store(true, std::memory_order_relaxed);
        Packet p;
        auto* ring = static_cast<RingBuffer<>*>(source);

        while (running.load(std::memory_order_relaxed)) {
            if (ring->try_pop(p)) {
                callback(p);
            }
            else {
                _mm_pause();
            }
        }
    }

    template<typename Callback>
    size_t Consumer<Callback>::consume_batch(size_t max_packets) {
        std::array<Packet, 64> batch;
        size_t to_process = (max_packets < 64) ? max_packets : 64;
        auto* ring = static_cast<RingBuffer<>*>(source);
        size_t got = ring->pop_batch(batch.data(), to_process);

        for (size_t i = 0; i < got; ++i) {
            callback(batch[i]);
        }

        return got;
    }

    template<typename Callback>
    void Consumer<Callback>::stop() noexcept {
        running.store(false, std::memory_order_relaxed);
    }

    // Explicit template instantiations
    template class RingBuffer<8192>;
    template class RingBuffer<4096>;
    template class RingBuffer<2048>;

    template class Consumer<void(*)(const Packet&)>;
    template class Consumer<std::function<void(const Packet&)>>;

} // namespace PacketPipeline
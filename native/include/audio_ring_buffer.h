#ifndef AUDIO_RING_BUFFER_H
#define AUDIO_RING_BUFFER_H

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <algorithm>

/**
 * AudioRingBuffer — Lock-free Single-Producer Single-Consumer (SPSC) circular buffer
 * for PCM audio data (16-bit signed samples).
 *
 * Design decisions:
 * - Power-of-two capacity for efficient modulo via bitmask (avoids expensive division)
 * - std::atomic<uint32_t> head/tail with memory_order_acquire/release for lock-free safety
 * - 64-byte cache line alignment between write_pos_ and read_pos_ to prevent false sharing
 * - Overflow policy: overwrite oldest samples by advancing the read pointer (never block producer)
 *
 * Thread safety:
 * - Exactly ONE producer thread may call write()
 * - Exactly ONE consumer thread may call read() / available()
 * - advance_read_on_overflow() is called by the producer only during overflow handling
 *
 * Validates: Requirements 3.4, 3.5, 14.1, 14.5, 14.6
 */
class AudioRingBuffer {
public:
    /**
     * Construct a ring buffer with the given power-of-two capacity.
     * @param capacity_power_of_two Must be a power of two (e.g., 1048576 = 2^20).
     *        If not a power of two, it will be rounded up to the next power of two.
     */
    explicit AudioRingBuffer(uint32_t capacity_power_of_two) {
        capacity_ = next_power_of_two(capacity_power_of_two);
        mask_ = capacity_ - 1;
        buffer_.reset(new int16_t[capacity_]);
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
        // Zero-initialize buffer
        std::memset(buffer_.get(), 0, capacity_ * sizeof(int16_t));
    }

    /**
     * Write samples into the buffer (producer side).
     * If writing would overflow, oldest samples are overwritten by advancing the read pointer.
     *
     * @param samples Pointer to source sample data
     * @param count   Number of samples to write
     * @return Number of samples actually written (always == count for overwrite policy)
     */
    uint32_t write(const int16_t* samples, uint32_t count) {
        if (count == 0 || samples == nullptr) {
            return 0;
        }

        const uint32_t original_count = count;

        // If count exceeds capacity, only write the last capacity_ samples
        if (count > capacity_) {
            samples += (count - capacity_);
            count = capacity_;
        }

        const uint32_t write_pos = write_pos_.load(std::memory_order_relaxed);
        const uint32_t read_pos = read_pos_.load(std::memory_order_acquire);

        // Check if write would overflow available space
        const uint32_t available_space = capacity_ - (write_pos - read_pos);
        if (count > available_space) {
            // Advance read pointer to make room (overwrite oldest samples)
            const uint32_t advance = count - available_space;
            advance_read_on_overflow(advance);
        }

        // Write samples into the buffer, handling wrap-around
        const uint32_t start_idx = write_pos & mask_;
        const uint32_t first_chunk = std::min(count, capacity_ - start_idx);

        std::memcpy(buffer_.get() + start_idx, samples, first_chunk * sizeof(int16_t));
        if (first_chunk < count) {
            // Wrap around to beginning of buffer
            std::memcpy(buffer_.get(), samples + first_chunk,
                        (count - first_chunk) * sizeof(int16_t));
        }

        // Publish new write position with release ordering
        write_pos_.store(write_pos + count, std::memory_order_release);

        return original_count;
    }

    /**
     * Read samples from the buffer (consumer side).
     * Reads up to 'count' samples, or fewer if not enough are available.
     *
     * @param dest  Pointer to destination buffer
     * @param count Maximum number of samples to read
     * @return Number of samples actually read
     */
    uint32_t read(int16_t* dest, uint32_t count) {
        if (count == 0 || dest == nullptr) {
            return 0;
        }

        const uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);
        const uint32_t write_pos = write_pos_.load(std::memory_order_acquire);

        // Only read what's available
        const uint32_t avail = write_pos - read_pos;
        const uint32_t to_read = std::min(count, avail);

        if (to_read == 0) {
            return 0;
        }

        // Read samples from the buffer, handling wrap-around
        const uint32_t start_idx = read_pos & mask_;
        const uint32_t first_chunk = std::min(to_read, capacity_ - start_idx);

        std::memcpy(dest, buffer_.get() + start_idx, first_chunk * sizeof(int16_t));
        if (first_chunk < to_read) {
            // Wrap around to beginning of buffer
            std::memcpy(dest + first_chunk, buffer_.get(),
                        (to_read - first_chunk) * sizeof(int16_t));
        }

        // Publish new read position with release ordering
        read_pos_.store(read_pos + to_read, std::memory_order_release);

        return to_read;
    }

    /**
     * Query the number of samples available for reading.
     * Can be called from the consumer thread.
     *
     * @return Number of unread samples currently in the buffer
     */
    uint32_t available() const {
        const uint32_t write_pos = write_pos_.load(std::memory_order_acquire);
        const uint32_t read_pos = read_pos_.load(std::memory_order_acquire);
        return write_pos - read_pos;
    }

    /**
     * Advance the read pointer to discard oldest samples (overflow handling).
     * Called by the producer thread when the buffer is full and new data must be written.
     *
     * @param count Number of samples to discard from the read end
     */
    void advance_read_on_overflow(uint32_t count) {
        const uint32_t read_pos = read_pos_.load(std::memory_order_relaxed);
        read_pos_.store(read_pos + count, std::memory_order_release);
    }

    /** @return The buffer capacity in samples */
    uint32_t capacity() const { return capacity_; }

private:
    /**
     * Round up to next power of two (no-op if already power of two).
     */
    static uint32_t next_power_of_two(uint32_t v) {
        if (v == 0) return 1;
        // If already a power of two, return as-is
        if ((v & (v - 1)) == 0) return v;
        // Round up
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        return v;
    }

    // Cache-line aligned write position (producer only modifies)
    alignas(64) std::atomic<uint32_t> write_pos_;

    // Cache-line aligned read position (consumer modifies; producer reads during overflow)
    alignas(64) std::atomic<uint32_t> read_pos_;

    // Sample storage
    std::unique_ptr<int16_t[]> buffer_;
    uint32_t capacity_;
    uint32_t mask_;
};

#endif // AUDIO_RING_BUFFER_H

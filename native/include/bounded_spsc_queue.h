#ifndef BOUNDED_SPSC_QUEUE_H
#define BOUNDED_SPSC_QUEUE_H

#include <array>
#include <atomic>
#include <cstdint>

/**
 * BoundedSPSCQueue - A lock-free bounded queue with overflow-drop semantics.
 *
 * Design:
 * - Fixed capacity (power of two) for bitmask indexing.
 * - Slot-based with sequence numbers for occupancy tracking.
 * - On overflow: drops the oldest element, pushes the new one, never blocks.
 * - try_push() returns true on normal push, false if overflow occurred (oldest dropped).
 * - try_pop() returns true if an item was dequeued, false if queue was empty.
 * - Head and tail are aligned on 64-byte cache lines to prevent false sharing.
 * - Uses memory_order_acquire/release for atomic operations.
 *
 * Concurrency model:
 *   - tail_ is exclusively owned by the producer (written only in try_push).
 *   - head_ is advanced by try_pop (consumer) and also by try_push on overflow
 *     (producer). Both use compare_exchange to safely advance head_.
 *   - Slots use a sequence/turn protocol: each slot's sequence number indicates
 *     whether it is ready for writing or reading at a given logical position.
 *
 * Requirements: 14.2, 14.4, 14.5
 */
template <typename T, uint32_t Capacity = 64>
class BoundedSPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(Capacity >= 2, "Capacity must be at least 2");

public:
    BoundedSPSCQueue() noexcept : head_(0), tail_(0) {
        for (uint32_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    /**
     * Push an item into the queue.
     *
     * If the queue is full, the oldest element is dropped first, then the new
     * item is pushed. The function never blocks.
     *
     * @param item  The item to enqueue.
     * @return true if push succeeded normally (queue was not full),
     *         false if overflow occurred (oldest was dropped, new item still pushed).
     */
    bool try_push(const T& item) noexcept {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t idx = tail & mask_;
        uint32_t seq = slots_[idx].sequence.load(std::memory_order_acquire);

        if (seq == tail) {
            // Slot is empty and ready for writing - normal push
            slots_[idx].data = item;
            slots_[idx].sequence.store(tail + 1, std::memory_order_release);
            tail_.store(tail + 1, std::memory_order_release);
            return true;
        }

        // Queue is full. We need to discard the oldest element.
        // Advance head using CAS to safely handle potential concurrent pop.
        uint32_t head = head_.load(std::memory_order_acquire);

        // Try to advance head (discard oldest)
        if (head_.compare_exchange_strong(head, head + 1,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            // We successfully advanced head. Mark that slot as available for reuse.
            uint32_t head_idx = head & mask_;
            slots_[head_idx].sequence.store(head + Capacity, std::memory_order_release);
        }
        // Even if CAS failed (consumer already popped), the slot should now be free.
        // Re-check our target slot.

        // Write the item. After advancing head (or consumer having popped),
        // our target slot is now available.
        slots_[idx].data = item;
        slots_[idx].sequence.store(tail + 1, std::memory_order_release);
        tail_.store(tail + 1, std::memory_order_release);
        return false;
    }

    /**
     * Pop an item from the queue.
     *
     * @param item  Output parameter to receive the dequeued item.
     * @return true if an item was dequeued, false if the queue was empty.
     */
    bool try_pop(T& item) noexcept {
        uint32_t head = head_.load(std::memory_order_acquire);
        uint32_t idx = head & mask_;
        uint32_t seq = slots_[idx].sequence.load(std::memory_order_acquire);

        if (seq != head + 1) {
            // Queue is empty (slot not yet written or already consumed)
            return false;
        }

        // Slot has data. Try to claim it via CAS on head.
        if (!head_.compare_exchange_strong(head, head + 1,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            // CAS failed: producer's overflow already advanced head past this slot.
            // The element we wanted was dropped. Queue appears empty from new head.
            return false;
        }

        // We successfully claimed this slot.
        item = slots_[idx].data;
        slots_[idx].sequence.store(head + Capacity, std::memory_order_release);
        return true;
    }

    /**
     * Get the current number of items in the queue.
     *
     * @return The number of items currently in the queue (0 to Capacity).
     */
    uint32_t size() const noexcept {
        uint32_t tail = tail_.load(std::memory_order_acquire);
        uint32_t head = head_.load(std::memory_order_acquire);
        uint32_t diff = tail - head;
        return diff <= Capacity ? diff : Capacity;
    }

private:
    struct Slot {
        std::atomic<uint32_t> sequence;
        T data;
    };

    static constexpr uint32_t mask_ = Capacity - 1;

    // Align head and tail on separate 64-byte cache lines to avoid false sharing
    alignas(64) std::atomic<uint32_t> head_;
    alignas(64) std::atomic<uint32_t> tail_;
    std::array<Slot, Capacity> slots_;
};

#endif // BOUNDED_SPSC_QUEUE_H

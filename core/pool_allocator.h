//
// Created by Dominic Kloecker on 22/03/2026.
//

#ifndef TRADING_POOL_ALLOCATOR_H
#define TRADING_POOL_ALLOCATOR_H

#include <cstddef>
#include <list>
#include <memory>
#include <vector>

namespace dsl {
/**
 * @brief Fixed-size pool allocator backed by aligned memory blocks.
 *
 * Pre-allocates blocks of memory divided into fixed-size chunks.
 * Blocks grow on demand and are never moved, so pointers into the pool
 * remain stable for the allocator's lifetime.
 *
 * @tparam ChunkSize       Bytes per chunk must be at least sizeof(void*)
 * @tparam ChunksPerBlock  Number of chunks per block
 * @tparam Alignment       Byte alignment of each chunk
 */
template<size_t ChunkSize, size_t ChunksPerBlock = 4, size_t Alignment = ChunkSize>
    requires (ChunkSize >= sizeof(void *))
             && (Alignment >= alignof(void *))
             && (ChunkSize % Alignment == 0)
class pool_resource : public std::pmr::memory_resource {
    static constexpr size_t BlockSize = ChunksPerBlock * ChunkSize;

    /**
     * A free chunk. Occupies `ChunkSize` bytes in the backing `Block`.
     * When free, the first bytes hold a pointer to the next free chunk.
     * When allocated, the caller owns those bytes.
     */
    struct Chunk {
        Chunk *next = nullptr;
    };

    /**
     * A contiguous block of memory divided into ChunksPerBlock chunks.
     * Aligned to Alignment so that every chunk start address satisfies
     * the same alignment guarantee.
     * Blocks form an intrusive linked list for ownership tracking.
     */
    struct alignas(Alignment) Block {
        std::array<std::byte, BlockSize> data;
        std::unique_ptr<Block>           next = nullptr;

        void *operator new(size_t size) {
            void *p = std::aligned_alloc(Alignment, size);
            if (!p) throw std::bad_alloc{};
            return p;
        }

        void operator delete(void *p) noexcept { std::free(p); }
    };

    /** Owns all allocated blocks. New blocks are prepended here. */
    std::unique_ptr<Block> block_head_ = nullptr;

    /** Head of the free list. The next allocation is served from here. */
    Chunk *free_ = nullptr;

    /**
     * Allocates a new block, links its chunks into the free list,
     * and prepends the block to the ownership chain.
     *
     * @throws std::bad_alloc on failed allocation of new Block
     */
    void allocate_new_block() {
        auto  block = std::make_unique<Block>();
        auto *head  = reinterpret_cast<Chunk *>(block->data.data());

        Chunk *tail = head;
        for (size_t c = 0; c < ChunksPerBlock - 1; c++) {
            tail->next = reinterpret_cast<Chunk *>(
                reinterpret_cast<std::byte *>(tail) + ChunkSize
            );
            tail = tail->next;
        }
        tail->next = nullptr;

        block->next = std::move(block_head_);
        block_head_ = std::move(block);
        free_       = head;
    }

    /**
     * Allocate a Chunk of Memory available for consumption.
     * @throws std::bad_alloc when failing to grow if required or incompatible size / alignment is requested.
     */
    [[nodiscard]] void *do_allocate(const std::size_t bytes, const std::size_t alignment) override {
        if (bytes > ChunkSize || Alignment % alignment != 0) throw std::bad_alloc{};
        if (!free_) allocate_new_block();

        Chunk *chunk = free_;
        free_        = free_->next;
        return chunk;
    }

    /**
     * Returns a previously allocated chunk to the free list.
     * The chunk becomes the new free list head and will be reused first.
     * Passing a pointer not obtained from this allocator is undefined behavior.
     */
    void do_deallocate(void *ptr, std::size_t, std::size_t) override {
        if (!ptr) return;
        auto *chunk = static_cast<Chunk *>(ptr);
        chunk->next = free_;
        free_       = chunk;
    }

    [[nodiscard]] bool do_is_equal(memory_resource const &other) const noexcept override {
        return this == &other;
    }
};
}

#endif //TRADING_POOL_ALLOCATOR_H

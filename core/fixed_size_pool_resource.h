#ifndef TRADING_POOL_ALLOCATOR_H
#define TRADING_POOL_ALLOCATOR_H

#include <cstddef>
#include <list>
#include <memory>

#include "util.h"

namespace dsl {
constexpr size_t align_up(const size_t size, const size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}


/**
 * @brief Fixed-size pool allocator backed by aligned memory blocks.
 *
 * Pre-allocates blocks of memory divided into fixed-size chunks.
 * Blocks grow on demand and are never moved, so pointers into the pool
 * remain stable for the allocator's lifetime.
 *
 * @tparam ChunkSize      Minimum required size per chunk
 * @tparam ChunksPerBlock Number of chunks to allocate within a single block
 * @tparam Alignment      Desired Alignment of allocated chunks
 *
 * The final chunk size may be larger than the requested, depending on the alignment
 * and size. But all chunks are guaranteed to be Alignment compatible with the
 * requested Alignment.
 */
template<size_t ChunkSize, size_t ChunksPerBlock, size_t Alignment = alignof(std::max_align_t)>
    requires (PowerOfTwo<Alignment> && ChunksPerBlock > 0)
class fixed_size_pool_resource_bench : public std::pmr::memory_resource {
public:
    struct pool_options {
        static constexpr size_t chunk_size       = align_up(std::max(ChunkSize, sizeof(void *)), Alignment);
        static constexpr size_t chunks_per_block = ChunksPerBlock;
        static constexpr size_t alignment        = Alignment;
    };

    static constexpr pool_options options;

private:
    static constexpr size_t block_size = options.chunks_per_block * options.chunk_size;

    /**
     * A free chunk. Occupies `ChunkSize` bytes in the backing `Block`.
     * When free, the first bytes hold a pointer to the next free chunk.
     * When allocated, the caller owns those bytes.
     */
    struct Chunk {
        Chunk *next = nullptr;
    };

    /**
     * A contiguous block of memory divided into a fixed number of chunks.
     * Aligned to Alignment so that every chunk start address satisfies
     * the same alignment guarantee.
     * Blocks form an intrusive linked list for ownership tracking.
     */
    struct Block {
        std::unique_ptr<Block>                               next = nullptr;
        alignas(Alignment) std::array<std::byte, block_size> data;

        void *operator new(const size_t size) {
            void *p = std::aligned_alloc(alignof(Block), sizeof(Block));
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
        auto   block = std::unique_ptr<Block>(new Block());
        auto * head  = reinterpret_cast<Chunk *>(block->data.data());
        Chunk *tail  = head;

        for (size_t c = 0; c < ChunksPerBlock - 1; c++) {
            tail->next = reinterpret_cast<Chunk *>(
                reinterpret_cast<std::byte *>(tail) + options.chunk_size);
            tail = tail->next;
        }
        tail->next = nullptr;

        block->next = std::move(block_head_);
        block_head_ = std::move(block);
        free_       = head;
    }

    /**
     * Allocate a Chunk of Memory available for consumption.
     * Will ignore any provided alignment, as all chunks will be aligned to the templated
     * `Alignment` parameter.
     * @throws std::bad_alloc when failing to grow if required or incompatible size.
     */
    [[nodiscard]] void *do_allocate(const std::size_t bytes, std::size_t) override {
        if (bytes > options.chunk_size) throw std::bad_alloc{};
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

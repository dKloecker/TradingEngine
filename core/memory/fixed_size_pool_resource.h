#ifndef TRADING_POOL_ALLOCATOR_H
#define TRADING_POOL_ALLOCATOR_H

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>

#include "core/util/util.h"

namespace dsl {
/**
 * @brief Fixed-size pool allocator backed by over-allocated aligned memory blocks.
 *
 * Pre-allocates blocks of memory divided into fixed-size chunks.
 * Blocks grow on demand and are never moved, so pointers into the pool
 * remain stable for the allocator's lifetime.
 *
 * Configured through construction via @c pool_options struct
 * The final chunk size may be larger than requested, depending on the alignment.
 * But all chunks are guaranteed to be compatible with the requested alignment.
 */
class fixed_size_pool_resource : public std::pmr::memory_resource {
public:
    struct pool_options {
        size_t chunk_size;
        size_t chunks_per_block;
        size_t alignment = alignof(std::max_align_t);
    };

    /**
     * @param opts Options for the desired pool configuration
     * @throws std::invalid_argument if invalid chunks_per_block
     * @throws std::invalid_argument if invalid alignment provided
     */
    explicit fixed_size_pool_resource(const pool_options &opts)
        : options_{
            .chunk_size = align_up(std::max(opts.chunk_size, sizeof(void *)),
                                   opts.alignment),
            .chunks_per_block = opts.chunks_per_block,
            .alignment        = opts.alignment
        } {
        if (opts.chunks_per_block == 0)
            throw std::invalid_argument("chunks_per_block must be greater than zero");
        if (opts.alignment == 0)
            throw std::invalid_argument("alignment must be greater than zero");
        allocate_new_block();
    }

    fixed_size_pool_resource(fixed_size_pool_resource &&) = default;

    fixed_size_pool_resource(const fixed_size_pool_resource &) = delete;

    fixed_size_pool_resource &operator=(const fixed_size_pool_resource &) = delete;

private:
    /** Options Provided to Pool Resource */
    const pool_options options_;

    /**
     * A free chunk. Occupies `chunk_size` bytes in the backing Block.
     * When free, the first bytes hold a pointer to the next free chunk.
     * When allocated, the caller owns those bytes.
     */
    struct Chunk {
        Chunk *next = nullptr;
    };

    /**
     * Total bytes needed for a single over-allocated Block:
     * the Block header (rounded up to alignment) plus all chunk data.
     */
    [[nodiscard]] size_t total_block_size() const {
        const size_t header = align_up(sizeof(Block), options_.alignment);
        return header + options_.chunks_per_block * options_.chunk_size;
    }

    // Forward declaration required for BlockDeleter
    struct Block;
    /**
     * Custom deleter for over-allocated Blocks.
     * Required because the allocation size exceeds @c sizeof(Block),
     * Explicitly invoke the destructor to clean up the owned next pointer
     * before returning the raw memory to @c std::free.
     */
    struct BlockDeleter {
        void operator()(Block *b) const noexcept {
            b->~Block();
            std::free(b);
        }
    };

    /**
     * A contiguous block of memory divided into a fixed number of chunks.
     * Blocks are over-allocated with data living in trailing bytes after
     * the block header.
     * Blocks form a linked list via @c next for ownership tracking.
     * @code
     *  <----------------- total_block_size() ------------------>
     *  <-- header (aligned) ---> <----- chunk data region ----->
     * [ Block struct | padding ] [ chunk 0 | chunk 1 |   ...   ]
     * @endcode
     */
    struct Block {
        // TODO: Maybe keep track of alignment in here?
        using unique_block_ptr = std::unique_ptr<Block, BlockDeleter>;
        unique_block_ptr next  = {};

        /**
         * Returns a pointer to the start of the aligned chunk data region.
         */
        std::byte *data(const size_t alignment) {
            // Address past header
            const auto addr = reinterpret_cast<uintptr_t>(this) + sizeof(Block);
            // Address past aligned header (including padding)
            return reinterpret_cast<std::byte *>(align_up(addr, alignment));
        }

        /**
         * Placement operator new. Allocates `total_size` bytes (header and chunk data)
         * @param _ ignored but required to compile
         * @param total_size actual size allocated
         * @throws std::bad_alloc on allocation failure
         */
        [[nodiscard]] void *operator new(const size_t _, const size_t total_size) {
            void *p = std::aligned_alloc(alignof(Block), total_size);
            if (!p) throw std::bad_alloc{};
            return p;
        }

        /** Matching placement delete, called only if the Block constructor throws. */
        void operator delete(void *p, size_t) noexcept {
            std::free(p);
        }

        /** Normally delete. Required by the language but should not be called directly. */
        void operator delete(void *p) noexcept {
            std::free(p);
        }

        /**
         * Utility to create @c unique_ptr, since make_unique cannot be used due to overallocation
         */
        [[nodiscard]] static unique_block_ptr make_unique(const size_t total_size) {
            auto *b = new(total_size) Block();
            return unique_block_ptr(b);
        }
    };

    /** Owns all allocated blocks. New blocks are prepended here. */
    Block::unique_block_ptr block_head_ = nullptr;

    /** Head of the free list. The next allocation is served from here. */
    Chunk *free_ = nullptr;

    /**
     * Allocates a new block, links its chunks into the free list,
     * and prepends the block to the ownership chain.
     *
     * @throws std::bad_alloc on failed allocation of new Block
     */
    void allocate_new_block() {
        auto   block = Block::make_unique(total_block_size());
        auto * head  = reinterpret_cast<Chunk *>(block->data(options_.alignment));
        Chunk *tail  = head;

        for (size_t c = 0; c < options_.chunks_per_block - 1; c++) {
            tail->next = reinterpret_cast<Chunk *>(
                reinterpret_cast<std::byte *>(tail) + options_.chunk_size);
            tail = tail->next;
        }
        tail->next = nullptr;

        block->next = std::move(block_head_);
        block_head_ = std::move(block);
        free_       = head;
    }

    /**
     * Allocate a chunk of memory available for consumption.
     * Will ignore any provided alignment, as all chunks are aligned to
     * the configured alignment.
     * @throws std::bad_alloc when failing to grow if required or incompatible size.
     */
    [[nodiscard]] void *do_allocate(const size_t bytes, size_t) override {
        if (bytes > options_.chunk_size) throw std::bad_alloc{};
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
    void do_deallocate(void *ptr, size_t, size_t) override {
        if (!ptr) return;
        auto *chunk = static_cast<Chunk *>(ptr);
        chunk->next = free_;
        free_       = chunk;
    }

    [[nodiscard]] bool do_is_equal(const memory_resource &other) const noexcept override {
        return this == &other;
    }
};

/**
 * @brief Fixed-size pool allocator with compile time configuration.
 *
 * Thin wrapper over pool_resource_base that feeds template constants as runtime options.
 *
 * @tparam ChunkSize      Minimum required size per chunk
 * @tparam ChunksPerBlock  Number of chunks to allocate within a single block
 * @tparam Alignment       Desired alignment of allocated chunks
 *
 * The final chunk size may be larger than requested, depending on the alignment
 * and size. But all chunks are guaranteed to be compatible with the
 * requested alignment.
 */
template<size_t ChunkSize, size_t ChunksPerBlock, size_t Alignment = alignof(std::max_align_t)>
    requires (PowerOfTwo<Alignment> && ChunksPerBlock > 0)
class static_fixed_size_pool_resource : public fixed_size_pool_resource {
    static constexpr auto make_options() {
        return pool_options{
            .chunk_size       = align_up(std::max(ChunkSize, sizeof(void *)), Alignment),
            .chunks_per_block = ChunksPerBlock,
            .alignment        = Alignment
        };
    }

public:
    static constexpr pool_options options = make_options();

    static_fixed_size_pool_resource() : fixed_size_pool_resource(options) {}
};
}

#endif //TRADING_POOL_ALLOCATOR_H

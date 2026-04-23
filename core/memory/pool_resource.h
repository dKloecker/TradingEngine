//
// Created by Dominic Kloecker on 18/04/2026.
//

#ifndef TRADING_POOL_RESOURCE_H
#define TRADING_POOL_RESOURCE_H

#include <algorithm>
#include <memory_resource> // Required for std::pmr::memory_resource
#include <vector>

#include "core/memory/fixed_size_pool_resource.h"

namespace dsl {
/**
 * @brief Multi Size separated pool allocator, dispatching to fixed size pools
 *
 * Maintains a collection of @c fixed_size_pool_resource instances, each
 * configured for a distinct chunk size.
 * Sized grow from @c min_chunk_size up to @c largest_required_chunk.
 * Allocation requests are routed to the smallest pool whose chunk size fits the request.
 * Requests exceeding the @c largest_required_chunk are forwarded to the upstream resource.
 *
 * Configured through construction via @c pool_options struct.
 * All pools are built with alignment @c alignof(std::max_align_t)
 * Pointers returned by this allocator remain stable for its lifetime.
 */
class pool_resource : public std::pmr::memory_resource {
    static constexpr size_t min_n_chunks      = 1;
    static constexpr size_t max_n_chunks      = 1 << 8;
    static constexpr size_t min_chunk_size    = sizeof(void *);
    static constexpr size_t max_chunk_size    = 1 << 12;
    static constexpr size_t chunk_size_growth = 2;
    static constexpr size_t chunk_n_growth    = 2;
    static constexpr size_t default_alignment = alignof(std::max_align_t);

public:
    struct pool_options {
        size_t max_chunks_per_block;
        size_t largest_required_chunk;
    };

    /**
     * @param opts Options for the desired pool configuration
     * @param upstream Upstream resource used for requests exceeding
     *        @c largest_required_chunk
     */
    explicit pool_resource(const pool_options &opts, std::pmr::memory_resource *upstream)
        : options_{
              .max_chunks_per_block = std::clamp(
                  opts.max_chunks_per_block,
                  min_n_chunks,
                  max_n_chunks
              ),
              .largest_required_chunk = std::clamp(
                  opts.largest_required_chunk,
                  min_chunk_size,
                  max_chunk_size
              )
          },
          pools_(create_pools(options_)),
          upstream_(upstream) {}

    /**
     * @param opts Options for the desired pool configuration
     *
     * Uses @c std::pmr::get_default_resource() as the upstream resource.
     */
    explicit pool_resource(const pool_options &opts)
        : options_{
              .max_chunks_per_block = std::clamp(
                  opts.max_chunks_per_block,
                  min_n_chunks,
                  max_n_chunks
              ),
              .largest_required_chunk = std::clamp(
                  opts.largest_required_chunk,
                  min_chunk_size,
                  max_chunk_size
              )
          },
          pools_(create_pools(options_)),
          upstream_(std::pmr::get_default_resource()) {}

    pool_resource(const pool_resource &) = delete;

    pool_resource &operator=(const pool_resource &) = delete;

private:
    /** Options provided to the pool resource */
    const pool_options options_;

    /** Backing fixed-size pools ordered by increasing chunk size */
    std::vector<fixed_size_pool_resource> pools_;

    /** Upstream resource for oversized requests */
    std::pmr::memory_resource *upstream_;

    [[nodiscard]] size_t number_of_pools() const { return pools_.size(); }

    /**
     * Selects the resource responsible for a request of the given size.
     * Requests larger than @c largest_required_chunk are forwarded to
     * the upstream resource; all others are routed to the pool
     * identified by @c pool_index.
     */
    std::pmr::memory_resource *route(const size_t bytes) {
        if (bytes > options_.largest_required_chunk) {
            return upstream_;
        } else {
            return &pools_[pool_index(bytes)];
        }
    }

    /**
     * Allocate a chunk of memory available for consumption.
     * Dispatches to the smallest pool whose chunk size fits @c bytes,
     * or to the upstream resource if no pool is large enough.
     */
    [[nodiscard]] void *do_allocate(const size_t bytes, const size_t alignment) override {
        return route(bytes)->allocate(bytes, alignment);
    }

    /**
     * Returns a previously allocated chunk to the resource that produced it.
     * The caller must pass the same @c bytes value used at allocation so
     * the request is routed back to the correct pool.
     * Passing a pointer not obtained from this allocator is undefined behavior.
     */
    void do_deallocate(void *p, const size_t bytes, const size_t alignment) override {
        return route(bytes)->deallocate(p, bytes, alignment);
    }

    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
        return this == &other;
    }

    /**
     * @param size desired size
     * @return index of pool for requested size
     */
    static constexpr size_t pool_index(const size_t size) {
        // TODO: Improve this, probably cache the indexes
        if (size <= min_chunk_size) {
            return 0;
        }
        size_t res        = 0;
        size_t chunk_size = min_chunk_size;

        // clamp to maximum allowed
        const size_t target_size = std::min(size, max_chunk_size);
        while (chunk_size < target_size) {
            chunk_size *= chunk_size_growth;
            res++;
        }
        return res;
    }

    /**
     * Builds the collection of fixed-size pools covering chunk sizes from
     * @c min_chunk_size up to @c largest_required_chunk, growing
     * geometrically by @c chunk_size_growth. The number of chunks per
     * block shrinks by @c chunk_n_growth at each step, bounded below by
     * @c min_n_chunks, so the per-block memory footprint stays balanced as the
     * chunk size grows. All pools are constructed with alignment
     * @c default_alignment.
     */
    [[nodiscard]] static std::vector<fixed_size_pool_resource> create_pools(const pool_options &opts) {
        const size_t max_size   = opts.largest_required_chunk;
        size_t       chunk_size = min_chunk_size;
        size_t       num_chunks = opts.max_chunks_per_block;

        std::vector<fixed_size_pool_resource> pools;

        while (chunk_size <= max_size) {
            pools.push_back(fixed_size_pool_resource{
                {
                    .chunk_size       = chunk_size,
                    .chunks_per_block = num_chunks,
                    .alignment        = default_alignment
                }
            });
            // Keep number of chunks above min always
            num_chunks = std::max(min_n_chunks, num_chunks / chunk_n_growth);
            chunk_size *= chunk_size_growth;
        }

        return pools;
    }
};
} // namespace dsl

#endif //TRADING_POOL_RESOURCE_H

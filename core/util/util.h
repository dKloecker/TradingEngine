//
// Created by Dominic Kloecker on 31/03/2026.
//

#ifndef TRADING_UTIL_H
#define TRADING_UTIL_H
#include <cstddef>

namespace dsl {
template<size_t Value>
concept PowerOfTwo = (Value > 0) && ((Value & (Value - 1)) == 0);

constexpr size_t align_up(const size_t size, const size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}


constexpr size_t round_up_pow2(size_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}
}

#endif //TRADING_UTIL_H

//
// Created by Dominic Kloecker on 31/03/2026.
//

#ifndef TRADING_UTIL_H
#define TRADING_UTIL_H
#include <cstddef>

template<size_t Value>
concept PowerOfTwo = (Value > 0) && ((Value & (Value - 1)) == 0);


#endif //TRADING_UTIL_H

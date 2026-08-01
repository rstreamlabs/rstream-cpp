// See LICENSE file in the project root for license information.

#pragma once

#include <chrono>
#include <limits>

#ifndef RSTREAM_TEST_TIMEOUT_SCALE
#define RSTREAM_TEST_TIMEOUT_SCALE 1
#endif

static_assert(RSTREAM_TEST_TIMEOUT_SCALE > 0);

namespace rstream::test {

template <class Rep, class Period>
constexpr auto timeout(std::chrono::duration<Rep, Period> duration)
{
  return duration * RSTREAM_TEST_TIMEOUT_SCALE;
}

constexpr unsigned int timeout_ms(unsigned int milliseconds)
{
  constexpr auto scale = static_cast<unsigned int>(RSTREAM_TEST_TIMEOUT_SCALE);
  return milliseconds <= std::numeric_limits<unsigned int>::max() / scale
             ? milliseconds * scale
             : std::numeric_limits<unsigned int>::max();
}

}  // namespace rstream::test

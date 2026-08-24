#ifndef WATERLINKEDSONAR_SPAN_HPP
#define WATERLINKEDSONAR_SPAN_HPP

#include <waterlinkedsonar/detail/tcb_span.hpp>

namespace waterlinked::sonar {

/**
 * @brief Non-owning view over a contiguous sequence, API-compatible with
 * C++20 std::span.
 *
 * Backed by the vendored tcb::span (detail/tcb_span.hpp) unconditionally: an
 * alias that switched to std::span by language standard would give the library
 * and its consumers different types when compiled under different standards.
 */
template <typename T>
using Span = tcb::span<T>;

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_SPAN_HPP

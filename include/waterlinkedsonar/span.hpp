/// \file
/// Non-owning view over a contiguous sequence.

#ifndef WATERLINKEDSONAR_SPAN_HPP
#define WATERLINKEDSONAR_SPAN_HPP

#include <waterlinkedsonar/detail/tcb_span.hpp>

namespace waterlinked::sonar {

/// Non-owning view over a contiguous sequence, with the interface of C++20
/// std::span.
///
/// It is the vendored tcb::span under every language standard, so code built
/// as C++17 and as C++20 agrees on the type.
template <typename T>
using Span = tcb::span<T>;

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_SPAN_HPP

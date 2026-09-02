/// \file
/// Comparison of firmware release numbers.

#ifndef WATERLINKEDSONAR_HTTP_SEMVER_HPP
#define WATERLINKEDSONAR_HTTP_SEMVER_HPP

#include <string_view>

namespace waterlinked::sonar::detail {

/// Whether release \p a precedes release \p b, both "major.minor.patch".
///
/// \throws std::invalid_argument unless both are three dot-separated
///   non-negative integers.
bool semver_less(std::string_view a, std::string_view b);

}  // namespace waterlinked::sonar::detail

#endif  // WATERLINKEDSONAR_HTTP_SEMVER_HPP

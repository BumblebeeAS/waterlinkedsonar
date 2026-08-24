#ifndef WATERLINKEDSONAR_SEMVER_HPP
#define WATERLINKEDSONAR_SEMVER_HPP

#include <string>

namespace waterlinked::sonar::internal {

// Compares three-component numeric versions ("1.7.0"). Throws
// std::invalid_argument on anything else.
bool semver_is_less_than(const std::string& a, const std::string& b);

}  // namespace waterlinked::sonar::internal

#endif  // WATERLINKEDSONAR_SEMVER_HPP

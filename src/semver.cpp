#include "semver.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>

namespace waterlinked::sonar::internal {

namespace {

std::array<long, 3> parse(const std::string& v) {
  std::array<long, 3> parts{};
  std::size_t pos = 0;
  for (int i = 0; i < 3; ++i) {
    if (pos >= v.size()) {
      throw std::invalid_argument("invalid semver string: '" + v + "'");
    }
    std::size_t consumed = 0;
    try {
      parts[i] = std::stol(v.substr(pos), &consumed);
    } catch (const std::exception&) {
      throw std::invalid_argument("invalid semver string: '" + v + "'");
    }
    if (consumed == 0 || parts[i] < 0) {
      throw std::invalid_argument("invalid semver string: '" + v + "'");
    }
    pos += consumed;
    if (i < 2) {
      if (pos >= v.size() || v[pos] != '.') {
        throw std::invalid_argument("invalid semver string: '" + v + "'");
      }
      ++pos;
    }
  }
  if (pos != v.size()) {
    throw std::invalid_argument("invalid semver string: '" + v + "'");
  }
  return parts;
}

}  // namespace

bool semver_is_less_than(const std::string& a, const std::string& b) {
  return parse(a) < parse(b);
}

}  // namespace waterlinked::sonar::internal

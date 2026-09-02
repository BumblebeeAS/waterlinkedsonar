#include "http/semver.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <string>

namespace waterlinked::sonar::detail {

namespace {

std::array<unsigned long, 3> parse(std::string_view version) {
  const auto invalid = [&] {
    return std::invalid_argument("invalid release number \"" +
                                 std::string(version) + "\"");
  };
  std::array<unsigned long, 3> parts{};
  const char* pos = version.data();
  const char* const end = pos + version.size();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      if (pos == end || *pos != '.') {
        throw invalid();
      }
      ++pos;
    }
    const auto [next, error] = std::from_chars(pos, end, parts[i]);
    if (error != std::errc()) {
      throw invalid();
    }
    pos = next;
  }
  if (pos != end) {
    throw invalid();
  }
  return parts;
}

}  // namespace

bool semver_less(std::string_view a, std::string_view b) {
  return parse(a) < parse(b);
}

}  // namespace waterlinked::sonar::detail

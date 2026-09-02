#include "http/semver.hpp"

#include <gtest/gtest.h>

#include <stdexcept>

namespace waterlinked::sonar::detail {
namespace {

TEST(Semver, ComparesNumerically) {
  EXPECT_TRUE(semver_less("1.2.3", "1.2.4"));
  EXPECT_TRUE(semver_less("1.2.9", "1.10.0"));
  EXPECT_TRUE(semver_less("1.9.9", "2.0.0"));
  EXPECT_FALSE(semver_less("1.2.3", "1.2.3"));
  EXPECT_FALSE(semver_less("1.10.0", "1.2.9"));
}

TEST(Semver, RejectsMalformedVersions) {
  for (const char* version :
       {"1.2", "1.2.3.4", "1.2.x", "", "-1.2.3", "+1.2.3", " 1.2.3", "1..3"}) {
    EXPECT_THROW(semver_less(version, "1.2.3"), std::invalid_argument)
        << version;
  }
}

}  // namespace
}  // namespace waterlinked::sonar::detail

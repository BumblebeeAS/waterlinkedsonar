#include <gtest/gtest.h>

#include <stdexcept>

#include "semver.hpp"

namespace waterlinked::sonar::internal {

TEST(Semver, LessThan) {
  EXPECT_TRUE(semver_is_less_than("1.2.3", "1.2.4"));
  EXPECT_TRUE(semver_is_less_than("1.2.3", "1.3.0"));
  EXPECT_TRUE(semver_is_less_than("1.2.3", "2.0.0"));
  EXPECT_TRUE(semver_is_less_than("1.2.9", "1.10.0"));
}

TEST(Semver, Equal) { EXPECT_FALSE(semver_is_less_than("1.2.3", "1.2.3")); }

TEST(Semver, GreaterThan) {
  EXPECT_FALSE(semver_is_less_than("1.2.4", "1.2.3"));
  EXPECT_FALSE(semver_is_less_than("1.3.0", "1.2.9"));
  EXPECT_FALSE(semver_is_less_than("2.0.0", "1.9.9"));
}

TEST(Semver, InvalidInputThrows) {
  EXPECT_THROW(semver_is_less_than("1.2", "1.2.3"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("1.2.3.4", "1.2.3"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("abc", "1.2.3"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("1.2.3", "xyz"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("", "1.2.3"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("1.2.3", ""), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("1..3", "1.2.3"), std::invalid_argument);
  EXPECT_THROW(semver_is_less_than("1.2.x", "1.2.3"), std::invalid_argument);
}

}  // namespace waterlinked::sonar::internal

/// \file
/// Reader for .sonar recordings, which are concatenated RIP packets.

#ifndef WATERLINKEDSONAR_TEST_SUPPORT_RECORDING_HPP
#define WATERLINKEDSONAR_TEST_SUPPORT_RECORDING_HPP

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace waterlinked::sonar::test {

/// Recording of a real sonar: six range images and six signal-strength
/// images, sequence IDs 4448 to 4453.
inline const std::string SHIP_RECORDING =
    std::string(WATERLINKEDSONAR_TEST_DATA_DIR) + "/ship_short.sonar";

inline std::vector<std::vector<std::uint8_t>> read_recording(
    const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  EXPECT_TRUE(file.good()) << "cannot open " << path;
  std::vector<std::vector<std::uint8_t>> packets;
  std::array<std::uint8_t, 8> header{};
  while (file.read(reinterpret_cast<char*>(header.data()), header.size())) {
    std::uint32_t length = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      length |= static_cast<std::uint32_t>(header[4 + i]) << (8 * i);
    }
    std::vector<std::uint8_t> packet(header.begin(), header.end());
    packet.resize(length);
    file.read(reinterpret_cast<char*>(packet.data()) + header.size(),
              static_cast<std::streamsize>(length - header.size()));
    EXPECT_TRUE(file.good()) << "truncated packet in " << path;
    packets.push_back(std::move(packet));
  }
  return packets;
}

}  // namespace waterlinked::sonar::test

#endif  // WATERLINKEDSONAR_TEST_SUPPORT_RECORDING_HPP

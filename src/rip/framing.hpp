/// \file
/// Range Image Protocol framing, used by rip::Decoder and the test encoder.

#ifndef WATERLINKEDSONAR_RIP_FRAMING_HPP
#define WATERLINKEDSONAR_RIP_FRAMING_HPP

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <waterlinkedsonar/span.hpp>

#include "WaterLinkedSonarIntegrationProtocol.pb.h"

namespace waterlinked::sonar::rip {

namespace proto = waterlinked::sonar::protocol;

inline constexpr std::size_t MAGIC_SIZE = 4;
inline constexpr std::size_t LENGTH_SIZE = 4;
inline constexpr std::size_t CRC_SIZE = 4;
inline constexpr std::size_t HEADER_SIZE = MAGIC_SIZE + LENGTH_SIZE;
inline constexpr std::size_t MIN_PACKET_SIZE = HEADER_SIZE + CRC_SIZE;

inline std::uint32_t read_le32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

inline void write_le32(std::uint32_t value, std::uint8_t* bytes) {
  bytes[0] = static_cast<std::uint8_t>(value);
  bytes[1] = static_cast<std::uint8_t>(value >> 8U);
  bytes[2] = static_cast<std::uint8_t>(value >> 16U);
  bytes[3] = static_cast<std::uint8_t>(value >> 24U);
}

inline std::uint32_t crc32(Span<const std::uint8_t> bytes) {
  return static_cast<std::uint32_t>(
      ::crc32(0UL, bytes.data(), static_cast<uInt>(bytes.size())));
}

}  // namespace waterlinked::sonar::rip

#endif  // WATERLINKEDSONAR_RIP_FRAMING_HPP

/// \file
/// Range Image Protocol encoder for building test packets.

#ifndef WATERLINKEDSONAR_TEST_SUPPORT_RIP_ENCODER_HPP
#define WATERLINKEDSONAR_TEST_SUPPORT_RIP_ENCODER_HPP

#include <google/protobuf/message.h>
#include <snappy.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "rip/framing.hpp"

namespace waterlinked::sonar::rip {

/// Protocol version of an encoded packet.
enum class Version : std::uint8_t { RIP1, RIP2 };

/// Wraps \p message in a Packet and frames it as a RIP packet.
inline std::vector<std::uint8_t> encode(
    const google::protobuf::Message& message, Version version = Version::RIP2) {
  proto::Packet packet;
  // PackFrom returns void in protobuf 3.12 and [[nodiscard]] bool later.
  static_cast<void>(packet.mutable_msg()->PackFrom(message));
  std::string payload = packet.SerializeAsString();
  if (version == Version::RIP2) {
    std::string compressed;
    snappy::Compress(payload.data(), payload.size(), &compressed);
    payload = std::move(compressed);
  }

  std::vector<std::uint8_t> bytes(MIN_PACKET_SIZE + payload.size());
  std::copy_n(version == Version::RIP2 ? "RIP2" : "RIP1", MAGIC_SIZE,
              bytes.begin());
  write_le32(static_cast<std::uint32_t>(bytes.size()), &bytes[MAGIC_SIZE]);
  std::copy(payload.begin(), payload.end(), bytes.begin() + HEADER_SIZE);
  const std::size_t crc_offset = bytes.size() - CRC_SIZE;
  write_le32(crc32(Span<const std::uint8_t>(bytes).first(crc_offset)),
             &bytes[crc_offset]);
  return bytes;
}

}  // namespace waterlinked::sonar::rip

#endif  // WATERLINKEDSONAR_TEST_SUPPORT_RIP_ENCODER_HPP

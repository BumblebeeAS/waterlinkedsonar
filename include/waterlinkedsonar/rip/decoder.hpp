/// \file
/// Decoder for Range Image Protocol packets.

#ifndef WATERLINKEDSONAR_RIP_DECODER_HPP
#define WATERLINKEDSONAR_RIP_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <variant>
#include <waterlinkedsonar/rip/messages.hpp>
#include <waterlinkedsonar/span.hpp>

namespace waterlinked::sonar::rip {

/// Largest packet the sonar sends: the maximum UDP payload.
inline constexpr std::size_t MAX_PACKET_SIZE = 65507;

/// Outcome of decoding one packet.
enum class DecodeStatus : std::uint8_t {
  OK,  ///< Decoded a published message type.
  /// Valid packet with a message type other than the published ones. The
  /// device also sends internal types, which should be ignored.
  UNKNOWN_TYPE,
  BAD_MAGIC,  ///< Neither "RIP2" nor "RIP1".
  /// Packet shorter than its framing or longer than MAX_PACKET_SIZE, or
  /// length field different from the packet size.
  BAD_LENGTH,
  CRC_MISMATCH,       ///< Corrupted packet.
  DECOMPRESS_FAILED,  ///< Invalid snappy data.
  PARSE_FAILED,       ///< Invalid protobuf data.
};

/// Description of \p status, e.g. "CRC mismatch".
const char* to_string(DecodeStatus status) noexcept;

/// Result of Decoder::decode().
struct Decoded {
  DecodeStatus status{DecodeStatus::PARSE_FAILED};  ///< Outcome.
  /// The message when status is OK; std::monostate otherwise.
  std::variant<std::monostate, RangeImageView, BitmapImageView, ImuBatchView>
      message;
  /// Type URL of the message when status is UNKNOWN_TYPE.
  std::string_view unknown_type_url;
};

/// Decodes Range Image Protocol packets
/// (https://docs.waterlinked.com/sonar-3d/sonar-3d-15-api/).
///
/// A packet is the magic "RIP2" or "RIP1", the little-endian uint32 packet
/// length, the payload, and the little-endian IEEE CRC-32 of everything
/// before it. The payload is a serialized Packet protobuf, snappy-compressed
/// in RIP2.
///
/// The decoder reuses its buffers, so it stops allocating once they fit the
/// stream. Views it returns are valid until its next decode() call,
/// destruction or move. Not thread-safe.
class Decoder {
 public:
  /// Creates a decoder; buffers are allocated on first use.
  Decoder();
  /// Frees the buffers, invalidating the views returned by decode().
  ~Decoder();
  /// Takes over the buffers of \p other, which may only be destroyed or
  /// assigned to afterwards.
  Decoder(Decoder&& other) noexcept;
  /// Same as the move constructor.
  Decoder& operator=(Decoder&& other) noexcept;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  /// Decodes one packet that spans all of \p packet.
  Decoded decode(Span<const std::uint8_t> packet);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace waterlinked::sonar::rip

#endif  // WATERLINKEDSONAR_RIP_DECODER_HPP

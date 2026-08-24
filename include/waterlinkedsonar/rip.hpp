#ifndef WATERLINKEDSONAR_RIP_HPP
#define WATERLINKEDSONAR_RIP_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <waterlinkedsonar/messages.hpp>

namespace waterlinked::sonar::rip {

/**
 * @brief Outcome of decoding one datagram.
 */
enum class DecodeStatus : std::uint8_t {
  OK,
  /** Valid packet carrying a message type other than the three published
   * ones. The device routinely sends internal types; skip quietly. */
  UNKNOWN_TYPE,
  BAD_MAGIC,
  BAD_LENGTH,
  CRC_MISMATCH,
  DECOMPRESS_FAILED,
  PARSE_FAILED,
};

/**
 * @brief Human-readable name for a decode status.
 *
 * @param[in] status Status to describe.
 * @return Static string, never null.
 */
const char* to_string(DecodeStatus status);

/**
 * @brief Result of Decoder::decode().
 *
 * `payload` holds a view when `status` is OK and `std::monostate` otherwise.
 * The views point into the decoder's storage; see Decoder for their lifetime.
 */
struct Decoded {
  DecodeStatus status{DecodeStatus::PARSE_FAILED};
  std::variant<std::monostate, RangeImageView, BitmapImageView, ImuBatchView>
      payload;
  /** Type URL of the skipped message when status is UNKNOWN_TYPE. */
  std::string unknown_type_url;
};

/**
 * @brief Range Image Protocol codec
 * (https://docs.waterlinked.com/sonar-3d/sonar-3d-15-api/).
 *
 * A packet is [4B magic "RIP2"|"RIP1"][4B LE total length][payload]
 * [4B LE CRC-32], where the CRC covers everything before it and the RIP2
 * payload is a snappy-compressed serialized protobuf ("RIP1": uncompressed).
 *
 * The decoder reuses internal buffers across calls, so decoding is
 * steady-state allocation-free and the returned views point into those
 * buffers: a view is valid until the next decode() on the same Decoder, like
 * an iterator invalidated by container mutation. Copy any data kept longer.
 * Not thread-safe; one caller at a time.
 */
class Decoder {
 public:
  Decoder();
  ~Decoder();
  Decoder(Decoder&&) noexcept;
  Decoder& operator=(Decoder&&) noexcept;
  Decoder(const Decoder&) = delete;
  Decoder& operator=(const Decoder&) = delete;

  /**
   * @brief Decodes one datagram. Never throws; malformed input is reported
   * through the returned status.
   *
   * @param[in] data Datagram bytes.
   * @param[in] size Datagram size; the packet's length field must equal it.
   * @return Status plus, on OK, a view of the decoded message. Invalidates
   *         views returned by previous calls.
   */
  Decoded decode(const std::uint8_t* data, std::size_t size);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace waterlinked::sonar::rip

#endif  // WATERLINKEDSONAR_RIP_HPP

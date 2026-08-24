#ifndef WATERLINKEDSONAR_MESSAGES_HPP
#define WATERLINKEDSONAR_MESSAGES_HPP

#include <cstdint>
#include <waterlinkedsonar/span.hpp>

namespace waterlinked::sonar {

/**
 * @brief Per-shot metadata shared by the image message types.
 */
struct MessageHeader {
  /** Device timestamp, nanoseconds since the Unix epoch. Meaningful in
   * absolute terms only when the sonar's clock is NTP-synchronized. */
  std::int64_t timestamp_ns{0};
  /** Increments per acoustic shot; shared between the RangeImageView and
   * BitmapImageView of the same shot. Wraps at the uint32 maximum. */
  std::uint32_t sequence_id{0};
};

/**
 * @brief Per-pixel distance to the strongest reflection.
 *
 * A view into decoder-owned storage: valid until the next
 * rip::Decoder::decode() on the decoder that produced it (in Receiver
 * callbacks, for the duration of the callback). Copy any data kept longer.
 */
struct RangeImageView {
  MessageHeader header;
  float speed_of_sound{0.0F};  ///< Configured speed of sound, m/s.
  float range{0.0F};           ///< Configured maximum range, meters.
  std::uint32_t frequency{0};  ///< Imaging frequency, Hz.
  std::uint32_t width{0};      ///< Pixels per row.
  std::uint32_t height{0};     ///< Rows.
  float fov_horizontal{0.0F};  ///< Degrees.
  float fov_vertical{0.0F};    ///< Degrees.
  /** Multiply a pixel value by this to obtain distance in meters. */
  float image_pixel_scale{0.0F};
  /** Row-major, width * height entries, 16-bit values; 0 means no data. */
  Span<const std::uint32_t> image_pixel_data;
};

/**
 * @brief What a BitmapImageView shows.
 */
enum class BitmapImageType : std::uint8_t {
  /** Logarithmic strength of the strongest reflection per pixel:
   * pixel = 100 * log10(linear / 30), linear strength is 15-bit. */
  SIGNAL_STRENGTH,
  /** Shaded rendering of the range image. Experimental on the device and may
   * be removed by Water Linked. */
  SHADED,
};

/**
 * @brief 8-bit greyscale image, displayable without further processing.
 *
 * Same view lifetime as RangeImageView.
 */
struct BitmapImageView {
  MessageHeader header;
  float speed_of_sound{0.0F};  ///< Configured speed of sound, m/s.
  float range{0.0F};           ///< Configured maximum range, meters.
  std::uint32_t frequency{0};  ///< Imaging frequency, Hz.
  BitmapImageType type{BitmapImageType::SIGNAL_STRENGTH};
  std::uint32_t width{0};      ///< Pixels per row.
  std::uint32_t height{0};     ///< Rows.
  float fov_horizontal{0.0F};  ///< Degrees.
  float fov_vertical{0.0F};    ///< Degrees.
  /** Row-major, width * height bytes; 0 means no data. */
  Span<const std::uint8_t> image_pixel_data;
};

/**
 * @brief Batch of ~100 Hz IMU samples, sent while acoustics is enabled.
 *
 * The IMU sits at (-22, -46, -3) mm from the point-cloud origin. Same view
 * lifetime as RangeImageView.
 */
struct ImuBatchView {
  /** Increments per batch, independent of the imaging sequence IDs. */
  std::uint32_t batch_sequence_id{0};
  std::uint32_t samples{0};
  /** Per-sample device timestamps, nanoseconds since the Unix epoch;
   * `samples` entries. */
  Span<const std::int64_t> timestamps_ns;
  /** Specific force, m/s^2; `samples * 3` entries as x,y,z triples. */
  Span<const float> specific_force;
  /** Rate of turn, rad/s; `samples * 3` entries as x,y,z triples. */
  Span<const float> rate_of_turn;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_MESSAGES_HPP

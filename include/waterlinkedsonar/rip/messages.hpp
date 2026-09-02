/// \file
/// Views of the messages carried by the Range Image Protocol.
///
/// A view points into storage owned by the rip::Decoder that produced it and
/// is valid until that decoder's next decode() call, destruction or move.

#ifndef WATERLINKEDSONAR_RIP_MESSAGES_HPP
#define WATERLINKEDSONAR_RIP_MESSAGES_HPP

#include <cstdint>
#include <waterlinkedsonar/span.hpp>

namespace waterlinked::sonar {

/// Metadata shared by the images of one acoustic shot.
struct MessageHeader {
  /// Device time in nanoseconds since the Unix epoch. Comparable with other
  /// clocks only while the device clock is synchronized.
  std::int64_t timestamp_ns{0};
  /// Shot counter, shared by the range and bitmap image of a shot. Wraps to
  /// 0 after the uint32 maximum.
  std::uint32_t sequence_id{0};
};

/// Distance to the strongest reflection for each pixel.
struct RangeImageView {
  MessageHeader header;        ///< Shot metadata.
  float speed_of_sound{0.0F};  ///< m/s.
  float range{0.0F};           ///< Configured maximum range in meters.
  std::uint32_t frequency{0};  ///< Hz.
  std::uint32_t width{0};      ///< Pixels per row.
  std::uint32_t height{0};     ///< Rows.
  float fov_horizontal{0.0F};  ///< Degrees.
  float fov_vertical{0.0F};    ///< Degrees.
  /// Meters per pixel value.
  float image_pixel_scale{0.0F};
  /// Row-major, width * height values of at most 16 bits; 0 means no
  /// reflection.
  Span<const std::uint32_t> image_pixel_data;
};

/// Content of a BitmapImageView.
enum class BitmapImageType : std::uint8_t {
  /// Strength of the strongest reflection, as 100 * log10(linear / 30) of the
  /// 15-bit linear strength.
  SIGNAL_STRENGTH,
  /// Shaded rendering of the range image. Experimental on the device.
  SHADED,
};

/// 8-bit greyscale image that can be displayed as is.
struct BitmapImageView {
  MessageHeader header;        ///< Shot metadata.
  float speed_of_sound{0.0F};  ///< m/s.
  float range{0.0F};           ///< Configured maximum range in meters.
  std::uint32_t frequency{0};  ///< Hz.
  BitmapImageType type{BitmapImageType::SIGNAL_STRENGTH};  ///< Content.
  std::uint32_t width{0};                                  ///< Pixels per row.
  std::uint32_t height{0};                                 ///< Rows.
  float fov_horizontal{0.0F};                              ///< Degrees.
  float fov_vertical{0.0F};                                ///< Degrees.
  /// Row-major, width * height bytes; 0 means no reflection.
  Span<const std::uint8_t> image_pixel_data;
};

/// IMU samples taken at about 100 Hz while acoustics is enabled.
///
/// The IMU sits at (-22, -46, -3) mm from the point cloud origin.
struct ImuBatchView {
  /// Batch counter, independent of MessageHeader::sequence_id.
  std::uint32_t batch_sequence_id{0};
  std::uint32_t samples{0};  ///< Number of samples.
  /// Device time of each sample in nanoseconds since the Unix epoch.
  Span<const std::int64_t> timestamps_ns;
  /// Specific force in m/s^2 as samples x, y, z triples.
  Span<const float> specific_force;
  /// Rate of turn in rad/s as samples x, y, z triples.
  Span<const float> rate_of_turn;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_RIP_MESSAGES_HPP

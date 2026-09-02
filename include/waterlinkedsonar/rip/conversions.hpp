/// \file
/// Conversions of decoded images to distances, point clouds and linear
/// signal strength.
///
/// Each function resizes its output vector and leaves it empty when the image
/// is inconsistent. Reusing an output vector across calls avoids reallocating
/// it.

#ifndef WATERLINKEDSONAR_RIP_CONVERSIONS_HPP
#define WATERLINKEDSONAR_RIP_CONVERSIONS_HPP

#include <cstdint>
#include <vector>
#include <waterlinkedsonar/rip/messages.hpp>

namespace waterlinked::sonar {

/// Layout of the point cloud produced by range_image_to_points().
enum class PointLayout : std::uint8_t {
  /// One point per pixel in row-major order; NaN where a pixel has no
  /// reflection.
  ORGANIZED,
  /// One point per pixel with a reflection, in row-major order.
  UNORGANIZED,
};

/// Converts a range image to per-pixel distances in meters, 0 where a pixel
/// has no reflection.
///
/// \returns false if the pixel count is not width * height.
bool range_image_to_distances(const RangeImageView& image,
                              std::vector<float>& distances);

/// Converts a range image to x, y, z triples in meters.
///
/// Pixel (px, py) at distance d maps to yaw = px / (width - 1) * fov_h -
/// fov_h / 2 and pitch = py / (height - 1) * fov_v - fov_v / 2, and to
/// (d cos(pitch) cos(yaw), d cos(pitch) sin(yaw), -d sin(pitch)).
///
/// \returns false if width or height is below 2 or the pixel count is not
///   width * height.
bool range_image_to_points(const RangeImageView& image,
                           std::vector<float>& points, PointLayout layout);

/// Converts a SIGNAL_STRENGTH bitmap to 15-bit linear signal strength, 0
/// where a pixel has no reflection.
///
/// \returns false if the pixel count is not width * height.
bool bitmap_to_strength_linear(const BitmapImageView& image,
                               std::vector<std::uint16_t>& strength);

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_RIP_CONVERSIONS_HPP

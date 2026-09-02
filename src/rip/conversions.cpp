#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <waterlinkedsonar/rip/conversions.hpp>

namespace waterlinked::sonar {

namespace {

constexpr double PI = 3.14159265358979323846;

constexpr double to_radians(double degrees) { return degrees * PI / 180.0; }

bool has_all_pixels(std::uint32_t width, std::uint32_t height,
                    std::size_t pixel_count) {
  return static_cast<std::size_t>(width) * height == pixel_count;
}

}  // namespace

bool range_image_to_distances(const RangeImageView& image,
                              std::vector<float>& distances) {
  const auto pixels = image.image_pixel_data;
  if (!has_all_pixels(image.width, image.height, pixels.size())) {
    distances.clear();
    return false;
  }
  distances.resize(pixels.size());
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    distances[i] = static_cast<float>(pixels[i]) * image.image_pixel_scale;
  }
  return true;
}

bool range_image_to_points(const RangeImageView& image,
                           std::vector<float>& points, PointLayout layout) {
  const auto pixels = image.image_pixel_data;
  points.clear();
  if (image.width < 2 || image.height < 2 ||
      !has_all_pixels(image.width, image.height, pixels.size())) {
    return false;
  }
  const double fov_h = to_radians(image.fov_horizontal);
  const double fov_v = to_radians(image.fov_vertical);
  const double max_x = image.width - 1;
  const double max_y = image.height - 1;

  std::vector<double> cos_yaw(image.width);
  std::vector<double> sin_yaw(image.width);
  for (std::uint32_t px = 0; px < image.width; ++px) {
    const double yaw = (px / max_x * fov_h) - (fov_h / 2);
    cos_yaw[px] = std::cos(yaw);
    sin_yaw[px] = std::sin(yaw);
  }

  points.resize(pixels.size() * 3);
  auto out = points.begin();
  std::size_t i = 0;
  for (std::uint32_t py = 0; py < image.height; ++py) {
    const double pitch = (py / max_y * fov_v) - (fov_v / 2);
    const double cos_pitch = std::cos(pitch);
    const double sin_pitch = std::sin(pitch);
    for (std::uint32_t px = 0; px < image.width; ++px, ++i) {
      if (pixels[i] == 0) {
        if (layout == PointLayout::ORGANIZED) {
          out = std::fill_n(out, 3, std::numeric_limits<float>::quiet_NaN());
        }
        continue;
      }
      const double d = static_cast<double>(pixels[i]) * image.image_pixel_scale;
      *out++ = static_cast<float>(d * cos_pitch * cos_yaw[px]);
      *out++ = static_cast<float>(d * cos_pitch * sin_yaw[px]);
      *out++ = static_cast<float>(-d * sin_pitch);
    }
  }
  points.erase(out, points.end());
  return true;
}

bool bitmap_to_strength_linear(const BitmapImageView& image,
                               std::vector<std::uint16_t>& strength) {
  const auto pixels = image.image_pixel_data;
  if (!has_all_pixels(image.width, image.height, pixels.size())) {
    strength.clear();
    return false;
  }
  strength.resize(pixels.size());
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    // Inverts pixel = 100 * log10(linear / 30).
    strength[i] = pixels[i] == 0
                      ? 0
                      : static_cast<std::uint16_t>(std::lround(
                            30.0 * std::pow(10.0, pixels[i] / 100.0)));
  }
  return true;
}

}  // namespace waterlinked::sonar

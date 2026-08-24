#include <cmath>
#include <cstddef>
#include <limits>
#include <waterlinkedsonar/conversions.hpp>

namespace waterlinked::sonar {

namespace {
constexpr float QUIET_NAN = std::numeric_limits<float>::quiet_NaN();
}

bool range_image_to_distances(const RangeImageView& img,
                              std::vector<float>& out) {
  const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
  if (img.image_pixel_data.size() != n) {
    out.clear();
    return false;
  }
  const float scale = img.image_pixel_scale;
  out.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = static_cast<float>(img.image_pixel_data[i]) * scale;
  }
  return true;
}

bool range_image_to_points(const RangeImageView& img, std::vector<float>& out,
                           bool organized) {
  const std::uint32_t width = img.width;
  const std::uint32_t height = img.height;
  const std::size_t n = static_cast<std::size_t>(width) * height;
  if (width < 2 || height < 2 || img.image_pixel_data.size() != n) {
    out.clear();
    return false;
  }
  const double fov_h = img.fov_horizontal * M_PI / 180.0;
  const double fov_v = img.fov_vertical * M_PI / 180.0;
  const float scale = img.image_pixel_scale;

  out.clear();
  out.reserve(n * 3);
  std::size_t i = 0;
  for (std::uint32_t py = 0; py < height; ++py) {
    const double pitch =
        ((static_cast<double>(py) / (height - 1)) * fov_v) - (fov_v / 2.0);
    const double cos_pitch = std::cos(pitch);
    const double sin_pitch = std::sin(pitch);
    for (std::uint32_t px = 0; px < width; ++px, ++i) {
      const std::uint32_t value = img.image_pixel_data[i];
      if (value == 0) {
        if (organized) {
          out.push_back(QUIET_NAN);
          out.push_back(QUIET_NAN);
          out.push_back(QUIET_NAN);
        }
        continue;
      }
      const double yaw =
          ((static_cast<double>(px) / (width - 1)) * fov_h) - (fov_h / 2.0);
      const double d = static_cast<double>(value) * scale;
      out.push_back(static_cast<float>(d * cos_pitch * std::cos(yaw)));
      out.push_back(static_cast<float>(d * cos_pitch * std::sin(yaw)));
      out.push_back(static_cast<float>(-d * sin_pitch));
    }
  }
  return true;
}

bool bitmap_to_strength_linear(const BitmapImageView& img,
                               std::vector<std::uint16_t>& out) {
  const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
  if (img.image_pixel_data.size() != n) {
    out.clear();
    return false;
  }
  out.resize(n);
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint8_t v = img.image_pixel_data[i];
    // Inverse of pixel = 100 * log10(linear / 30); 0 means no data.
    out[i] = v == 0 ? 0
                    : static_cast<std::uint16_t>(
                          std::lround(30.0 * std::pow(10.0, v / 100.0)));
  }
  return true;
}

}  // namespace waterlinked::sonar

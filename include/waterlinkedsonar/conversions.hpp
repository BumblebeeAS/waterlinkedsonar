#ifndef WATERLINKEDSONAR_CONVERSIONS_HPP
#define WATERLINKEDSONAR_CONVERSIONS_HPP

#include <cstdint>
#include <vector>
#include <waterlinkedsonar/messages.hpp>

namespace waterlinked::sonar {

// These run on the receive thread in callback-driven consumers, so they
// report inconsistent input through the return value instead of throwing.
// Output vectors are resized, not reallocated, on repeated calls.

/**
 * @brief Converts a range image to per-pixel distances.
 *
 * @param[in] img Image to convert.
 * @param[out] out Resized to width * height; out[i] = pixel[i] *
 *             image_pixel_scale in meters, 0.0 where the pixel has no data.
 * @return False when the pixel count does not equal width * height (out is
 *         left cleared); true otherwise.
 */
bool range_image_to_distances(const RangeImageView& img,
                              std::vector<float>& out);

/**
 * @brief Converts a range image to XYZ float32 triples using the
 * pixel-to-angle mapping from the Water Linked docs:
 *   yaw   = (px / (width - 1))  * fov_h - fov_h / 2
 *   pitch = (py / (height - 1)) * fov_v - fov_v / 2
 *   (x, y, z) = d * (cos p * cos y, cos p * sin y, -sin p)
 *
 * @param[in] img Image to convert.
 * @param[out] out Organized: width * height triples in row-major pixel order,
 *             NaN for pixels with no data. Unorganized: one triple per valid
 *             pixel.
 * @param[in] organized Selects the output layout.
 * @return False when width or height is less than 2 or the pixel count does
 *         not equal width * height (out is left cleared); true otherwise.
 */
bool range_image_to_points(const RangeImageView& img, std::vector<float>& out,
                           bool organized);

/**
 * @brief Recovers linear 15-bit signal strength from a SIGNAL_STRENGTH
 * bitmap, inverting the device's logarithmic mapping
 * pixel = 100 * log10(linear / 30).
 *
 * @param[in] img Image to convert.
 * @param[out] out Resized to width * height; 0 where the pixel has no data.
 * @return False when the pixel count does not equal width * height (out is
 *         left cleared); true otherwise.
 */
bool bitmap_to_strength_linear(const BitmapImageView& img,
                               std::vector<std::uint16_t>& out);

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_CONVERSIONS_HPP

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <variant>
#include <vector>
#include <waterlinkedsonar/rip/conversions.hpp>
#include <waterlinkedsonar/rip/decoder.hpp>

#include "support/recording.hpp"

namespace waterlinked::sonar {
namespace {

constexpr double TOLERANCE = 1e-5;

// A 3x3 image with a 90x90 degree field of view, so the corner pixels look
// 45 degrees off axis in both directions.
RangeImageView three_by_three(Span<const std::uint32_t> pixels) {
  RangeImageView image;
  image.width = 3;
  image.height = 3;
  image.fov_horizontal = 90.0F;
  image.fov_vertical = 90.0F;
  image.image_pixel_scale = 0.5F;
  image.image_pixel_data = pixels;
  return image;
}

TEST(RangeImageToDistances, ScalesPixelValues) {
  const std::array<std::uint32_t, 9> pixels{0, 1, 2, 3, 4, 5, 6, 7, 8};
  std::vector<float> distances;
  ASSERT_TRUE(range_image_to_distances(three_by_three(pixels), distances));
  EXPECT_EQ(distances, (std::vector<float>{0.0F, 0.5F, 1.0F, 1.5F, 2.0F, 2.5F,
                                           3.0F, 3.5F, 4.0F}));
}

TEST(RangeImageToPoints, MapsPixelsToDirections) {
  // Distance 4 m everywhere except the empty top-right pixel.
  const std::array<std::uint32_t, 9> pixels{8, 8, 0, 8, 8, 8, 8, 8, 8};
  std::vector<float> points;
  ASSERT_TRUE(range_image_to_points(three_by_three(pixels), points,
                                    PointLayout::ORGANIZED));
  ASSERT_EQ(points.size(), 27U);
  const auto expect_point = [&](std::size_t pixel, double x, double y,
                                double z) {
    EXPECT_NEAR(points[pixel * 3], x, TOLERANCE) << "pixel " << pixel;
    EXPECT_NEAR(points[(pixel * 3) + 1], y, TOLERANCE) << "pixel " << pixel;
    EXPECT_NEAR(points[(pixel * 3) + 2], z, TOLERANCE) << "pixel " << pixel;
  };
  const double diagonal = 4.0 * std::sqrt(0.5);
  expect_point(4, 4.0, 0.0, 0.0);             // centre: straight ahead
  expect_point(3, diagonal, -diagonal, 0.0);  // left edge: yaw -45
  expect_point(1, diagonal, 0.0, diagonal);   // top edge: pitch -45
  expect_point(0, 2.0, -2.0, diagonal);       // top-left corner
  expect_point(8, 2.0, 2.0, -diagonal);       // bottom-right corner
  EXPECT_TRUE(std::isnan(points[6]) && std::isnan(points[7]) &&
              std::isnan(points[8]));
}

TEST(RangeImageToPoints, UnorganizedSkipsEmptyPixels) {
  const std::array<std::uint32_t, 9> pixels{0, 0, 0, 0, 8, 0, 0, 0, 2};
  std::vector<float> points;
  ASSERT_TRUE(range_image_to_points(three_by_three(pixels), points,
                                    PointLayout::UNORGANIZED));
  ASSERT_EQ(points.size(), 6U);
  EXPECT_NEAR(points[0], 4.0, TOLERANCE);
  EXPECT_NEAR(points[3], 0.5, TOLERANCE);
}

TEST(BitmapToStrengthLinear, InvertsLogarithmicMapping) {
  // pixel = 100 * log10(linear / 30), so linear = 30 * 10^(pixel / 100).
  const std::array<std::uint8_t, 4> pixels{0, 100, 200, 255};
  BitmapImageView image;
  image.width = 2;
  image.height = 2;
  image.image_pixel_data = pixels;
  std::vector<std::uint16_t> strength;
  ASSERT_TRUE(bitmap_to_strength_linear(image, strength));
  EXPECT_EQ(strength, (std::vector<std::uint16_t>{0, 300, 3000, 10644}));
}

TEST(Conversions, RejectPixelCountMismatch) {
  const std::array<std::uint32_t, 8> range_pixels{};
  const RangeImageView range = three_by_three(range_pixels);
  std::vector<float> out{1.0F};
  EXPECT_FALSE(range_image_to_distances(range, out));
  EXPECT_TRUE(out.empty());
  out = {1.0F};
  EXPECT_FALSE(range_image_to_points(range, out, PointLayout::ORGANIZED));
  EXPECT_TRUE(out.empty());

  const std::array<std::uint8_t, 3> bitmap_pixels{};
  BitmapImageView bitmap;
  bitmap.width = 2;
  bitmap.height = 2;
  bitmap.image_pixel_data = bitmap_pixels;
  std::vector<std::uint16_t> strength{1};
  EXPECT_FALSE(bitmap_to_strength_linear(bitmap, strength));
  EXPECT_TRUE(strength.empty());
}

TEST(RangeImageToPoints, RejectsSingleColumnImage) {
  const std::array<std::uint32_t, 3> pixels{1, 1, 1};
  RangeImageView image = three_by_three(pixels);
  image.width = 1;
  std::vector<float> points;
  EXPECT_FALSE(range_image_to_points(image, points, PointLayout::ORGANIZED));
}

// On real images every point lies at its pixel's distance from the origin.
TEST(RangeImageToPoints, PreservesDistancesOnRecording) {
  rip::Decoder decoder;
  std::size_t images = 0;
  for (const auto& packet : test::read_recording(test::SHIP_RECORDING)) {
    const rip::Decoded decoded = decoder.decode(packet);
    const auto* image = std::get_if<RangeImageView>(&decoded.message);
    if (image == nullptr) {
      continue;
    }
    ++images;
    std::vector<float> distances;
    std::vector<float> points;
    ASSERT_TRUE(range_image_to_distances(*image, distances));
    ASSERT_TRUE(range_image_to_points(*image, points, PointLayout::ORGANIZED));
    std::size_t mismatches = 0;
    for (std::size_t i = 0; i < distances.size(); ++i) {
      const float* point = &points[i * 3];
      if (distances[i] == 0.0F) {
        mismatches += std::isnan(point[0]) ? 0 : 1;
        continue;
      }
      const double norm =
          std::sqrt((point[0] * point[0]) + (point[1] * point[1]) +
                    (point[2] * point[2]));
      mismatches += std::abs(norm - distances[i]) < 1e-4 ? 0 : 1;
    }
    EXPECT_EQ(mismatches, 0U) << "sequence " << image->header.sequence_id;
  }
  EXPECT_EQ(images, 6U);
}

}  // namespace
}  // namespace waterlinked::sonar

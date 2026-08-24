#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <memory>
#include <variant>
#include <vector>
#include <waterlinkedsonar/conversions.hpp>
#include <waterlinkedsonar/messages.hpp>
#include <waterlinkedsonar/rip.hpp>

namespace waterlinked::sonar {

namespace {

// A decoded range image together with the decoder that owns its storage: the
// view stays valid only while the decoder lives and makes no further decodes.
struct RecordedRangeImage {
  rip::Decoder decoder;
  RangeImageView view;
};

// First RangeImage from test/data/ship_short.sonar, a recording captured
// from a real sonar. Null when the recording cannot be read.
std::unique_ptr<RecordedRangeImage> first_recorded_range_image() {
  std::ifstream f("test/data/ship_short.sonar", std::ios::binary);
  if (!f.good()) {
    return nullptr;
  }
  auto recorded = std::make_unique<RecordedRangeImage>();
  while (true) {
    std::uint8_t header[8];
    f.read(reinterpret_cast<char*>(header), 8);
    if (f.gcount() < 8) {
      return nullptr;
    }
    const std::uint32_t total = static_cast<std::uint32_t>(header[4]) |
                                (static_cast<std::uint32_t>(header[5]) << 8) |
                                (static_cast<std::uint32_t>(header[6]) << 16) |
                                (static_cast<std::uint32_t>(header[7]) << 24);
    std::vector<std::uint8_t> packet(total);
    std::copy(header, header + 8, packet.begin());
    f.read(reinterpret_cast<char*>(packet.data()) + 8, total - 8);
    const rip::Decoded d =
        recorded->decoder.decode(packet.data(), packet.size());
    if (d.status != rip::DecodeStatus::OK) {
      continue;
    }
    if (const auto* view = std::get_if<RangeImageView>(&d.payload)) {
      recorded->view = *view;
      return recorded;
    }
  }
}

}  // namespace

TEST(Conversions, DistancesFromRecordedImage) {
  const auto recorded = first_recorded_range_image();
  ASSERT_NE(recorded, nullptr);
  const RangeImageView& img = recorded->view;

  std::vector<float> distances;
  ASSERT_TRUE(range_image_to_distances(img, distances));
  ASSERT_EQ(distances.size(), static_cast<std::size_t>(img.width) * img.height);

  bool any_valid = false;
  bool any_zero = false;
  for (std::size_t i = 0; i < distances.size(); ++i) {
    const std::uint32_t pixel = img.image_pixel_data[i];
    // ASSERT: a formula regression would otherwise fail on thousands of
    // pixels; the first one with its index is the useful report.
    ASSERT_FLOAT_EQ(distances[i],
                    static_cast<float>(pixel) * img.image_pixel_scale)
        << "pixel " << i;
    any_valid |= pixel != 0;
    any_zero |= pixel == 0;
  }
  EXPECT_TRUE(any_valid);
  EXPECT_TRUE(any_zero);
}

TEST(Conversions, PointsMatchAngularFormula) {
  const auto recorded = first_recorded_range_image();
  ASSERT_NE(recorded, nullptr);
  const RangeImageView& img = recorded->view;

  std::vector<float> organized;
  ASSERT_TRUE(range_image_to_points(img, organized, true));
  ASSERT_EQ(organized.size(),
            static_cast<std::size_t>(img.width) * img.height * 3);

  const double fov_h = img.fov_horizontal * M_PI / 180.0;
  const double fov_v = img.fov_vertical * M_PI / 180.0;
  const std::size_t n = static_cast<std::size_t>(img.width) * img.height;
  std::size_t checked = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t pixel = img.image_pixel_data[i];
    const float x = organized[i * 3];
    const float y = organized[i * 3 + 1];
    const float z = organized[i * 3 + 2];
    // ASSERT throughout the loop: a formula regression would otherwise fail
    // on thousands of pixels; the first one with its index is the useful
    // report.
    if (pixel == 0) {
      ASSERT_TRUE(std::isnan(x) && std::isnan(y) && std::isnan(z))
          << "pixel " << i;
      continue;
    }
    const std::size_t px = i % img.width;
    const std::size_t py = i / img.width;
    const double d = pixel * static_cast<double>(img.image_pixel_scale);
    const double yaw =
        (static_cast<double>(px) / (img.width - 1)) * fov_h - fov_h / 2;
    const double pitch =
        (static_cast<double>(py) / (img.height - 1)) * fov_v - fov_v / 2;
    ASSERT_NEAR(x, d * std::cos(pitch) * std::cos(yaw), 1e-3) << "pixel " << i;
    ASSERT_NEAR(y, d * std::cos(pitch) * std::sin(yaw), 1e-3) << "pixel " << i;
    ASSERT_NEAR(z, -d * std::sin(pitch), 1e-3) << "pixel " << i;
    ++checked;
  }
  EXPECT_GT(checked, 0U);

  std::vector<float> unorganized;
  ASSERT_TRUE(range_image_to_points(img, unorganized, false));
  EXPECT_EQ(unorganized.size(), checked * 3);
  for (float v : unorganized) {
    EXPECT_FALSE(std::isnan(v));
  }
}

TEST(Conversions, StrengthLinearInvertsLogMapping) {
  std::vector<std::uint8_t> pixels(256 * 64, 0);
  pixels[1] = 100;
  pixels[2] = 255;
  BitmapImageView img;
  img.type = BitmapImageType::SIGNAL_STRENGTH;
  img.width = 256;
  img.height = 64;
  img.image_pixel_data = Span<const std::uint8_t>(pixels.data(), pixels.size());

  std::vector<std::uint16_t> linear;
  ASSERT_TRUE(bitmap_to_strength_linear(img, linear));
  ASSERT_EQ(linear.size(), pixels.size());

  EXPECT_EQ(linear[0], 0);
  EXPECT_EQ(linear[1], std::lround(30.0 * std::pow(10.0, 100.0 / 100.0)));
  EXPECT_GT(linear[2], linear[1]);
  EXPECT_LE(linear[2], (1 << 15) - 1);
}

TEST(Conversions, PointsRejectSingleColumnImage) {
  const std::uint32_t pixels[2] = {100, 200};
  RangeImageView img;
  img.width = 1;
  img.height = 2;
  img.fov_horizontal = 40.0F;
  img.fov_vertical = 40.0F;
  img.image_pixel_scale = 0.001F;
  img.image_pixel_data = Span<const std::uint32_t>(pixels, 2);

  std::vector<float> out{1.0F};  // stale content from an earlier conversion
  EXPECT_FALSE(range_image_to_points(img, out, true));
  EXPECT_TRUE(out.empty());
}

TEST(Conversions, RangeImageRejectsPixelCountMismatch) {
  const std::uint32_t pixels[7] = {};
  RangeImageView img;
  img.width = 4;
  img.height = 2;
  img.fov_horizontal = 40.0F;
  img.fov_vertical = 40.0F;
  img.image_pixel_scale = 0.001F;
  img.image_pixel_data = Span<const std::uint32_t>(pixels, 7);

  std::vector<float> distances{1.0F};
  EXPECT_FALSE(range_image_to_distances(img, distances));
  EXPECT_TRUE(distances.empty());

  std::vector<float> points{1.0F};
  EXPECT_FALSE(range_image_to_points(img, points, true));
  EXPECT_TRUE(points.empty());
}

TEST(Conversions, BitmapRejectsPixelCountMismatch) {
  const std::uint8_t pixels[7] = {};
  BitmapImageView img;
  img.type = BitmapImageType::SIGNAL_STRENGTH;
  img.width = 4;
  img.height = 2;
  img.image_pixel_data = Span<const std::uint8_t>(pixels, 7);

  std::vector<std::uint16_t> out{1};
  EXPECT_FALSE(bitmap_to_strength_linear(img, out));
  EXPECT_TRUE(out.empty());
}

}  // namespace waterlinked::sonar

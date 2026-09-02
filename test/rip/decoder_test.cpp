#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include <waterlinkedsonar/rip/decoder.hpp>

#include "rip/framing.hpp"
#include "support/recording.hpp"
#include "support/rip_encoder.hpp"

namespace waterlinked::sonar::rip {
namespace {

template <typename T>
std::vector<T> to_vector(Span<const T> values) {
  return {values.begin(), values.end()};
}

proto::RangeImage make_range_image() {
  proto::RangeImage image;
  image.mutable_header()->mutable_timestamp()->set_seconds(3);
  image.mutable_header()->mutable_timestamp()->set_nanos(42);
  image.mutable_header()->set_sequence_id(111);
  image.set_width(16);
  image.set_height(4);
  image.set_fov_horizontal(40.0F);
  image.set_fov_vertical(40.0F);
  image.set_image_pixel_scale(50.0F / 30000.0F);
  for (std::uint32_t i = 0; i < 16U * 4U; ++i) {
    image.add_image_pixel_data(i * 7 % 30000);
  }
  return image;
}

TEST(Decoder, DecodesRangeImage) {
  const proto::RangeImage image = make_range_image();
  const std::vector<std::uint8_t> packet = encode(image);
  Decoder decoder;
  const Decoded decoded = decoder.decode(packet);
  ASSERT_EQ(decoded.status, DecodeStatus::OK);
  const auto* view = std::get_if<RangeImageView>(&decoded.message);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->header.sequence_id, 111U);
  EXPECT_EQ(view->header.timestamp_ns, 3'000'000'042);
  EXPECT_EQ(view->width, 16U);
  EXPECT_EQ(view->height, 4U);
  EXPECT_TRUE(std::equal(
      view->image_pixel_data.begin(), view->image_pixel_data.end(),
      image.image_pixel_data().begin(), image.image_pixel_data().end()));
}

TEST(Decoder, DecodesBitmapImageType) {
  proto::BitmapImageGreyscale8 image;
  image.mutable_header()->set_sequence_id(5);
  image.set_type(proto::SIGNAL_STRENGTH_IMAGE);
  image.set_width(4);
  image.set_height(2);
  image.set_image_pixel_data(std::string("\x01\x02\x03\x04\x05\x06\x07\x08"));

  Decoder decoder;
  std::vector<std::uint8_t> packet = encode(image);
  Decoded decoded = decoder.decode(packet);
  ASSERT_EQ(decoded.status, DecodeStatus::OK);
  const auto* view = std::get_if<BitmapImageView>(&decoded.message);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->header.sequence_id, 5U);
  EXPECT_EQ(view->type, BitmapImageType::SIGNAL_STRENGTH);
  EXPECT_EQ(to_vector(view->image_pixel_data),
            (std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));

  image.set_type(proto::SHADED_IMAGE);
  packet = encode(image);
  decoded = decoder.decode(packet);
  ASSERT_EQ(decoded.status, DecodeStatus::OK);
  EXPECT_EQ(std::get<BitmapImageView>(decoded.message).type,
            BitmapImageType::SHADED);
}

TEST(Decoder, DecodesImuBatch) {
  proto::ImuBatch batch;
  batch.set_batch_sequence_id(123);
  batch.set_samples(1);
  batch.add_timestamp()->set_seconds(10);
  batch.mutable_timestamp(0)->set_nanos(20);
  for (const float value : {1.0F, 2.0F, 3.0F}) {
    batch.add_specific_force(value);
    batch.add_rate_of_turn(value + 3.0F);
  }

  const std::vector<std::uint8_t> packet = encode(batch);
  Decoder decoder;
  const Decoded decoded = decoder.decode(packet);
  ASSERT_EQ(decoded.status, DecodeStatus::OK);
  const auto* view = std::get_if<ImuBatchView>(&decoded.message);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->batch_sequence_id, 123U);
  EXPECT_EQ(view->samples, 1U);
  EXPECT_EQ(to_vector(view->timestamps_ns),
            std::vector<std::int64_t>{10'000'000'020});
  EXPECT_EQ(to_vector(view->specific_force), (std::vector{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(to_vector(view->rate_of_turn), (std::vector{4.0F, 5.0F, 6.0F}));
}

TEST(Decoder, DecodesUncompressedRip1) {
  const std::vector<std::uint8_t> packet =
      encode(make_range_image(), Version::RIP1);
  ASSERT_TRUE(std::equal(packet.begin(), packet.begin() + 4, "RIP1"));
  Decoder decoder;
  const Decoded decoded = decoder.decode(packet);
  ASSERT_EQ(decoded.status, DecodeStatus::OK);
  EXPECT_EQ(std::get<RangeImageView>(decoded.message).header.sequence_id, 111U);
}

TEST(Decoder, ReportsUnknownType) {
  proto::Header header;
  header.set_sequence_id(7);
  const std::vector<std::uint8_t> packet = encode(header);
  Decoder decoder;
  const Decoded decoded = decoder.decode(packet);
  EXPECT_EQ(decoded.status, DecodeStatus::UNKNOWN_TYPE);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(decoded.message));
  EXPECT_EQ(decoded.unknown_type_url,
            "type.googleapis.com/waterlinked.sonar.protocol.Header");
}

TEST(Decoder, RejectsBadMagic) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  packet[0] = 'X';
  EXPECT_EQ(Decoder().decode(packet).status, DecodeStatus::BAD_MAGIC);
}

TEST(Decoder, RejectsLengthMismatch) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  const Span<const std::uint8_t> bytes = packet;
  Decoder decoder;
  for (const std::size_t size :
       {0UL, 4UL, MIN_PACKET_SIZE - 1, packet.size() - 1}) {
    EXPECT_EQ(decoder.decode(bytes.first(size)).status,
              DecodeStatus::BAD_LENGTH)
        << size;
  }

  const std::uint32_t length = read_le32(&packet[MAGIC_SIZE]);
  write_le32(length + 1, &packet[MAGIC_SIZE]);
  EXPECT_EQ(decoder.decode(packet).status, DecodeStatus::BAD_LENGTH);

  write_le32(length, &packet[MAGIC_SIZE]);
  packet.push_back(0);
  EXPECT_EQ(decoder.decode(packet).status, DecodeStatus::BAD_LENGTH);
}

TEST(Decoder, RejectsCrcMismatch) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  packet[HEADER_SIZE + 3] ^= 0x10U;
  EXPECT_EQ(Decoder().decode(packet).status, DecodeStatus::CRC_MISMATCH);
}

TEST(Decoder, DecodesRecording) {
  const auto packets = test::read_recording(test::SHIP_RECORDING);
  ASSERT_EQ(packets.size(), 12U);

  Decoder decoder;
  std::multiset<std::uint32_t> range_ids;
  std::multiset<std::uint32_t> bitmap_ids;
  for (const auto& packet : packets) {
    const Decoded decoded = decoder.decode(packet);
    ASSERT_EQ(decoded.status, DecodeStatus::OK);
    if (const auto* range = std::get_if<RangeImageView>(&decoded.message)) {
      range_ids.insert(range->header.sequence_id);
      EXPECT_EQ(range->image_pixel_data.size(),
                std::size_t{range->width} * range->height);
    } else {
      bitmap_ids.insert(
          std::get<BitmapImageView>(decoded.message).header.sequence_id);
    }
  }
  const std::multiset<std::uint32_t> expected{4448, 4449, 4450,
                                              4451, 4452, 4453};
  EXPECT_EQ(range_ids, expected);
  EXPECT_EQ(bitmap_ids, expected);
}

}  // namespace
}  // namespace waterlinked::sonar::rip

#include <gtest/gtest.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <variant>
#include <vector>
#include <waterlinkedsonar/rip.hpp>

#include "rip_internal.hpp"

namespace waterlinked::sonar::rip {

namespace {

proto::RangeImage make_range_image() {
  proto::RangeImage img;
  img.mutable_header()->mutable_timestamp()->set_seconds(0);
  img.mutable_header()->mutable_timestamp()->set_nanos(42);
  img.mutable_header()->set_sequence_id(111);
  img.set_width(256);
  img.set_height(64);
  img.set_fov_horizontal(40.0F);
  img.set_fov_vertical(40.0F);
  img.set_image_pixel_scale(50.0F / 30000.0F);
  // Varied 16-bit values, so decoded pixel data can be compared byte-for-byte.
  for (int i = 0; i < 256 * 64; ++i) {
    img.add_image_pixel_data(static_cast<std::uint32_t>(i * 7 % 30000));
  }
  return img;
}

std::uint32_t read_le32_at(const std::vector<std::uint8_t>& p,
                           std::size_t off) {
  return static_cast<std::uint32_t>(p[off]) |
         (static_cast<std::uint32_t>(p[off + 1]) << 8) |
         (static_cast<std::uint32_t>(p[off + 2]) << 16) |
         (static_cast<std::uint32_t>(p[off + 3]) << 24);
}

void write_le32_at(std::vector<std::uint8_t>& p, std::size_t off,
                   std::uint32_t v) {
  p[off] = static_cast<std::uint8_t>(v);
  p[off + 1] = static_cast<std::uint8_t>(v >> 8);
  p[off + 2] = static_cast<std::uint8_t>(v >> 16);
  p[off + 3] = static_cast<std::uint8_t>(v >> 24);
}

// Reads consecutive RIP packets from a .sonar recording.
std::vector<std::vector<std::uint8_t>> read_recording(const char* path) {
  std::ifstream f(path, std::ios::binary);
  EXPECT_TRUE(f.good()) << "cannot open " << path;
  std::vector<std::vector<std::uint8_t>> packets;
  while (true) {
    std::vector<std::uint8_t> header(8);
    f.read(reinterpret_cast<char*>(header.data()), 8);
    if (f.gcount() == 0) {
      break;
    }
    EXPECT_EQ(f.gcount(), 8);
    const std::uint32_t total = read_le32_at(header, 4);
    std::vector<std::uint8_t> packet(total);
    std::copy(header.begin(), header.end(), packet.begin());
    f.read(reinterpret_cast<char*>(packet.data()) + 8, total - 8);
    EXPECT_EQ(static_cast<std::uint32_t>(f.gcount()), total - 8);
    packets.push_back(std::move(packet));
  }
  return packets;
}

}  // namespace

TEST(Rip, RoundTripRangeImage) {
  const proto::RangeImage img = make_range_image();
  const std::vector<std::uint8_t> packet = encode(img);

  Decoder decoder;
  const Decoded d = decoder.decode(packet.data(), packet.size());
  ASSERT_EQ(d.status, DecodeStatus::OK);
  const auto* view = std::get_if<RangeImageView>(&d.payload);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->header.sequence_id, 111U);
  EXPECT_EQ(view->header.timestamp_ns, 42);
  EXPECT_EQ(view->width, 256U);
  EXPECT_EQ(view->height, 64U);
  ASSERT_EQ(view->image_pixel_data.size(), 256U * 64U);
  EXPECT_TRUE(std::equal(view->image_pixel_data.begin(),
                         view->image_pixel_data.end(),
                         img.image_pixel_data().begin()));
}

TEST(Rip, RoundTripBitmapImageMapsType) {
  proto::BitmapImageGreyscale8 img;
  img.mutable_header()->set_sequence_id(5);
  img.set_type(proto::SIGNAL_STRENGTH_IMAGE);
  img.set_width(4);
  img.set_height(2);
  img.set_image_pixel_data(std::string("\x01\x02\x03\x04\x05\x06\x07\x08", 8));

  Decoder decoder;
  std::vector<std::uint8_t> packet = encode(img);
  Decoded d = decoder.decode(packet.data(), packet.size());
  ASSERT_EQ(d.status, DecodeStatus::OK);
  const auto* view = std::get_if<BitmapImageView>(&d.payload);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->header.sequence_id, 5U);
  EXPECT_EQ(view->type, BitmapImageType::SIGNAL_STRENGTH);
  ASSERT_EQ(view->image_pixel_data.size(), 8U);
  EXPECT_EQ(view->image_pixel_data[0], 1U);
  EXPECT_EQ(view->image_pixel_data[7], 8U);

  img.set_type(proto::SHADED_IMAGE);
  packet = encode(img);
  d = decoder.decode(packet.data(), packet.size());
  ASSERT_EQ(d.status, DecodeStatus::OK);
  const auto* shaded = std::get_if<BitmapImageView>(&d.payload);
  ASSERT_NE(shaded, nullptr);
  EXPECT_EQ(shaded->type, BitmapImageType::SHADED);
}

TEST(Rip, RoundTripImuBatch) {
  proto::ImuBatch batch;
  batch.set_batch_sequence_id(123);
  batch.set_samples(1);
  auto* ts = batch.add_timestamp();
  ts->set_seconds(10);
  ts->set_nanos(20);
  for (float v : {1.0F, 2.0F, 3.0F}) {
    batch.add_specific_force(v);
  }
  for (float v : {4.0F, 5.0F, 6.0F}) {
    batch.add_rate_of_turn(v);
  }

  const std::vector<std::uint8_t> packet = encode(batch);
  Decoder decoder;
  const Decoded d = decoder.decode(packet.data(), packet.size());
  ASSERT_EQ(d.status, DecodeStatus::OK);
  const auto* view = std::get_if<ImuBatchView>(&d.payload);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->batch_sequence_id, 123U);
  EXPECT_EQ(view->samples, 1U);
  ASSERT_EQ(view->specific_force.size(), 3U);
  EXPECT_FLOAT_EQ(view->specific_force[0], 1.0F);
  EXPECT_FLOAT_EQ(view->specific_force[1], 2.0F);
  EXPECT_FLOAT_EQ(view->specific_force[2], 3.0F);
  ASSERT_EQ(view->rate_of_turn.size(), 3U);
  EXPECT_FLOAT_EQ(view->rate_of_turn[0], 4.0F);
  ASSERT_EQ(view->timestamps_ns.size(), 1U);
  EXPECT_EQ(view->timestamps_ns[0], 10LL * 1000000000LL + 20LL);
}

// encode() emits only RIP2, so the legacy uncompressed framing is built by
// hand. The CRC parameterization itself is pinned by the real-recording test.
TEST(Rip, Rip1UncompressedPayload) {
  proto::Packet wire;
  wire.mutable_msg()->PackFrom(make_range_image());
  const std::string payload = wire.SerializeAsString();

  std::vector<std::uint8_t> datagram(8 + payload.size() + 4);
  std::memcpy(datagram.data(), "RIP1", 4);
  write_le32_at(datagram, 4, static_cast<std::uint32_t>(datagram.size()));
  std::memcpy(datagram.data() + 8, payload.data(), payload.size());
  write_le32_at(
      datagram, datagram.size() - 4,
      static_cast<std::uint32_t>(::crc32(
          0L, datagram.data(), static_cast<uInt>(datagram.size() - 4))));

  Decoder decoder;
  const Decoded d = decoder.decode(datagram.data(), datagram.size());
  ASSERT_EQ(d.status, DecodeStatus::OK);
  const auto* view = std::get_if<RangeImageView>(&d.payload);
  ASSERT_NE(view, nullptr);
  EXPECT_EQ(view->header.sequence_id, 111U);
}

TEST(Rip, UnknownTypeReported) {
  // Header is a valid message from the proto file, but not a stream type.
  proto::Header header;
  header.set_sequence_id(7);
  const std::vector<std::uint8_t> packet = encode(header);

  Decoder decoder;
  const Decoded d = decoder.decode(packet.data(), packet.size());
  EXPECT_EQ(d.status, DecodeStatus::UNKNOWN_TYPE);
  EXPECT_TRUE(std::holds_alternative<std::monostate>(d.payload));
  EXPECT_NE(d.unknown_type_url.find("waterlinked.sonar.protocol.Header"),
            std::string::npos);
}

TEST(Rip, BadMagic) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  packet[0] = 'X';
  Decoder decoder;
  EXPECT_EQ(decoder.decode(packet.data(), packet.size()).status,
            DecodeStatus::BAD_MAGIC);
}

TEST(Rip, TruncatedInput) {
  const std::vector<std::uint8_t> packet = encode(make_range_image());
  Decoder decoder;
  EXPECT_EQ(decoder.decode(packet.data(), 0).status, DecodeStatus::BAD_LENGTH);
  EXPECT_EQ(decoder.decode(packet.data(), 4).status, DecodeStatus::BAD_LENGTH);
  EXPECT_EQ(decoder.decode(packet.data(), 11).status, DecodeStatus::BAD_LENGTH);
  EXPECT_EQ(decoder.decode(packet.data(), packet.size() - 1).status,
            DecodeStatus::BAD_LENGTH);
}

TEST(Rip, LengthFieldMismatch) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  const std::uint32_t total = read_le32_at(packet, 4);
  Decoder decoder;
  write_le32_at(packet, 4, total - 1);
  EXPECT_EQ(decoder.decode(packet.data(), packet.size()).status,
            DecodeStatus::BAD_LENGTH);
  write_le32_at(packet, 4, total + 1);
  EXPECT_EQ(decoder.decode(packet.data(), packet.size()).status,
            DecodeStatus::BAD_LENGTH);
}

TEST(Rip, CrcMismatch) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  packet[23] ^= 0x10;
  Decoder decoder;
  EXPECT_EQ(decoder.decode(packet.data(), packet.size()).status,
            DecodeStatus::CRC_MISMATCH);
}

TEST(Rip, ExtraDataAfterPacket) {
  std::vector<std::uint8_t> packet = encode(make_range_image());
  packet.push_back(0);
  // The length field no longer matches the datagram size.
  Decoder decoder;
  EXPECT_EQ(decoder.decode(packet.data(), packet.size()).status,
            DecodeStatus::BAD_LENGTH);
}

TEST(Rip, DecoderReusableAcrossPackets) {
  const std::vector<std::uint8_t> range_packet = encode(make_range_image());
  proto::ImuBatch batch;
  batch.set_batch_sequence_id(9);
  batch.set_samples(1);
  auto* ts = batch.add_timestamp();
  ts->set_seconds(1);
  ts->set_nanos(0);
  for (float v : {7.0F, 8.0F, 9.0F}) {
    batch.add_specific_force(v);
    batch.add_rate_of_turn(v);
  }
  const std::vector<std::uint8_t> imu_packet = encode(batch);

  Decoder decoder;
  const Decoded first =
      decoder.decode(range_packet.data(), range_packet.size());
  ASSERT_EQ(first.status, DecodeStatus::OK);
  const auto* img = std::get_if<RangeImageView>(&first.payload);
  ASSERT_NE(img, nullptr);
  EXPECT_EQ(img->header.sequence_id, 111U);
  EXPECT_EQ(img->image_pixel_data.size(), 256U * 64U);

  const Decoded second = decoder.decode(imu_packet.data(), imu_packet.size());
  ASSERT_EQ(second.status, DecodeStatus::OK);
  const auto* imu = std::get_if<ImuBatchView>(&second.payload);
  ASSERT_NE(imu, nullptr);
  EXPECT_EQ(imu->batch_sequence_id, 9U);
  ASSERT_EQ(imu->specific_force.size(), 3U);
  EXPECT_FLOAT_EQ(imu->specific_force[2], 9.0F);
}

TEST(Rip, DecodesRealSonarRecording) {
  const auto packets = read_recording("test/data/ship_short.sonar");
  ASSERT_FALSE(packets.empty());

  Decoder decoder;
  std::map<std::uint32_t, int> range_by_seq;
  std::map<std::uint32_t, int> bitmap_by_seq;
  for (const auto& packet : packets) {
    const Decoded d = decoder.decode(packet.data(), packet.size());
    ASSERT_NE(d.status, DecodeStatus::BAD_MAGIC);
    ASSERT_NE(d.status, DecodeStatus::CRC_MISMATCH);
    if (d.status != DecodeStatus::OK) {
      EXPECT_EQ(d.status, DecodeStatus::UNKNOWN_TYPE);
      continue;
    }
    if (const auto* range = std::get_if<RangeImageView>(&d.payload)) {
      range_by_seq[range->header.sequence_id]++;
    } else if (const auto* bitmap = std::get_if<BitmapImageView>(&d.payload)) {
      bitmap_by_seq[bitmap->header.sequence_id]++;
    }
  }

  EXPECT_EQ(range_by_seq.size(), 6U);
  EXPECT_EQ(bitmap_by_seq.size(), 6U);
  for (std::uint32_t seq = 4448; seq <= 4453; ++seq) {
    EXPECT_EQ(range_by_seq.count(seq), 1U) << "missing range image " << seq;
    EXPECT_EQ(bitmap_by_seq.count(seq), 1U) << "missing bitmap image " << seq;
  }
}

}  // namespace waterlinked::sonar::rip

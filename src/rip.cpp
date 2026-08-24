#include <snappy.h>
#include <zlib.h>

#include <cstring>
#include <string>
#include <vector>
#include <waterlinkedsonar/rip.hpp>

#include "rip_internal.hpp"

namespace waterlinked::sonar::rip {

namespace {

constexpr std::size_t MAGIC_SIZE = 4;
constexpr std::size_t LENGTH_SIZE = 4;
constexpr std::size_t CRC_SIZE = 4;
constexpr std::size_t MIN_PACKET_SIZE = MAGIC_SIZE + LENGTH_SIZE + CRC_SIZE;
constexpr std::size_t MAX_PACKET_SIZE = 65507;

std::uint32_t read_le32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

void write_le32(std::uint32_t v, std::uint8_t* p) {
  p[0] = static_cast<std::uint8_t>(v);
  p[1] = static_cast<std::uint8_t>(v >> 8);
  p[2] = static_cast<std::uint8_t>(v >> 16);
  p[3] = static_cast<std::uint8_t>(v >> 24);
}

// IEEE 802.3 CRC-32, as the docs specify and the device computes.
std::uint32_t crc32_ieee(const std::uint8_t* data, std::size_t size) {
  return static_cast<std::uint32_t>(::crc32(0L, data, static_cast<uInt>(size)));
}

std::int64_t to_ns(const google::protobuf::Timestamp& ts) {
  return (ts.seconds() * 1'000'000'000LL) + ts.nanos();
}

}  // namespace

const char* to_string(DecodeStatus status) {
  switch (status) {
    case DecodeStatus::OK:
      return "ok";
    case DecodeStatus::UNKNOWN_TYPE:
      return "unknown message type";
    case DecodeStatus::BAD_MAGIC:
      return "bad packet identifier";
    case DecodeStatus::BAD_LENGTH:
      return "bad packet length";
    case DecodeStatus::CRC_MISMATCH:
      return "CRC mismatch";
    case DecodeStatus::DECOMPRESS_FAILED:
      return "decompression failed";
    case DecodeStatus::PARSE_FAILED:
      return "protobuf parse failed";
  }
  return "?";
}

// Buffers and message objects reused across decodes; steady state, their
// capacity is already sufficient and decode() does not allocate.
struct Decoder::Impl {
  std::string proto_bytes;
  proto::Packet packet;
  proto::RangeImage range_image;
  proto::BitmapImageGreyscale8 bitmap_image;
  proto::ImuBatch imu_batch;
  std::vector<std::int64_t> imu_timestamps_ns;
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;
Decoder::Decoder(Decoder&&) noexcept = default;
Decoder& Decoder::operator=(Decoder&&) noexcept = default;

Decoded Decoder::decode(const std::uint8_t* data, std::size_t size) {
  Decoded result{};

  if (size < MIN_PACKET_SIZE) {
    result.status = DecodeStatus::BAD_LENGTH;
    return result;
  }

  const bool is_rip2 = std::memcmp(data, "RIP2", MAGIC_SIZE) == 0;
  const bool is_rip1 = std::memcmp(data, "RIP1", MAGIC_SIZE) == 0;
  if (!is_rip2 && !is_rip1) {
    result.status = DecodeStatus::BAD_MAGIC;
    return result;
  }

  // One packet per datagram, so the length field must equal the datagram
  // size exactly.
  const std::uint32_t total_length = read_le32(data + MAGIC_SIZE);
  if (total_length != size || total_length > MAX_PACKET_SIZE) {
    result.status = DecodeStatus::BAD_LENGTH;
    return result;
  }

  const std::uint32_t crc_received = read_le32(data + size - CRC_SIZE);
  const std::uint32_t crc_computed = crc32_ieee(data, size - CRC_SIZE);
  if (crc_received != crc_computed) {
    result.status = DecodeStatus::CRC_MISMATCH;
    return result;
  }

  const char* payload =
      reinterpret_cast<const char*>(data + MAGIC_SIZE + LENGTH_SIZE);
  const std::size_t payload_size = size - MIN_PACKET_SIZE;

  std::string& proto_bytes = impl_->proto_bytes;
  if (is_rip2) {
    if (!snappy::Uncompress(payload, payload_size, &proto_bytes)) {
      result.status = DecodeStatus::DECOMPRESS_FAILED;
      return result;
    }
  } else {
    proto_bytes.assign(payload, payload_size);
  }

  if (!impl_->packet.ParseFromString(proto_bytes)) {
    result.status = DecodeStatus::PARSE_FAILED;
    return result;
  }

  const google::protobuf::Any& any = impl_->packet.msg();
  if (any.Is<proto::RangeImage>()) {
    if (!any.UnpackTo(&impl_->range_image)) {
      result.status = DecodeStatus::PARSE_FAILED;
      return result;
    }
    const proto::RangeImage& msg = impl_->range_image;
    RangeImageView view;
    view.header.timestamp_ns = to_ns(msg.header().timestamp());
    view.header.sequence_id = msg.header().sequence_id();
    view.speed_of_sound = msg.speed_of_sound();
    view.range = msg.range();
    view.frequency = msg.frequency();
    view.width = msg.width();
    view.height = msg.height();
    view.fov_horizontal = msg.fov_horizontal();
    view.fov_vertical = msg.fov_vertical();
    view.image_pixel_scale = msg.image_pixel_scale();
    view.image_pixel_data = Span<const std::uint32_t>(
        msg.image_pixel_data().data(),
        static_cast<std::size_t>(msg.image_pixel_data().size()));
    result.payload = view;
  } else if (any.Is<proto::BitmapImageGreyscale8>()) {
    if (!any.UnpackTo(&impl_->bitmap_image)) {
      result.status = DecodeStatus::PARSE_FAILED;
      return result;
    }
    const proto::BitmapImageGreyscale8& msg = impl_->bitmap_image;
    BitmapImageView view;
    view.header.timestamp_ns = to_ns(msg.header().timestamp());
    view.header.sequence_id = msg.header().sequence_id();
    view.speed_of_sound = msg.speed_of_sound();
    view.range = msg.range();
    view.frequency = msg.frequency();
    view.type = msg.type() == proto::SIGNAL_STRENGTH_IMAGE
                    ? BitmapImageType::SIGNAL_STRENGTH
                    : BitmapImageType::SHADED;
    view.width = msg.width();
    view.height = msg.height();
    view.fov_horizontal = msg.fov_horizontal();
    view.fov_vertical = msg.fov_vertical();
    const std::string& bytes = msg.image_pixel_data();
    view.image_pixel_data = Span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    result.payload = view;
  } else if (any.Is<proto::ImuBatch>()) {
    if (!any.UnpackTo(&impl_->imu_batch)) {
      result.status = DecodeStatus::PARSE_FAILED;
      return result;
    }
    const proto::ImuBatch& msg = impl_->imu_batch;
    std::vector<std::int64_t>& timestamps = impl_->imu_timestamps_ns;
    timestamps.clear();
    timestamps.reserve(static_cast<std::size_t>(msg.timestamp_size()));
    for (const google::protobuf::Timestamp& ts : msg.timestamp()) {
      timestamps.push_back(to_ns(ts));
    }
    ImuBatchView view;
    view.batch_sequence_id = msg.batch_sequence_id();
    view.samples = msg.samples();
    view.timestamps_ns =
        Span<const std::int64_t>(timestamps.data(), timestamps.size());
    view.specific_force = Span<const float>(
        msg.specific_force().data(),
        static_cast<std::size_t>(msg.specific_force().size()));
    view.rate_of_turn =
        Span<const float>(msg.rate_of_turn().data(),
                          static_cast<std::size_t>(msg.rate_of_turn().size()));
    result.payload = view;
  } else {
    result.status = DecodeStatus::UNKNOWN_TYPE;
    result.unknown_type_url = any.type_url();
    return result;
  }

  result.status = DecodeStatus::OK;
  return result;
}

std::vector<std::uint8_t> encode(const google::protobuf::Message& msg) {
  proto::Packet packet;
  packet.mutable_msg()->PackFrom(msg);

  const std::string proto_bytes = packet.SerializeAsString();
  std::string compressed;
  snappy::Compress(proto_bytes.data(), proto_bytes.size(), &compressed);

  const std::size_t total = MIN_PACKET_SIZE + compressed.size();
  std::vector<std::uint8_t> out(total);
  std::memcpy(out.data(), "RIP2", MAGIC_SIZE);
  write_le32(static_cast<std::uint32_t>(total), out.data() + MAGIC_SIZE);
  std::memcpy(out.data() + MAGIC_SIZE + LENGTH_SIZE, compressed.data(),
              compressed.size());
  write_le32(crc32_ieee(out.data(), total - CRC_SIZE),
             out.data() + total - CRC_SIZE);
  return out;
}

}  // namespace waterlinked::sonar::rip

#include <snappy.h>

#include <algorithm>
#include <string>
#include <vector>
#include <waterlinkedsonar/rip/decoder.hpp>

#include "rip/framing.hpp"

namespace waterlinked::sonar::rip {

namespace {

template <typename T>
Span<const T> to_span(const google::protobuf::RepeatedField<T>& field) {
  return {field.data(), static_cast<std::size_t>(field.size())};
}

std::int64_t to_nanoseconds(const google::protobuf::Timestamp& timestamp) {
  return (timestamp.seconds() * 1'000'000'000LL) + timestamp.nanos();
}

template <typename Proto>
MessageHeader to_header(const Proto& message) {
  return {to_nanoseconds(message.header().timestamp()),
          message.header().sequence_id()};
}

RangeImageView to_view(const proto::RangeImage& image) {
  RangeImageView view;
  view.header = to_header(image);
  view.speed_of_sound = image.speed_of_sound();
  view.range = image.range();
  view.frequency = image.frequency();
  view.width = image.width();
  view.height = image.height();
  view.fov_horizontal = image.fov_horizontal();
  view.fov_vertical = image.fov_vertical();
  view.image_pixel_scale = image.image_pixel_scale();
  view.image_pixel_data = to_span(image.image_pixel_data());
  return view;
}

BitmapImageView to_view(const proto::BitmapImageGreyscale8& image) {
  BitmapImageView view;
  view.header = to_header(image);
  view.speed_of_sound = image.speed_of_sound();
  view.range = image.range();
  view.frequency = image.frequency();
  view.type = image.type() == proto::SHADED_IMAGE
                  ? BitmapImageType::SHADED
                  : BitmapImageType::SIGNAL_STRENGTH;
  view.width = image.width();
  view.height = image.height();
  view.fov_horizontal = image.fov_horizontal();
  view.fov_vertical = image.fov_vertical();
  const std::string& pixels = image.image_pixel_data();
  view.image_pixel_data = {reinterpret_cast<const std::uint8_t*>(pixels.data()),
                           pixels.size()};
  return view;
}

ImuBatchView to_view(const proto::ImuBatch& batch,
                     std::vector<std::int64_t>& timestamps_ns) {
  timestamps_ns.clear();
  for (const google::protobuf::Timestamp& timestamp : batch.timestamp()) {
    timestamps_ns.push_back(to_nanoseconds(timestamp));
  }
  ImuBatchView view;
  view.batch_sequence_id = batch.batch_sequence_id();
  view.samples = batch.samples();
  view.timestamps_ns = timestamps_ns;
  view.specific_force = to_span(batch.specific_force());
  view.rate_of_turn = to_span(batch.rate_of_turn());
  return view;
}

Decoded without_message(DecodeStatus status) {
  Decoded decoded;
  decoded.status = status;
  return decoded;
}

template <typename View>
Decoded success(const View& view) {
  Decoded decoded;
  decoded.status = DecodeStatus::OK;
  decoded.message = view;
  return decoded;
}

}  // namespace

const char* to_string(DecodeStatus status) noexcept {
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
  return "unknown";
}

struct Decoder::Impl {
  std::string payload;
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

Decoded Decoder::decode(Span<const std::uint8_t> packet) {
  if (packet.size() < MIN_PACKET_SIZE) {
    return without_message(DecodeStatus::BAD_LENGTH);
  }
  const auto magic = packet.first(MAGIC_SIZE);
  const bool is_rip2 = std::equal(magic.begin(), magic.end(), "RIP2");
  const bool is_rip1 = std::equal(magic.begin(), magic.end(), "RIP1");
  if (!is_rip2 && !is_rip1) {
    return without_message(DecodeStatus::BAD_MAGIC);
  }
  const std::uint32_t length = read_le32(&packet[MAGIC_SIZE]);
  if (length != packet.size() || length > MAX_PACKET_SIZE) {
    return without_message(DecodeStatus::BAD_LENGTH);
  }
  const std::size_t crc_offset = packet.size() - CRC_SIZE;
  if (read_le32(&packet[crc_offset]) != crc32(packet.first(crc_offset))) {
    return without_message(DecodeStatus::CRC_MISMATCH);
  }

  const auto payload = packet.subspan(HEADER_SIZE, crc_offset - HEADER_SIZE);
  const auto* payload_chars = reinterpret_cast<const char*>(payload.data());
  if (is_rip2) {
    if (!snappy::Uncompress(payload_chars, payload.size(), &impl_->payload)) {
      return without_message(DecodeStatus::DECOMPRESS_FAILED);
    }
  } else {
    impl_->payload.assign(payload_chars, payload.size());
  }
  if (!impl_->packet.ParseFromString(impl_->payload)) {
    return without_message(DecodeStatus::PARSE_FAILED);
  }

  const google::protobuf::Any& any = impl_->packet.msg();
  if (any.Is<proto::RangeImage>()) {
    if (!any.UnpackTo(&impl_->range_image)) {
      return without_message(DecodeStatus::PARSE_FAILED);
    }
    return success(to_view(impl_->range_image));
  }
  if (any.Is<proto::BitmapImageGreyscale8>()) {
    if (!any.UnpackTo(&impl_->bitmap_image)) {
      return without_message(DecodeStatus::PARSE_FAILED);
    }
    return success(to_view(impl_->bitmap_image));
  }
  if (any.Is<proto::ImuBatch>()) {
    if (!any.UnpackTo(&impl_->imu_batch)) {
      return without_message(DecodeStatus::PARSE_FAILED);
    }
    return success(to_view(impl_->imu_batch, impl_->imu_timestamps_ns));
  }
  Decoded unknown = without_message(DecodeStatus::UNKNOWN_TYPE);
  unknown.unknown_type_url = any.type_url();
  return unknown;
}

}  // namespace waterlinked::sonar::rip

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <waterlinkedsonar/udp/receiver.hpp>
#include <waterlinkedsonar/udp/socket.hpp>

#include "rip/framing.hpp"
#include "support/loopback.hpp"
#include "support/rip_encoder.hpp"

namespace waterlinked::sonar {
namespace {

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;
namespace proto = rip::proto;

proto::RangeImage make_range_image(std::uint32_t sequence_id) {
  proto::RangeImage image;
  image.mutable_header()->set_sequence_id(sequence_id);
  image.set_width(4);
  image.set_height(2);
  for (int i = 0; i < 8; ++i) {
    image.add_image_pixel_data(100);
  }
  return image;
}

proto::BitmapImageGreyscale8 make_bitmap_image(std::uint32_t sequence_id) {
  proto::BitmapImageGreyscale8 image;
  image.mutable_header()->set_sequence_id(sequence_id);
  image.set_width(4);
  image.set_height(2);
  image.set_image_pixel_data(std::string(8, '\x10'));
  return image;
}

bool eventually(const std::function<bool()>& condition) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(5ms);
  }
  return true;
}

TEST(Receiver, DispatchesMessagesAndCountsErrors) {
  UdpSocket socket = test::loopback_socket();
  const test::LoopbackSender sender(socket.local_port());
  Receiver receiver(std::move(socket));

  std::atomic<std::uint32_t> last_range_id{0};
  std::atomic<int> ranges{0};
  std::atomic<int> bitmaps{0};
  std::atomic<int> imu_batches{0};
  std::atomic<int> errors{0};
  receiver.on_range_image([&](const RangeImageView& image) {
    last_range_id = image.header.sequence_id;
    ++ranges;
  });
  receiver.on_bitmap_image([&](const BitmapImageView&) { ++bitmaps; });
  receiver.on_imu_batch([&](const ImuBatchView&) { ++imu_batches; });
  receiver.on_decode_error([&](rip::DecodeStatus) { ++errors; });
  receiver.start();

  sender.send(rip::encode(make_range_image(42)));
  sender.send(rip::encode(proto::Header()));
  std::vector<std::uint8_t> corrupted = rip::encode(make_range_image(43));
  corrupted.back() ^= 0xFFU;
  sender.send(corrupted);
  sender.send(rip::encode(make_bitmap_image(43)));
  sender.send(rip::encode(proto::ImuBatch()));
  sender.send(rip::encode(make_range_image(44)));

  ASSERT_TRUE(
      eventually([&] { return receiver.stats().datagrams_received == 6; }));
  ASSERT_TRUE(eventually([&] { return ranges == 2; }));
  EXPECT_EQ(bitmaps, 1);
  EXPECT_EQ(imu_batches, 1);
  EXPECT_EQ(errors, 1);
  EXPECT_EQ(last_range_id, 44U);

  const ReceiverStats stats = receiver.stats();
  EXPECT_EQ(stats.range_images, 2U);
  EXPECT_EQ(stats.bitmap_images, 1U);
  EXPECT_EQ(stats.imu_batches, 1U);
  EXPECT_EQ(stats.unknown_type, 1U);
  EXPECT_EQ(stats.decode_errors, 1U);
  EXPECT_EQ(stats.last_sequence_id, 44U);

  receiver.stop();
  EXPECT_FALSE(receiver.running());
  receiver.stop();
}

TEST(Receiver, RejectsChangesAfterStart) {
  Receiver receiver(test::loopback_socket());
  receiver.start();
  EXPECT_THROW(receiver.start(), std::logic_error);
  EXPECT_THROW(receiver.on_range_image({}), std::logic_error);
  EXPECT_THROW(receiver.on_bitmap_image({}), std::logic_error);
  EXPECT_THROW(receiver.on_imu_batch({}), std::logic_error);
  EXPECT_THROW(receiver.on_decode_error({}), std::logic_error);
  EXPECT_THROW(receiver.on_socket_error({}), std::logic_error);
}

TEST(Receiver, DropsOtherSenders) {
  UdpSocket socket = test::loopback_socket();
  const test::LoopbackSender sender(socket.local_port());
  ReceiverOptions options;
  options.source_ip = "192.0.2.1";
  Receiver receiver(std::move(socket), options);
  std::atomic<int> ranges{0};
  receiver.on_range_image([&](const RangeImageView&) { ++ranges; });
  receiver.start();

  sender.send(rip::encode(make_range_image(1)));
  ASSERT_TRUE(
      eventually([&] { return receiver.stats().filtered_source == 1; }));
  EXPECT_EQ(ranges, 0);
  EXPECT_EQ(receiver.stats().datagrams_received, 0U);
}

TEST(Receiver, StopFromCallbackEndsLoop) {
  UdpSocket socket = test::loopback_socket();
  const test::LoopbackSender sender(socket.local_port());
  Receiver receiver(std::move(socket));
  receiver.on_range_image([&](const RangeImageView&) { receiver.stop(); });
  receiver.start();

  sender.send(rip::encode(make_range_image(1)));
  ASSERT_TRUE(eventually([&] { return !receiver.running(); }));
  receiver.stop();
  EXPECT_EQ(receiver.stats().range_images, 1U);
}

}  // namespace
}  // namespace waterlinked::sonar

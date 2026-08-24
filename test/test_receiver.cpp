#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <waterlinkedsonar/messages.hpp>
#include <waterlinkedsonar/receiver.hpp>
#include <waterlinkedsonar/rip.hpp>
#include <waterlinkedsonar/udp_socket.hpp>

#include "rip_internal.hpp"

namespace waterlinked::sonar {

namespace {

proto::RangeImage make_range_image(std::uint32_t sequence_id) {
  proto::RangeImage img;
  img.mutable_header()->set_sequence_id(sequence_id);
  img.set_width(4);
  img.set_height(2);
  img.set_fov_horizontal(40.0F);
  img.set_fov_vertical(40.0F);
  img.set_image_pixel_scale(0.001F);
  for (int i = 0; i < 8; ++i) {
    img.add_image_pixel_data(100);
  }
  return img;
}

proto::BitmapImageGreyscale8 make_bitmap_image(std::uint32_t sequence_id) {
  proto::BitmapImageGreyscale8 img;
  img.mutable_header()->set_sequence_id(sequence_id);
  img.set_type(proto::SIGNAL_STRENGTH_IMAGE);
  img.set_width(4);
  img.set_height(2);
  img.set_image_pixel_data(std::string(8, '\x10'));
  return img;
}

proto::ImuBatch make_imu_batch() {
  proto::ImuBatch batch;
  batch.set_batch_sequence_id(7);
  batch.set_samples(1);
  batch.add_timestamp()->set_seconds(1);
  for (int i = 0; i < 3; ++i) {
    batch.add_specific_force(1.0F);
    batch.add_rate_of_turn(2.0F);
  }
  return batch;
}

class LoopbackSender {
 public:
  explicit LoopbackSender(std::uint16_t port) : port_(port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    EXPECT_GE(fd_, 0);
  }
  ~LoopbackSender() { ::close(fd_); }

  void send(const std::vector<std::uint8_t>& packet) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port_);
    inet_aton("127.0.0.1", &dest.sin_addr);
    const ssize_t n =
        ::sendto(fd_, packet.data(), packet.size(), 0,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    EXPECT_EQ(static_cast<std::size_t>(n), packet.size());
  }

 private:
  int fd_;
  std::uint16_t port_;
};

std::unique_ptr<UdpSocket> loopback_socket() {
  UdpSocketConfig config;
  config.mode = UdpSocketConfig::Mode::UNICAST;
  config.interface_ip = "127.0.0.1";
  config.port = 0;  // ephemeral
  return std::make_unique<UdpSocket>(config);
}

bool wait_for(const std::function<bool()>& condition,
              std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (condition()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return condition();
}

}  // namespace

TEST(UdpSocketTest, RecvTimesOutWhenIdle) {
  auto socket = loopback_socket();
  std::uint8_t buf[64];
  EXPECT_FALSE(socket->recv(buf, sizeof(buf), std::chrono::milliseconds(50)));
}

TEST(UdpSocketTest, RecvReturnsDatagramAndSource) {
  auto socket = loopback_socket();
  LoopbackSender sender(socket->local_port());
  sender.send({1, 2, 3});

  std::uint8_t buf[64];
  const auto datagram = socket->recv(buf, sizeof(buf), std::chrono::seconds(2));
  ASSERT_TRUE(datagram.has_value());
  EXPECT_EQ(datagram->size, 3U);
  EXPECT_EQ(datagram->source_ip, "127.0.0.1");
  EXPECT_EQ(buf[2], 3);
}

TEST(ReceiverTest, DispatchesDecodedMessagesAndCountsErrors) {
  auto socket = loopback_socket();
  const std::uint16_t port = socket->local_port();
  Receiver receiver(std::move(socket));

  std::atomic<std::uint32_t> last_range_seq{0};
  std::atomic<int> range_count{0};
  std::atomic<int> bitmap_count{0};
  std::atomic<int> imu_count{0};
  std::atomic<int> error_count{0};
  receiver.on_range_image([&](const RangeImageView& img) {
    last_range_seq.store(img.header.sequence_id);
    range_count.fetch_add(1);
  });
  receiver.on_bitmap_image(
      [&](const BitmapImageView&) { bitmap_count.fetch_add(1); });
  receiver.on_imu_batch([&](const ImuBatchView&) { imu_count.fetch_add(1); });
  receiver.on_decode_error(
      [&](rip::DecodeStatus) { error_count.fetch_add(1); });
  receiver.start();

  LoopbackSender sender(port);
  sender.send(rip::encode(make_range_image(42)));

  proto::Header unknown;
  unknown.set_sequence_id(1);
  sender.send(rip::encode(unknown));

  std::vector<std::uint8_t> corrupted = rip::encode(make_range_image(43));
  corrupted[corrupted.size() - 1] ^= 0xFF;
  sender.send(corrupted);

  sender.send(rip::encode(make_bitmap_image(43)));
  sender.send(rip::encode(make_imu_batch()));
  sender.send(rip::encode(make_range_image(44)));

  ASSERT_TRUE(wait_for([&] { return range_count.load() == 2; }));
  ASSERT_TRUE(wait_for([&] { return bitmap_count.load() == 1; }));
  ASSERT_TRUE(wait_for([&] { return imu_count.load() == 1; }));
  ASSERT_TRUE(wait_for([&] { return error_count.load() == 1; }));
  EXPECT_EQ(last_range_seq.load(), 44U);

  const ReceiverStats stats = receiver.stats();
  EXPECT_EQ(stats.datagrams_received, 6U);
  EXPECT_EQ(stats.range_images, 2U);
  EXPECT_EQ(stats.bitmap_images, 1U);
  EXPECT_EQ(stats.imu_batches, 1U);
  EXPECT_EQ(stats.unknown_type, 1U);
  EXPECT_EQ(stats.decode_errors, 1U);
  EXPECT_EQ(stats.last_sequence_id, 44U);

  receiver.stop();
  EXPECT_FALSE(receiver.running());
  receiver.stop();  // idempotent
}

TEST(ReceiverTest, RegistrationClosedAfterStart) {
  Receiver receiver(loopback_socket());
  receiver.start();
  EXPECT_THROW(receiver.on_range_image([](const RangeImageView&) {}),
               std::logic_error);
  EXPECT_THROW(receiver.on_bitmap_image([](const BitmapImageView&) {}),
               std::logic_error);
  EXPECT_THROW(receiver.on_imu_batch([](const ImuBatchView&) {}),
               std::logic_error);
  EXPECT_THROW(receiver.on_decode_error([](rip::DecodeStatus) {}),
               std::logic_error);
  EXPECT_THROW(receiver.on_socket_error([](const std::error_code&) {}),
               std::logic_error);
  receiver.stop();
}

TEST(ReceiverTest, FiltersOtherSources) {
  auto socket = loopback_socket();
  const std::uint16_t port = socket->local_port();
  ReceiverOptions options;
  options.source_ip = "192.0.2.1";  // never the loopback sender
  Receiver receiver(std::move(socket), options);

  std::atomic<int> range_count{0};
  receiver.on_range_image(
      [&](const RangeImageView&) { range_count.fetch_add(1); });
  receiver.start();

  LoopbackSender sender(port);
  sender.send(rip::encode(make_range_image(1)));

  ASSERT_TRUE(wait_for([&] { return receiver.stats().filtered_source == 1; }));
  EXPECT_EQ(range_count.load(), 0);
  EXPECT_EQ(receiver.stats().datagrams_received, 0U);
}

TEST(ReceiverTest, SingleCycle) {
  Receiver receiver(loopback_socket());
  receiver.start();
  receiver.stop();
  EXPECT_THROW(receiver.start(), std::logic_error);
}

}  // namespace waterlinked::sonar

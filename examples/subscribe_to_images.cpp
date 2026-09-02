// Joins the sonar's multicast group and prints each message it receives.
// Usage: subscribe_to_images [interface_ip]

#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <waterlinkedsonar/udp/receiver.hpp>

namespace sonar = waterlinked::sonar;

namespace {
volatile std::sig_atomic_t interrupted = 0;
}  // namespace

int main(int argc, char* argv[]) {
  std::signal(SIGINT, [](int) { interrupted = 1; });

  sonar::UdpSocketConfig config;
  if (argc > 1) {
    config.interface_ip = argv[1];
  }
  sonar::Receiver receiver{sonar::UdpSocket(config)};
  receiver.on_range_image([](const sonar::RangeImageView& image) {
    std::cout << "Range image " << image.header.sequence_id << ": "
              << image.width << "x" << image.height << ", " << image.range
              << " m\n";
  });
  receiver.on_bitmap_image([](const sonar::BitmapImageView& image) {
    std::cout << "Bitmap image " << image.header.sequence_id << ": "
              << image.width << "x" << image.height << "\n";
  });
  receiver.on_imu_batch([](const sonar::ImuBatchView& batch) {
    std::cout << "IMU batch " << batch.batch_sequence_id << ": "
              << batch.samples << " samples\n";
  });
  receiver.start();
  std::cout << "Listening on " << config.multicast_group << ":" << config.port
            << " via " << config.interface_ip << "; Ctrl-C stops\n";

  while (interrupted == 0 && receiver.running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  receiver.stop();

  const sonar::ReceiverStats stats = receiver.stats();
  std::cout << stats.datagrams_received << " datagrams: " << stats.range_images
            << " range, " << stats.bitmap_images << " bitmap, "
            << stats.imu_batches << " IMU, " << stats.unknown_type
            << " unknown, " << stats.decode_errors << " errors\n";
  return 0;
}

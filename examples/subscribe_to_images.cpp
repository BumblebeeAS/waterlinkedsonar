// Joins the sonar's multicast group and prints metadata for each image.
// Usage: subscribe_to_images [interface_ip]
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <waterlinkedsonar/receiver.hpp>

namespace {
volatile std::sig_atomic_t interrupted = 0;
}

int main(int argc, char* argv[]) {
  std::signal(SIGINT, [](int) { interrupted = 1; });

  waterlinked::sonar::UdpSocketConfig config;
  if (argc > 1) {
    config.interface_ip = argv[1];
  }

  waterlinked::sonar::Receiver receiver(
      std::make_unique<waterlinked::sonar::UdpSocket>(config));

  receiver.on_range_image([](const waterlinked::sonar::RangeImageView& img) {
    std::cout << "RangeImage seq=" << img.header.sequence_id << " " << img.width
              << "x" << img.height << " range=" << img.range << "m\n";
  });
  receiver.on_bitmap_image([](const waterlinked::sonar::BitmapImageView& img) {
    std::cout << "BitmapImage seq=" << img.header.sequence_id << " "
              << img.width << "x" << img.height << "\n";
  });
  receiver.on_imu_batch([](const waterlinked::sonar::ImuBatchView& batch) {
    std::cout << "ImuBatch seq=" << batch.batch_sequence_id
              << " samples=" << batch.samples << "\n";
  });

  receiver.start();
  std::cout << "Listening on multicast " << config.multicast_group << ":"
            << config.port << " (interface " << config.interface_ip
            << "), Ctrl-C to stop\n";

  while (interrupted == 0 && receiver.running()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  receiver.stop();

  const auto stats = receiver.stats();
  std::cout << "Received " << stats.datagrams_received << " datagrams ("
            << stats.range_images << " range, " << stats.bitmap_images
            << " bitmap, " << stats.imu_batches << " imu, "
            << stats.unknown_type << " unknown, " << stats.decode_errors
            << " errors)\n";
  return 0;
}

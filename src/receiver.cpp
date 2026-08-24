#include <stdexcept>
#include <utility>
#include <variant>
#include <waterlinkedsonar/receiver.hpp>
#include <waterlinkedsonar/types.hpp>

namespace waterlinked::sonar {

namespace {
constexpr std::chrono::milliseconds POLL_TIMEOUT{200};
}

Receiver::Receiver(std::unique_ptr<UdpSocket> socket, ReceiverOptions options)
    : socket_(std::move(socket)), options_(std::move(options)) {
  if (!socket_) {
    throw std::invalid_argument("Receiver requires a socket");
  }
}

Receiver::~Receiver() { stop(); }

void Receiver::on_range_image(std::function<void(const RangeImageView&)> cb) {
  if (started_) {
    throw std::logic_error("callback registration after start()");
  }
  range_callbacks_.push_back(std::move(cb));
}

void Receiver::on_bitmap_image(std::function<void(const BitmapImageView&)> cb) {
  if (started_) {
    throw std::logic_error("callback registration after start()");
  }
  bitmap_callbacks_.push_back(std::move(cb));
}

void Receiver::on_imu_batch(std::function<void(const ImuBatchView&)> cb) {
  if (started_) {
    throw std::logic_error("callback registration after start()");
  }
  imu_callbacks_.push_back(std::move(cb));
}

void Receiver::on_decode_error(std::function<void(rip::DecodeStatus)> cb) {
  if (started_) {
    throw std::logic_error("callback registration after start()");
  }
  decode_error_callbacks_.push_back(std::move(cb));
}

void Receiver::on_socket_error(std::function<void(const std::error_code&)> cb) {
  if (started_) {
    throw std::logic_error("callback registration after start()");
  }
  socket_error_callbacks_.push_back(std::move(cb));
}

void Receiver::start() {
  if (started_) {
    throw std::logic_error("Receiver is single-cycle; construct a new one");
  }
  started_ = true;
  running_.store(true);
  thread_ = std::thread([this] { receive_loop(); });
}

void Receiver::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool Receiver::running() const { return running_.load(); }

ReceiverStats Receiver::stats() const {
  ReceiverStats s;
  s.datagrams_received = datagrams_received_.load();
  s.range_images = range_images_.load();
  s.bitmap_images = bitmap_images_.load();
  s.imu_batches = imu_batches_.load();
  s.unknown_type = unknown_type_.load();
  s.decode_errors = decode_errors_.load();
  s.filtered_source = filtered_source_.load();
  s.last_sequence_id = last_sequence_id_.load();
  s.last_datagram_time = std::chrono::steady_clock::time_point(
      std::chrono::steady_clock::duration(last_datagram_ns_.load()));
  return s;
}

void Receiver::receive_loop() {
  std::vector<std::uint8_t> buf(MAX_DATAGRAM_SIZE);

  while (running_.load()) {
    std::optional<UdpSocket::Datagram> datagram;
    try {
      datagram = socket_->recv(buf.data(), buf.size(), POLL_TIMEOUT);
    } catch (const std::system_error& e) {
      for (const auto& cb : socket_error_callbacks_) {
        cb(e.code());
      }
      // The socket is unrecoverable; running() lets the owner notice.
      running_.store(false);
      return;
    }
    if (!datagram) {
      continue;
    }

    if (options_.source_ip && datagram->source_ip != *options_.source_ip) {
      filtered_source_.fetch_add(1);
      continue;
    }

    datagrams_received_.fetch_add(1);
    last_datagram_ns_.store(
        std::chrono::steady_clock::now().time_since_epoch().count());

    const rip::Decoded decoded = decoder_.decode(buf.data(), datagram->size);
    switch (decoded.status) {
      case rip::DecodeStatus::OK:
        break;
      case rip::DecodeStatus::UNKNOWN_TYPE:
        unknown_type_.fetch_add(1);
        continue;
      default:
        decode_errors_.fetch_add(1);
        for (const auto& cb : decode_error_callbacks_) {
          cb(decoded.status);
        }
        continue;
    }

    if (const auto* range = std::get_if<RangeImageView>(&decoded.payload)) {
      range_images_.fetch_add(1);
      last_sequence_id_.store(range->header.sequence_id);
      for (const auto& cb : range_callbacks_) {
        cb(*range);
      }
    } else if (const auto* bitmap =
                   std::get_if<BitmapImageView>(&decoded.payload)) {
      bitmap_images_.fetch_add(1);
      last_sequence_id_.store(bitmap->header.sequence_id);
      for (const auto& cb : bitmap_callbacks_) {
        cb(*bitmap);
      }
    } else if (const auto* imu = std::get_if<ImuBatchView>(&decoded.payload)) {
      imu_batches_.fetch_add(1);
      for (const auto& cb : imu_callbacks_) {
        cb(*imu);
      }
    }
  }
}

}  // namespace waterlinked::sonar

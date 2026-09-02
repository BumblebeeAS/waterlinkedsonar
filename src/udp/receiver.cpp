#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#include <waterlinkedsonar/udp/receiver.hpp>

namespace waterlinked::sonar {

namespace {

// Bounds how long stop() waits for the receive thread to notice.
constexpr std::chrono::milliseconds POLL_INTERVAL{200};

template <typename Callback, typename... Args>
void invoke_if_set(const Callback& callback, Args&&... args) {
  if (callback) {
    callback(std::forward<Args>(args)...);
  }
}

}  // namespace

Receiver::Receiver(UdpSocket socket, ReceiverOptions options)
    : socket_(std::move(socket)), options_(std::move(options)) {}

Receiver::~Receiver() { stop(); }

void Receiver::require_not_started() const {
  if (started_) {
    throw std::logic_error("Receiver callbacks must be set before start()");
  }
}

void Receiver::on_range_image(
    std::function<void(const RangeImageView&)> callback) {
  require_not_started();
  on_range_image_ = std::move(callback);
}

void Receiver::on_bitmap_image(
    std::function<void(const BitmapImageView&)> callback) {
  require_not_started();
  on_bitmap_image_ = std::move(callback);
}

void Receiver::on_imu_batch(std::function<void(const ImuBatchView&)> callback) {
  require_not_started();
  on_imu_batch_ = std::move(callback);
}

void Receiver::on_decode_error(
    std::function<void(rip::DecodeStatus)> callback) {
  require_not_started();
  on_decode_error_ = std::move(callback);
}

void Receiver::on_socket_error(
    std::function<void(const std::error_code&)> callback) {
  require_not_started();
  on_socket_error_ = std::move(callback);
}

void Receiver::start() {
  if (started_) {
    throw std::logic_error("Receiver can only be started once");
  }
  started_ = true;
  running_ = true;
  thread_ = std::thread([this] { receive_loop(); });
}

void Receiver::stop() {
  running_ = false;
  if (std::this_thread::get_id() != loop_thread_ && thread_.joinable()) {
    thread_.join();
  }
}

bool Receiver::running() const noexcept { return running_; }

ReceiverStats Receiver::stats() const noexcept {
  return {
      datagrams_received_,
      range_images_,
      bitmap_images_,
      imu_batches_,
      unknown_type_,
      decode_errors_,
      filtered_source_,
      last_sequence_id_,
      std::chrono::steady_clock::time_point(
          std::chrono::steady_clock::duration(last_datagram_ticks_)),
  };
}

void Receiver::receive_loop() {
  loop_thread_ = std::this_thread::get_id();
  std::vector<std::uint8_t> buffer(rip::MAX_PACKET_SIZE);
  while (running_) {
    std::optional<UdpSocket::Datagram> datagram;
    try {
      datagram = socket_.receive(buffer, POLL_INTERVAL);
    } catch (const std::system_error& e) {
      invoke_if_set(on_socket_error_, e.code());
      running_ = false;
      return;
    }
    if (!datagram) {
      continue;
    }
    if (options_.source_ip && datagram->source_ip != *options_.source_ip) {
      ++filtered_source_;
      continue;
    }
    ++datagrams_received_;
    last_datagram_ticks_ =
        std::chrono::steady_clock::now().time_since_epoch().count();
    dispatch(decoder_.decode(
        Span<const std::uint8_t>(buffer).first(datagram->size)));
  }
}

void Receiver::dispatch(const rip::Decoded& decoded) {
  if (decoded.status == rip::DecodeStatus::UNKNOWN_TYPE) {
    ++unknown_type_;
    return;
  }
  if (decoded.status != rip::DecodeStatus::OK) {
    ++decode_errors_;
    invoke_if_set(on_decode_error_, decoded.status);
    return;
  }
  if (const auto* image = std::get_if<RangeImageView>(&decoded.message)) {
    ++range_images_;
    last_sequence_id_ = image->header.sequence_id;
    invoke_if_set(on_range_image_, *image);
  } else if (const auto* bitmap =
                 std::get_if<BitmapImageView>(&decoded.message)) {
    ++bitmap_images_;
    last_sequence_id_ = bitmap->header.sequence_id;
    invoke_if_set(on_bitmap_image_, *bitmap);
  } else if (const auto* batch = std::get_if<ImuBatchView>(&decoded.message)) {
    ++imu_batches_;
    invoke_if_set(on_imu_batch_, *batch);
  }
}

}  // namespace waterlinked::sonar

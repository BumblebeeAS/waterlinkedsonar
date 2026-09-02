/// \file
/// Thread that receives, decodes and dispatches the sonar's data stream.

#ifndef WATERLINKEDSONAR_UDP_RECEIVER_HPP
#define WATERLINKEDSONAR_UDP_RECEIVER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <waterlinkedsonar/rip/decoder.hpp>
#include <waterlinkedsonar/rip/messages.hpp>
#include <waterlinkedsonar/udp/socket.hpp>

namespace waterlinked::sonar {

/// Filtering applied to datagrams before decoding.
struct ReceiverOptions {
  /// Sender to accept; datagrams from other senders are dropped. Needed when
  /// several sonars share the multicast group.
  std::optional<std::string> source_ip;
};

/// Counters accumulated since Receiver::start().
///
/// Each field is read atomically, but not all fields at the same instant.
struct ReceiverStats {
  std::uint64_t datagrams_received{0};  ///< Datagrams that passed the filter.
  std::uint64_t range_images{0};        ///< Decoded range images.
  std::uint64_t bitmap_images{0};       ///< Decoded bitmap images.
  std::uint64_t imu_batches{0};         ///< Decoded IMU batches.
  std::uint64_t unknown_type{0};        ///< Valid packets of unpublished types.
  std::uint64_t decode_errors{0};       ///< Datagrams that failed to decode.
  std::uint64_t filtered_source{0};  ///< Dropped by ReceiverOptions::source_ip.
  /// Sequence ID of the latest range or bitmap image.
  std::uint32_t last_sequence_id{0};
  /// Arrival of the latest datagram that passed the filter; the epoch before
  /// the first one.
  std::chrono::steady_clock::time_point last_datagram_time;
};

/// Receives datagrams on a thread of its own, decodes them and passes the
/// messages to the registered callbacks.
///
/// Callbacks run one at a time on the receive thread and must not throw. A
/// view passed to a callback is valid until the callback returns.
class Receiver {
 public:
  /// Takes over \p socket. No callbacks run before start(); datagrams that
  /// arrive earlier wait in the socket's receive buffer.
  explicit Receiver(UdpSocket socket, ReceiverOptions options = {});

  /// Calls stop(). Must not run on the receive thread, so a callback must
  /// not destroy the Receiver.
  ~Receiver();

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;
  Receiver(Receiver&&) = delete;
  Receiver& operator=(Receiver&&) = delete;

  /// Sets the callback for range images, replacing any earlier one.
  ///
  /// \throws std::logic_error after start().
  void on_range_image(std::function<void(const RangeImageView&)> callback);

  /// Sets the callback for bitmap images; see on_range_image().
  void on_bitmap_image(std::function<void(const BitmapImageView&)> callback);

  /// Sets the callback for IMU batches; see on_range_image().
  void on_imu_batch(std::function<void(const ImuBatchView&)> callback);

  /// Sets the callback for datagrams that fail to decode; see
  /// on_range_image(). Packets of unpublished types are only counted.
  void on_decode_error(std::function<void(rip::DecodeStatus)> callback);

  /// Sets the callback for the socket error that ends the receive thread;
  /// see on_range_image().
  void on_socket_error(std::function<void(const std::error_code&)> callback);

  /// Starts the receive thread. A Receiver starts at most once.
  ///
  /// \throws std::logic_error if it was started before.
  /// \throws std::system_error if the thread cannot be created.
  void start();

  /// Stops the receive thread and joins it unless called from a callback.
  /// Idempotent.
  void stop();

  /// Whether the receive thread is running; false once a socket error has
  /// ended it.
  [[nodiscard]] bool running() const noexcept;

  /// Current counters; callable from any thread.
  [[nodiscard]] ReceiverStats stats() const noexcept;

 private:
  void require_not_started() const;
  void receive_loop();
  void dispatch(const rip::Decoded& decoded);

  UdpSocket socket_;
  ReceiverOptions options_;
  rip::Decoder decoder_;

  std::function<void(const RangeImageView&)> on_range_image_;
  std::function<void(const BitmapImageView&)> on_bitmap_image_;
  std::function<void(const ImuBatchView&)> on_imu_batch_;
  std::function<void(rip::DecodeStatus)> on_decode_error_;
  std::function<void(const std::error_code&)> on_socket_error_;

  std::thread thread_;
  std::atomic<std::thread::id> loop_thread_;
  std::atomic<bool> running_{false};
  bool started_{false};

  std::atomic<std::uint64_t> datagrams_received_{0};
  std::atomic<std::uint64_t> range_images_{0};
  std::atomic<std::uint64_t> bitmap_images_{0};
  std::atomic<std::uint64_t> imu_batches_{0};
  std::atomic<std::uint64_t> unknown_type_{0};
  std::atomic<std::uint64_t> decode_errors_{0};
  std::atomic<std::uint64_t> filtered_source_{0};
  std::atomic<std::uint32_t> last_sequence_id_{0};
  std::atomic<std::chrono::steady_clock::rep> last_datagram_ticks_{0};
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_UDP_RECEIVER_HPP

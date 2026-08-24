#ifndef WATERLINKEDSONAR_RECEIVER_HPP
#define WATERLINKEDSONAR_RECEIVER_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <waterlinkedsonar/messages.hpp>
#include <waterlinkedsonar/rip.hpp>
#include <waterlinkedsonar/udp_socket.hpp>

namespace waterlinked::sonar {

struct ReceiverOptions {
  /** If set, datagrams from other senders are dropped (and counted). The
   * multicast group is shared, so two sonars on one network interleave
   * otherwise. */
  std::optional<std::string> source_ip;
};

/**
 * @brief Snapshot of the receive thread's counters, cumulative since start().
 *
 * Fields are sampled individually, so a snapshot taken mid-datagram may be
 * torn across fields; fine for diagnostics.
 */
struct ReceiverStats {
  std::uint64_t datagrams_received{0};
  std::uint64_t range_images{0};
  std::uint64_t bitmap_images{0};
  std::uint64_t imu_batches{0};
  std::uint64_t unknown_type{0};
  std::uint64_t decode_errors{0};
  std::uint64_t filtered_source{0};
  std::uint32_t last_sequence_id{0};
  std::chrono::steady_clock::time_point last_datagram_time;
};

/**
 * @brief Owns the receive thread: reads datagrams from the injected socket,
 * decodes them, and dispatches to the registered callbacks on that thread.
 *
 * Callbacks run sequentially on the receive thread. The views they receive
 * are valid for the duration of the callback — the storage behind them is not
 * reused until the callback returns, because decode and dispatch share the
 * one thread. Copy any data kept beyond the callback.
 */
class Receiver {
 public:
  /**
   * @param[in] socket Open socket to read from; must not be null.
   * @param[in] options Datagram filtering.
   * @throws std::invalid_argument When socket is null.
   */
  explicit Receiver(std::unique_ptr<UdpSocket> socket,
                    ReceiverOptions options = {});
  ~Receiver();  // stops if running

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;

  /**
   * @brief Registers a callback for decoded RangeImage messages.
   *
   * Registration for any callback type is only valid before start() and
   * throws std::logic_error afterwards; registering twice calls both.
   * Callbacks run sequentially on the receive thread and must not block for
   * long or throw.
   *
   * @param[in] cb Invoked once per decoded message.
   */
  void on_range_image(std::function<void(const RangeImageView&)> cb);

  /** @copydoc on_range_image() */
  void on_bitmap_image(std::function<void(const BitmapImageView&)> cb);

  /** @copydoc on_range_image() */
  void on_imu_batch(std::function<void(const ImuBatchView&)> cb);

  /**
   * @brief Registers a callback for datagrams that fail to decode.
   *
   * Same registration and execution rules as on_range_image(). UNKNOWN_TYPE
   * packets are counted, not reported here.
   *
   * @param[in] cb Invoked with the failure status.
   */
  void on_decode_error(std::function<void(rip::DecodeStatus)> cb);

  /**
   * @brief Registers a callback for the unrecoverable socket error that ends
   * the receive loop.
   *
   * Same registration and execution rules as on_range_image(). Invoked at
   * most once, from the receive thread, just before running() becomes false.
   *
   * @param[in] cb Invoked with the socket error.
   */
  void on_socket_error(std::function<void(const std::error_code&)> cb);

  /**
   * @brief Starts the receive thread.
   *
   * One start()/stop() cycle per instance; construct a new Receiver (with a
   * fresh socket) to restart.
   *
   * @throws std::logic_error On a second start().
   */
  void start();

  /** @brief Stops and joins the receive thread. Idempotent. */
  void stop();

  /** @return False before start, after stop, and after a socket error ends
   *          the receive loop on its own. */
  [[nodiscard]] bool running() const;

  /** @return Counter snapshot; callable from any thread. */
  [[nodiscard]] ReceiverStats stats() const;

 private:
  void receive_loop();

  std::unique_ptr<UdpSocket> socket_;
  ReceiverOptions options_;
  rip::Decoder decoder_;

  std::vector<std::function<void(const RangeImageView&)>> range_callbacks_;
  std::vector<std::function<void(const BitmapImageView&)>> bitmap_callbacks_;
  std::vector<std::function<void(const ImuBatchView&)>> imu_callbacks_;
  std::vector<std::function<void(rip::DecodeStatus)>> decode_error_callbacks_;
  std::vector<std::function<void(const std::error_code&)>>
      socket_error_callbacks_;

  std::thread thread_;
  std::atomic<bool> running_{false};
  bool started_{false};

  // Written only by the receive thread.
  std::atomic<std::uint64_t> datagrams_received_{0};
  std::atomic<std::uint64_t> range_images_{0};
  std::atomic<std::uint64_t> bitmap_images_{0};
  std::atomic<std::uint64_t> imu_batches_{0};
  std::atomic<std::uint64_t> unknown_type_{0};
  std::atomic<std::uint64_t> decode_errors_{0};
  std::atomic<std::uint64_t> filtered_source_{0};
  std::atomic<std::uint32_t> last_sequence_id_{0};
  std::atomic<std::chrono::steady_clock::time_point::rep> last_datagram_ns_{0};
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_RECEIVER_HPP

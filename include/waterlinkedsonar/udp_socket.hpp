#ifndef WATERLINKEDSONAR_UDP_SOCKET_HPP
#define WATERLINKEDSONAR_UDP_SOCKET_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <waterlinkedsonar/types.hpp>

namespace waterlinked::sonar {

struct UdpSocketConfig {
  enum class Mode : std::uint8_t { MULTICAST, UNICAST };
  Mode mode{Mode::MULTICAST};
  std::string multicast_group{MULTICAST_GROUP};
  /** For unicast this must match the destination port configured on the
   * sonar. */
  std::uint16_t port{MULTICAST_PORT};
  std::string interface_ip{"0.0.0.0"};  ///< Multicast join / bind interface.
  int receive_buffer_bytes{4 << 20};    ///< SO_RCVBUF request.
};

/**
 * @brief Receive-only UDP socket for the sonar's data stream. No knowledge
 * of RIP.
 */
class UdpSocket {
 public:
  /**
   * @brief Opens, binds, joins the multicast group (multicast mode), and
   * sets SO_REUSEADDR / SO_RCVBUF.
   *
   * @param[in] config Socket setup.
   * @throws std::system_error On failure.
   */
  explicit UdpSocket(UdpSocketConfig config);
  ~UdpSocket();

  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  struct Datagram {
    std::size_t size;
    std::string source_ip;
  };

  /**
   * @brief Blocks up to `timeout` for one datagram.
   *
   * @param[out] buf Receives the datagram bytes.
   * @param[in] buf_size Capacity of buf.
   * @param[in] timeout Maximum wait.
   * @return The datagram's size and source, or nullopt on timeout.
   * @throws std::system_error On socket errors.
   */
  std::optional<Datagram> recv(std::uint8_t* buf, std::size_t buf_size,
                               std::chrono::milliseconds timeout);

  /** @return The bound port; differs from the configured one only when it
   *          was 0. */
  [[nodiscard]] std::uint16_t local_port() const;

 private:
  int fd_{-1};
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_UDP_SOCKET_HPP

/// \file
/// Receive-only UDP socket for the sonar's data stream.

#ifndef WATERLINKEDSONAR_UDP_SOCKET_HPP
#define WATERLINKEDSONAR_UDP_SOCKET_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <waterlinkedsonar/span.hpp>

namespace waterlinked::sonar {

/// Multicast group the sonar streams to by default.
inline constexpr const char* MULTICAST_GROUP = "224.0.0.96";
/// Port the sonar streams to by default.
inline constexpr std::uint16_t MULTICAST_PORT = 4747;

/// Addressing, port and receive buffer of a UdpSocket.
struct UdpSocketConfig {
  /// Addressing of the received stream.
  enum class Mode : std::uint8_t {
    MULTICAST,  ///< Join multicast_group.
    UNICAST,    ///< Receive datagrams addressed to interface_ip.
  };

  Mode mode{Mode::MULTICAST};  ///< Multicast or unicast.
  /// Group joined in MULTICAST mode.
  std::string multicast_group{MULTICAST_GROUP};
  /// Local port. In UNICAST mode, the destination port set on the sonar.
  std::uint16_t port{MULTICAST_PORT};
  /// Interface that joins the group in MULTICAST mode; bind address in
  /// UNICAST mode.
  std::string interface_ip{"0.0.0.0"};
  /// Requested SO_RCVBUF size. The kernel caps it at net.core.rmem_max.
  int receive_buffer_bytes{4 << 20};
};

/// IPv4 UDP socket bound for the sonar's multicast or unicast stream.
class UdpSocket {
 public:
  /// Size and sender of a received datagram.
  struct Datagram {
    std::size_t size{0};    ///< Bytes written to the buffer.
    std::string source_ip;  ///< IPv4 address of the sender.
  };

  /// Binds the socket and, in MULTICAST mode, joins the group with
  /// SO_REUSEADDR set so that several receivers can share the port.
  ///
  /// \throws std::system_error on failure, including EADDRINUSE when another
  ///   socket holds the port in UNICAST mode.
  explicit UdpSocket(const UdpSocketConfig& config);

  /// Closes the socket.
  ~UdpSocket();
  /// Takes over the socket of \p other, which may only be destroyed or
  /// assigned to afterwards.
  UdpSocket(UdpSocket&& other) noexcept;
  /// Closes this socket and takes over the socket of \p other.
  UdpSocket& operator=(UdpSocket&& other) noexcept;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;

  /// Waits up to \p timeout for a datagram and copies it into \p buffer,
  /// truncating it to the buffer size.
  ///
  /// \returns The datagram, or std::nullopt on timeout.
  /// \throws std::system_error on a socket error.
  std::optional<Datagram> receive(Span<std::uint8_t> buffer,
                                  std::chrono::milliseconds timeout);

  /// Port the socket is bound to; useful when the configured port was 0.
  ///
  /// \throws std::system_error if the socket cannot be queried.
  [[nodiscard]] std::uint16_t local_port() const;

 private:
  int fd_{-1};
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_UDP_SOCKET_HPP

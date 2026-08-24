#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <waterlinkedsonar/udp_socket.hpp>

namespace waterlinked::sonar {

namespace {

[[noreturn]] void throw_errno(const char* what) {
  throw std::system_error(errno, std::generic_category(), what);
}

in_addr parse_ip(const std::string& ip, const char* what) {
  in_addr addr{};
  if (inet_aton(ip.c_str(), &addr) == 0) {
    throw std::system_error(EINVAL, std::generic_category(),
                            std::string(what) + " '" + ip + "'");
  }
  return addr;
}

}  // namespace

UdpSocket::UdpSocket(UdpSocketConfig config) {
  fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd_ < 0) {
    throw_errno("socket");
  }

  try {
    const int reuse = 1;
    if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
      throw_errno("setsockopt(SO_REUSEADDR)");
    }

    // Sonar bursts (~64 KB datagrams at up to 20 Hz per stream) overrun
    // default kernel buffers; the kernel silently caps this at
    // net.core.rmem_max, so failure to reach the request is not an error.
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &config.receive_buffer_bytes,
               sizeof(config.receive_buffer_bytes));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(config.port);
    if (config.mode == UdpSocketConfig::Mode::MULTICAST) {
      // Bind to INADDR_ANY; interface selection happens via the group join.
      bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
      bind_addr.sin_addr =
          parse_ip(config.interface_ip, "invalid interface IP");
    }
    if (bind(fd_, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) <
        0) {
      throw_errno("bind");
    }

    if (config.mode == UdpSocketConfig::Mode::MULTICAST) {
      ip_mreq mreq{};
      mreq.imr_multiaddr =
          parse_ip(config.multicast_group, "invalid multicast group");
      mreq.imr_interface =
          parse_ip(config.interface_ip, "invalid interface IP");
      if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) <
          0) {
        throw_errno("setsockopt(IP_ADD_MEMBERSHIP)");
      }
    }
  } catch (...) {
    ::close(fd_);
    throw;
  }
}

UdpSocket::~UdpSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

// Not const: receiving consumes the datagram from the socket's queue.
// NOLINTNEXTLINE(readability-make-member-function-const)
std::optional<UdpSocket::Datagram> UdpSocket::recv(
    std::uint8_t* buf, std::size_t buf_size,
    std::chrono::milliseconds timeout) {
  pollfd pfd{};
  pfd.fd = fd_;
  pfd.events = POLLIN;

  int ret = 0;
  do {
    ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
  } while (ret < 0 && errno == EINTR);
  if (ret < 0) {
    throw_errno("poll");
  }
  if (ret == 0) {
    return std::nullopt;
  }

  sockaddr_in src{};
  socklen_t src_len = sizeof(src);
  const ssize_t n = ::recvfrom(fd_, buf, buf_size, 0,
                               reinterpret_cast<sockaddr*>(&src), &src_len);
  if (n < 0) {
    throw_errno("recvfrom");
  }

  std::array<char, INET_ADDRSTRLEN> ip_str{};
  inet_ntop(AF_INET, &src.sin_addr, ip_str.data(), ip_str.size());
  return Datagram{static_cast<std::size_t>(n), ip_str.data()};
}

std::uint16_t UdpSocket::local_port() const {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
    throw_errno("getsockname");
  }
  return ntohs(addr.sin_port);
}

}  // namespace waterlinked::sonar

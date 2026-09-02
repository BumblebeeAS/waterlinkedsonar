#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <string>
#include <system_error>
#include <utility>
#include <waterlinkedsonar/udp/socket.hpp>

namespace waterlinked::sonar {

namespace {

[[noreturn]] void throw_errno(const char* operation) {
  throw std::system_error(errno, std::generic_category(), operation);
}

in_addr parse_ipv4(const std::string& address) {
  in_addr parsed{};
  if (inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
    throw std::system_error(EINVAL, std::generic_category(),
                            "invalid IPv4 address \"" + address + "\"");
  }
  return parsed;
}

template <typename T>
void set_option(int fd, int level, int name, const T& value,
                const char* operation) {
  if (setsockopt(fd, level, name, &value, sizeof(value)) < 0) {
    throw_errno(operation);
  }
}

}  // namespace

UdpSocket::UdpSocket(const UdpSocketConfig& config)
    : fd_(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {
  if (fd_ < 0) {
    throw_errno("socket");
  }
  try {
    const bool multicast = config.mode == UdpSocketConfig::Mode::MULTICAST;
    // Not set in UNICAST mode: Linux would hand each datagram to only one of
    // the sockets sharing the port, so a second receiver would silently
    // steal the stream instead of failing to bind.
    if (multicast) {
      set_option(fd_, SOL_SOCKET, SO_REUSEADDR, 1, "SO_REUSEADDR");
    }
    // Failing to get the full size is not an error; the kernel caps it.
    setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &config.receive_buffer_bytes,
               sizeof(config.receive_buffer_bytes));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    address.sin_addr = multicast ? in_addr{htonl(INADDR_ANY)}
                                 : parse_ipv4(config.interface_ip);
    if (bind(fd_, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) < 0) {
      throw_errno("bind");
    }

    if (multicast) {
      const ip_mreq membership{parse_ipv4(config.multicast_group),
                               parse_ipv4(config.interface_ip)};
      set_option(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, membership,
                 "IP_ADD_MEMBERSHIP");
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

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = std::exchange(other.fd_, -1);
  }
  return *this;
}

// NOLINTNEXTLINE(readability-make-member-function-const): consumes a datagram
std::optional<UdpSocket::Datagram> UdpSocket::receive(
    Span<std::uint8_t> buffer, std::chrono::milliseconds timeout) {
  pollfd request{fd_, POLLIN, 0};
  int ready = 0;
  do {
    ready = ::poll(&request, 1, static_cast<int>(timeout.count()));
  } while (ready < 0 && errno == EINTR);
  if (ready < 0) {
    throw_errno("poll");
  }
  if (ready == 0) {
    return std::nullopt;
  }

  sockaddr_in source{};
  socklen_t source_size = sizeof(source);
  const ssize_t size =
      ::recvfrom(fd_, buffer.data(), buffer.size(), 0,
                 reinterpret_cast<sockaddr*>(&source), &source_size);
  if (size < 0) {
    throw_errno("recvfrom");
  }
  std::array<char, INET_ADDRSTRLEN> source_ip{};
  inet_ntop(AF_INET, &source.sin_addr, source_ip.data(), source_ip.size());
  return Datagram{static_cast<std::size_t>(size), source_ip.data()};
}

std::uint16_t UdpSocket::local_port() const {
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  if (getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &size) < 0) {
    throw_errno("getsockname");
  }
  return ntohs(address.sin_port);
}

}  // namespace waterlinked::sonar

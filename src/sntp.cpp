#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <memory>
#include <waterlinkedsonar/sntp.hpp>

namespace waterlinked::sonar {

namespace {

constexpr std::size_t PACKET_SIZE = 48;
// Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01).
constexpr std::uint64_t NTP_UNIX_OFFSET = 2208988800ULL;

std::uint32_t read_be32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) |
         static_cast<std::uint32_t>(p[3]);
}

struct AddrInfoDeleter {
  void operator()(addrinfo* info) const { freeaddrinfo(info); }
};

class Fd {
 public:
  explicit Fd(int fd) : fd_(fd) {}
  ~Fd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  [[nodiscard]] int get() const { return fd_; }

 private:
  int fd_;
};

}  // namespace

std::optional<std::chrono::system_clock::time_point> sntp_query(
    const std::string& server, std::chrono::milliseconds timeout,
    std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* raw_info = nullptr;
  if (getaddrinfo(server.c_str(), std::to_string(port).c_str(), &hints,
                  &raw_info) != 0) {
    return std::nullopt;
  }
  const std::unique_ptr<addrinfo, AddrInfoDeleter> info(raw_info);

  const Fd fd(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  if (fd.get() < 0) {
    return std::nullopt;
  }
  // connect() so the kernel filters responses from other sources.
  if (::connect(fd.get(), info->ai_addr, info->ai_addrlen) < 0) {
    return std::nullopt;
  }

  std::array<std::uint8_t, PACKET_SIZE> packet{};
  packet[0] = 0x23;  // LI 0, version 4, mode 3 (client)
  if (::send(fd.get(), packet.data(), packet.size(), 0) !=
      static_cast<ssize_t>(packet.size())) {
    return std::nullopt;
  }

  pollfd pfd{};
  pfd.fd = fd.get();
  pfd.events = POLLIN;
  int ret = 0;
  do {
    ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
  } while (ret < 0 && errno == EINTR);
  if (ret <= 0) {
    return std::nullopt;
  }

  std::array<std::uint8_t, PACKET_SIZE> response{};
  const ssize_t n = ::recv(fd.get(), response.data(), response.size(), 0);
  if (n < static_cast<ssize_t>(PACKET_SIZE)) {
    return std::nullopt;
  }

  const std::uint8_t leap = response[0] >> 6;
  const std::uint8_t mode = response[0] & 0x07;
  const std::uint8_t stratum = response[1];
  // RFC 4330: discard alarm-condition (LI 3), kiss-of-death (stratum 0), and
  // invalid-stratum responses. An unsynchronized server answers with these
  // rather than staying silent, and its time must not be trusted.
  if (mode != 4 || leap == 3 || stratum == 0 || stratum > 15) {
    return std::nullopt;
  }

  const std::uint32_t transmit_seconds = read_be32(&response[40]);
  const std::uint32_t transmit_fraction = read_be32(&response[44]);
  if (transmit_seconds == 0) {
    return std::nullopt;
  }

  // NTP era 0 ends in 2036; smaller values are era 1 (RFC 4330 section 3).
  const std::uint64_t unix_seconds =
      transmit_seconds >= NTP_UNIX_OFFSET
          ? transmit_seconds - NTP_UNIX_OFFSET
          : transmit_seconds + (1ULL << 32) - NTP_UNIX_OFFSET;
  const auto micros = static_cast<std::int64_t>(
      (static_cast<std::uint64_t>(transmit_fraction) * 1000000ULL) >> 32);

  return std::chrono::system_clock::from_time_t(
             static_cast<std::time_t>(unix_seconds)) +
         std::chrono::microseconds(micros);
}

}  // namespace waterlinked::sonar

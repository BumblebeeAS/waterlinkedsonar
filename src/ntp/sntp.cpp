#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <memory>
#include <string>
#include <waterlinkedsonar/ntp/sntp.hpp>

namespace waterlinked::sonar {

namespace {

constexpr std::size_t PACKET_SIZE = 48;
constexpr std::size_t TRANSMIT_TIME_OFFSET = 40;
// LI 0 (no warning), VN 4, mode 3 (client).
constexpr std::uint8_t CLIENT_REQUEST = 0x23;
constexpr std::uint8_t SERVER_MODE = 4;
constexpr std::uint8_t LEAP_ALARM = 3;
constexpr std::uint8_t MAX_STRATUM = 15;
// Seconds from the NTP epoch (1900) to the Unix epoch (1970).
constexpr std::int64_t NTP_TO_UNIX_SECONDS = 2'208'988'800;

std::uint32_t read_be32(const std::uint8_t* bytes) {
  return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
         (static_cast<std::uint32_t>(bytes[1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[2]) << 8U) |
         static_cast<std::uint32_t>(bytes[3]);
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int fd) : fd_(fd) {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }
  FileDescriptor(const FileDescriptor&) = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  [[nodiscard]] int get() const { return fd_; }

 private:
  int fd_;
};

struct AddrinfoDeleter {
  void operator()(addrinfo* info) const { freeaddrinfo(info); }
};

}  // namespace

std::optional<std::chrono::system_clock::time_point> sntp_query(
    const std::string& server, std::chrono::milliseconds timeout,
    std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* resolved = nullptr;
  if (getaddrinfo(server.c_str(), std::to_string(port).c_str(), &hints,
                  &resolved) != 0) {
    return std::nullopt;
  }
  const std::unique_ptr<addrinfo, AddrinfoDeleter> address(resolved);

  const FileDescriptor socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  // connect() makes the kernel drop datagrams from other senders.
  if (socket.get() < 0 ||
      ::connect(socket.get(), address->ai_addr, address->ai_addrlen) < 0) {
    return std::nullopt;
  }

  std::array<std::uint8_t, PACKET_SIZE> packet{CLIENT_REQUEST};
  if (::send(socket.get(), packet.data(), packet.size(), 0) !=
      static_cast<ssize_t>(packet.size())) {
    return std::nullopt;
  }

  pollfd request{socket.get(), POLLIN, 0};
  int ready = 0;
  do {
    ready = ::poll(&request, 1, static_cast<int>(timeout.count()));
  } while (ready < 0 && errno == EINTR);
  if (ready <= 0 || ::recv(socket.get(), packet.data(), packet.size(), 0) !=
                        static_cast<ssize_t>(packet.size())) {
    return std::nullopt;
  }

  const std::uint8_t leap = packet[0] >> 6U;
  const std::uint8_t mode = packet[0] & 0x07U;
  const std::uint8_t stratum = packet[1];
  const std::uint32_t seconds = read_be32(&packet[TRANSMIT_TIME_OFFSET]);
  const std::uint32_t fraction = read_be32(&packet[TRANSMIT_TIME_OFFSET + 4]);
  if (mode != SERVER_MODE || leap == LEAP_ALARM || stratum == 0 ||
      stratum > MAX_STRATUM || seconds == 0) {
    return std::nullopt;
  }

  // Values below the offset belong to NTP era 1, which starts in 2036
  // (RFC 4330, section 3).
  std::int64_t unix_seconds =
      static_cast<std::int64_t>(seconds) - NTP_TO_UNIX_SECONDS;
  if (unix_seconds < 0) {
    unix_seconds += std::int64_t{1} << 32U;
  }
  const auto micros = static_cast<std::int64_t>(
      (static_cast<std::uint64_t>(fraction) * 1'000'000U) >> 32U);
  return std::chrono::system_clock::time_point(
      std::chrono::seconds(unix_seconds) + std::chrono::microseconds(micros));
}

}  // namespace waterlinked::sonar

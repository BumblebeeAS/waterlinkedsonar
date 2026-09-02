#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <optional>
#include <thread>
#include <waterlinkedsonar/ntp/sntp.hpp>

namespace waterlinked::sonar {
namespace {

constexpr std::uint64_t NTP_UNIX_OFFSET = 2208988800ULL;
constexpr std::time_t TEST_UNIX_TIME = 1786924800;  // 2026-08-17T00:00:00Z

void write_be32(std::uint32_t v, std::uint8_t* p) {
  p[0] = static_cast<std::uint8_t>(v >> 24);
  p[1] = static_cast<std::uint8_t>(v >> 16);
  p[2] = static_cast<std::uint8_t>(v >> 8);
  p[3] = static_cast<std::uint8_t>(v);
}

// Answers one SNTP request on localhost with a canned 48-byte response.
class FakeNtpServer {
 public:
  explicit FakeNtpServer(std::array<std::uint8_t, 48> response)
      : response_(response) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    inet_aton("127.0.0.1", &addr.sin_addr);
    EXPECT_EQ(::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    socklen_t len = sizeof(addr);
    EXPECT_EQ(getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] {
      std::array<std::uint8_t, 48> request{};
      sockaddr_in src{};
      socklen_t src_len = sizeof(src);
      const ssize_t n = ::recvfrom(fd_, request.data(), request.size(), 0,
                                   reinterpret_cast<sockaddr*>(&src), &src_len);
      if (n > 0) {
        ::sendto(fd_, response_.data(), response_.size(), 0,
                 reinterpret_cast<sockaddr*>(&src), src_len);
      }
    });
  }

  ~FakeNtpServer() {
    ::shutdown(fd_, SHUT_RDWR);
    thread_.join();
    ::close(fd_);
  }

  [[nodiscard]] std::uint16_t port() const { return port_; }

 private:
  std::array<std::uint8_t, 48> response_;
  int fd_;
  std::uint16_t port_;
  std::thread thread_;
};

std::array<std::uint8_t, 48> valid_response() {
  std::array<std::uint8_t, 48> r{};
  r[0] = 0x24;  // LI 0, version 4, mode 4 (server)
  r[1] = 2;     // stratum
  write_be32(static_cast<std::uint32_t>(TEST_UNIX_TIME + NTP_UNIX_OFFSET),
             &r[40]);
  write_be32(0x80000000, &r[44]);  // 0.5 s fraction
  return r;
}

std::optional<std::chrono::system_clock::time_point> query(
    const FakeNtpServer& server) {
  return sntp_query("127.0.0.1", std::chrono::seconds(2), server.port());
}

TEST(Sntp, ParsesServerTime) {
  const FakeNtpServer server(valid_response());
  const auto time = query(server);
  ASSERT_TRUE(time.has_value());
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          time.value_or(std::chrono::system_clock::time_point{})
                              .time_since_epoch())
                          .count();
  EXPECT_EQ(micros, (TEST_UNIX_TIME * 1000000LL) + 500000LL);
}

TEST(Sntp, TimesOutWithoutServer) {
  // A bound but silent socket: the query gets no response and no ICMP error.
  const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  inet_aton("127.0.0.1", &addr.sin_addr);
  ASSERT_EQ(::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
  socklen_t len = sizeof(addr);
  ASSERT_EQ(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);

  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(sntp_query("127.0.0.1", std::chrono::milliseconds(200),
                          ntohs(addr.sin_port)));
  EXPECT_GE(std::chrono::steady_clock::now() - start,
            std::chrono::milliseconds(200));
  ::close(fd);
}

TEST(Sntp, RejectsUnsynchronizedServer) {
  auto alarm = valid_response();
  alarm[0] = 0xE4;  // LI 3: clock not synchronized
  auto kiss_of_death = valid_response();
  kiss_of_death[1] = 0;  // stratum 0
  auto client_mode = valid_response();
  client_mode[0] = 0x23;  // mode 3
  auto no_time = valid_response();
  write_be32(0, &no_time[40]);

  for (const auto& response : {alarm, kiss_of_death, client_mode, no_time}) {
    const FakeNtpServer server(response);
    EXPECT_FALSE(query(server).has_value());
  }
}

}  // namespace
}  // namespace waterlinked::sonar

/// \file
/// UDP sender and receiver on localhost.

#ifndef WATERLINKEDSONAR_TEST_SUPPORT_LOOPBACK_HPP
#define WATERLINKEDSONAR_TEST_SUPPORT_LOOPBACK_HPP

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <vector>
#include <waterlinkedsonar/udp/socket.hpp>

namespace waterlinked::sonar::test {

/// Sends datagrams to a port on localhost.
class LoopbackSender {
 public:
  explicit LoopbackSender(std::uint16_t port)
      : fd_(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {
    destination_.sin_family = AF_INET;
    destination_.sin_port = htons(port);
    destination_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }
  ~LoopbackSender() { ::close(fd_); }
  LoopbackSender(const LoopbackSender&) = delete;
  LoopbackSender& operator=(const LoopbackSender&) = delete;

  void send(const std::vector<std::uint8_t>& datagram) const {
    EXPECT_EQ(::sendto(fd_, datagram.data(), datagram.size(), 0,
                       reinterpret_cast<const sockaddr*>(&destination_),
                       sizeof(destination_)),
              static_cast<ssize_t>(datagram.size()));
  }

 private:
  int fd_;
  sockaddr_in destination_{};
};

/// Unicast socket on localhost; port 0 picks a free port.
inline UdpSocket loopback_socket(std::uint16_t port = 0) {
  UdpSocketConfig config;
  config.mode = UdpSocketConfig::Mode::UNICAST;
  config.interface_ip = "127.0.0.1";
  config.port = port;
  return UdpSocket(config);
}

}  // namespace waterlinked::sonar::test

#endif  // WATERLINKEDSONAR_TEST_SUPPORT_LOOPBACK_HPP

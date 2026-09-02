#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <system_error>
#include <waterlinkedsonar/udp/socket.hpp>

#include "support/loopback.hpp"

namespace waterlinked::sonar {
namespace {

using std::chrono_literals::operator""ms;
using std::chrono_literals::operator""s;

TEST(UdpSocket, ReceiveTimesOutWhenIdle) {
  UdpSocket socket = test::loopback_socket();
  std::array<std::uint8_t, 64> buffer{};
  EXPECT_FALSE(socket.receive(buffer, 50ms).has_value());
}

TEST(UdpSocket, ReceiveReturnsDatagramAndSource) {
  UdpSocket socket = test::loopback_socket();
  const test::LoopbackSender sender(socket.local_port());
  sender.send({1, 2, 3});

  std::array<std::uint8_t, 64> buffer{};
  const auto received = socket.receive(buffer, 2s);
  ASSERT_TRUE(received.has_value());
  const UdpSocket::Datagram datagram = received.value_or(UdpSocket::Datagram{});
  EXPECT_EQ(datagram.size, 3U);
  EXPECT_EQ(datagram.source_ip, "127.0.0.1");
  EXPECT_EQ(buffer[2], 3);
}

TEST(UdpSocket, UnicastPortIsExclusive) {
  const UdpSocket first = test::loopback_socket();
  EXPECT_THROW(test::loopback_socket(first.local_port()), std::system_error);
}

}  // namespace
}  // namespace waterlinked::sonar

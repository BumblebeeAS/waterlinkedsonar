/// \file
/// Single SNTP query for checking an NTP server.

#ifndef WATERLINKEDSONAR_NTP_SNTP_HPP
#define WATERLINKEDSONAR_NTP_SNTP_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace waterlinked::sonar {

/// Asks an NTP server for its time with one SNTP (RFC 4330) request, e.g. to
/// check a server before configuring it on the sonar.
///
/// \param server Hostname or IPv4 address.
/// \param timeout Limit for the response.
/// \param port UDP port of the server.
/// \returns The server's transmit time, or std::nullopt if the server does
///   not resolve, no valid server reply arrives within \p timeout, or the
///   server is not synchronized (leap indicator 3, stratum outside 1-15, or
///   zero transmit time).
std::optional<std::chrono::system_clock::time_point> sntp_query(
    const std::string& server, std::chrono::milliseconds timeout,
    std::uint16_t port = 123);

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_NTP_SNTP_HPP

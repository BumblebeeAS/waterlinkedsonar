#ifndef WATERLINKEDSONAR_SNTP_HPP
#define WATERLINKEDSONAR_SNTP_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace waterlinked::sonar {

/**
 * @brief Asks an NTP server for its current time with a single SNTP
 * (RFC 4330) query.
 *
 * Intended as a pre-flight check of a time source before configuring it on a
 * device, not as a time synchronization mechanism.
 *
 * @param[in] server Hostname or IP address.
 * @param[in] timeout Maximum wait for the response.
 * @param[in] port NTP port.
 * @return The server's time, or nullopt if it does not answer within the
 *         timeout or answers with anything other than a valid, synchronized
 *         server response (mode 4, stratum 1-15, no alarm condition,
 *         non-zero transmit time).
 */
std::optional<std::chrono::system_clock::time_point> sntp_query(
    const std::string& server, std::chrono::milliseconds timeout,
    std::uint16_t port = 123);

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_SNTP_HPP

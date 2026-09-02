/// \file
/// Client for the Sonar 3D-15 HTTP API.

#ifndef WATERLINKEDSONAR_HTTP_CLIENT_HPP
#define WATERLINKEDSONAR_HTTP_CLIENT_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <waterlinkedsonar/http/errors.hpp>
#include <waterlinkedsonar/http/types.hpp>

namespace waterlinked::sonar {

/// Factory default IP address of the sonar.
inline constexpr const char* DEFAULT_IP = "192.168.194.96";

namespace detail {
class HttpTransport;
}  // namespace detail

/// Blocking client for the integration API
/// (https://docs.waterlinked.com/sonar-3d/sonar-3d-15-api/).
///
/// Not thread-safe. Every method that contacts the device throws HttpError
/// when a request fails or the device answers with a non-2xx status, and
/// ProtocolError when the response body does not match the API. Methods that
/// need newer firmware than the device runs throw VersionError without
/// contacting the device.
class SonarClient {
 public:
  /// Connects to the sonar and reads its firmware version.
  ///
  /// \param host IP address or hostname of the sonar.
  /// \param port Port of the HTTP API.
  /// \param timeout Limit for each request.
  /// \throws HttpError if the request fails or returns a non-2xx status.
  /// \throws ProtocolError if the response does not match the API or the
  ///   firmware version is not "major.minor.patch".
  /// \throws VersionError if the firmware is older than 1.5.1.
  explicit SonarClient(
      const std::string& host, std::uint16_t port = 80,
      std::chrono::milliseconds timeout = std::chrono::seconds(5));

  /// Closes the connection.
  ~SonarClient();
  /// Takes over the connection of \p other, which may only be destroyed or
  /// assigned to afterwards.
  SonarClient(SonarClient&& other) noexcept;
  /// Same as the move constructor.
  SonarClient& operator=(SonarClient&& other) noexcept;
  SonarClient(const SonarClient&) = delete;
  SonarClient& operator=(const SonarClient&) = delete;

  /// Firmware release read at construction, e.g. "1.8.0".
  [[nodiscard]] const std::string& firmware_version() const noexcept {
    return firmware_version_;
  }

  /// Device identity.
  About about();

  /// Subsystem health. Requires firmware 1.7.0.
  Status status();

  /// Water temperature at the sonar in degrees Celsius.
  double temperature();

  /// Whether acoustic imaging is on.
  bool acoustics_enabled();

  /// Turns acoustic imaging on or off.
  void set_acoustics_enabled(bool enabled);

  /// Imaging range limits.
  Range range();

  /// Sets the imaging range limits.
  void set_range(Range range);

  /// Speed of sound in m/s; 0 means the sonar derives it from salinity and
  /// temperature.
  double speed_of_sound();

  /// Sets the speed of sound in m/s; 0 derives it from salinity and
  /// temperature.
  void set_speed_of_sound(double speed);

  /// Imaging mode. Requires firmware 1.7.0.
  AcousticsMode mode();

  /// Sets the imaging mode. Requires firmware 1.7.0.
  void set_mode(AcousticsMode mode);

  /// Water salinity. Requires firmware 1.7.0.
  Salinity salinity();

  /// Sets the water salinity. Requires firmware 1.7.0.
  void set_salinity(Salinity salinity);

  /// Destination of the UDP stream.
  UdpConfig udp_config();

  /// Sets the destination of the UDP stream.
  void set_udp_config(const UdpConfig& config);

  /// Whether IMU batches are sent. Requires firmware 1.8.0.
  bool imu_batch_enabled();

  /// Turns IMU batch output on or off. Requires firmware 1.8.0.
  void set_imu_batch_enabled(bool enabled);

  /// State of the device clock. Requires firmware 1.7.1.
  TimeStatus time_status();

  /// Sets the device clock, rounded down to microseconds. Requires firmware
  /// 1.7.1.
  ///
  /// \throws TimeAlreadySynchronizedError if the clock is NTP-synchronized.
  void set_time_manual(std::chrono::system_clock::time_point time);

  /// NTP server address, or "auto". Requires firmware 1.7.1.
  std::string ntp_address();

  /// Sets the NTP server address, or "auto". Requires firmware 1.7.1.
  void set_ntp_address(const std::string& address);

  /// Makes the device sync its clock with the NTP server and waits for the
  /// outcome. Requires firmware 1.7.1.
  ///
  /// \param timeout Time the device spends on the sync. The request is
  ///   allowed this much longer than the client's request timeout.
  /// \throws ForceSyncOngoingError if another force-sync is running.
  ForceSyncResult force_sync_ntp(std::chrono::duration<double> timeout);

 private:
  void require_firmware(const char* operation, const char* min_version) const;

  std::unique_ptr<detail::HttpTransport> transport_;
  std::string firmware_version_;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_HTTP_CLIENT_HPP

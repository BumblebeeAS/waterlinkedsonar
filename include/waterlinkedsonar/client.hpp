#ifndef WATERLINKEDSONAR_CLIENT_HPP
#define WATERLINKEDSONAR_CLIENT_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <waterlinkedsonar/http_transport.hpp>
#include <waterlinkedsonar/types.hpp>

namespace waterlinked::sonar {

/**
 * @brief Client for the Sonar 3D-15 HTTP API
 * (https://docs.waterlinked.com/sonar-3d/sonar-3d-15-api/).
 *
 * Synchronous and not thread-safe; one caller at a time.
 *
 * All methods throw HttpError on transport failure or an unexpected HTTP
 * status, and ProtocolError on a response body the client cannot interpret.
 * Methods gated on firmware throw VersionError, before any network traffic,
 * when the device firmware (captured at construction) is too old.
 */
class SonarClient {
 public:
  /**
   * @brief Performs a GET /about to verify connectivity and capture the
   * firmware version.
   *
   * @param[in] transport Transport to issue requests through.
   * @throws VersionError If the firmware is older than 1.5.1, the first
   *         release with this API.
   */
  explicit SonarClient(std::unique_ptr<HttpTransport> transport);

  /**
   * @brief Convenience: constructs a CurlTransport internally.
   *
   * @param[in] ip Device IP address.
   * @param[in] port HTTP port.
   * @param[in] timeout Per-request timeout.
   */
  explicit SonarClient(
      const std::string& ip, std::uint16_t port = 80,
      std::chrono::milliseconds timeout = std::chrono::seconds(5));

  ~SonarClient();
  SonarClient(SonarClient&&) noexcept;
  SonarClient& operator=(SonarClient&&) noexcept;
  SonarClient(const SonarClient&) = delete;
  SonarClient& operator=(const SonarClient&) = delete;

  /** @return Firmware version captured at construction, e.g. "1.8.0". */
  [[nodiscard]] const std::string& firmware_version() const {
    return firmware_version_;
  }

  About about();
  Status status();       ///< Firmware >= 1.7.0; time entry present from 1.7.1.
  double temperature();  ///< Degrees Celsius.

  bool acoustics_enabled();
  void set_acoustics_enabled(bool enabled);

  Range range();
  void set_range(Range range);

  double speed_of_sound();  ///< m/s; 0.0 = automatic.
  /** @param[in] speed m/s; 0.0 enables automatic calculation from salinity
   *             and temperature. */
  void set_speed_of_sound(double speed);

  AcousticsMode mode();               ///< Firmware >= 1.7.0.
  void set_mode(AcousticsMode mode);  ///< Firmware >= 1.7.0.

  Salinity salinity();                   ///< Firmware >= 1.7.0.
  void set_salinity(Salinity salinity);  ///< Firmware >= 1.7.0.

  UdpConfig udp_config();
  void set_udp_config(const UdpConfig& config);

  bool imu_batch_enabled();                  ///< Firmware >= 1.8.0.
  void set_imu_batch_enabled(bool enabled);  ///< Firmware >= 1.8.0.

  TimeStatus time_status();  ///< Firmware >= 1.7.1.

  /**
   * @brief Sets the device clock. Firmware >= 1.7.1.
   *
   * @param[in] now Time to set.
   * @throws TimeAlreadySynchronizedError If the device is NTP-synced.
   */
  void set_time_manual(std::chrono::system_clock::time_point now);

  std::string ntp_address();                         ///< Firmware >= 1.7.1.
  void set_ntp_address(const std::string& address);  ///< Firmware >= 1.7.1.

  /**
   * @brief Triggers an NTP sync and blocks up to `timeout` while the device
   * attempts it. Firmware >= 1.7.1.
   *
   * @param[in] timeout Device-side sync timeout; the HTTP request is held
   *            open slightly longer.
   * @return Whether the sync completed, plus the resulting time status.
   * @throws ForceSyncOngoingError If another force-sync is running.
   */
  ForceSyncResult force_sync_ntp(std::chrono::seconds timeout);

 private:
  void require_firmware(const char* what, const char* min_version) const;

  std::unique_ptr<HttpTransport> transport_;
  std::string firmware_version_;
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_CLIENT_HPP

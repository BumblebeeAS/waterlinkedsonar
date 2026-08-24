#ifndef WATERLINKEDSONAR_TYPES_HPP
#define WATERLINKEDSONAR_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace waterlinked::sonar {

inline constexpr const char* DEFAULT_IP = "192.168.194.96";
inline constexpr const char* MULTICAST_GROUP = "224.0.0.96";
inline constexpr std::uint16_t MULTICAST_PORT = 4747;
inline constexpr std::size_t MAX_DATAGRAM_SIZE = 65507;

/** @brief Device identity, from GET /about. */
struct About {
  std::string chipid;
  int hardware_revision;
  bool is_ready;
  int product_id;
  std::string product_name;
  std::string variant;
  std::string version;        ///< Full build string.
  std::string version_short;  ///< e.g. "1.8.0".
};

/** @brief One subsystem's health, from GET /status. */
struct StatusEntry {
  std::string id;
  std::string message;
  std::string status;  ///< "ok", "warning", or "error".
  bool operational;
};

struct Status {
  StatusEntry api;
  StatusEntry temperature;
  StatusEntry systems_check;
  std::optional<StatusEntry> time;  ///< Present from firmware 1.7.1.
};

enum class AcousticsMode : std::uint8_t { LOW_FREQUENCY, HIGH_FREQUENCY };
enum class Salinity : std::uint8_t { SALT, FRESH };

/** @brief Imaging range limits, meters. */
struct Range {
  double min;
  double max;
};

/** @brief How the sonar streams data, from GET/POST /udp. */
struct UdpConfig {
  enum class Mode : std::uint8_t { MULTICAST, UNICAST, DISABLED };
  Mode mode{Mode::MULTICAST};
  std::string unicast_destination_ip;  ///< Used only when UNICAST.
  std::uint16_t unicast_destination_port{0};
};

/** @brief Device clock state, from GET /time/status. */
struct TimeStatus {
  std::chrono::system_clock::time_point system_time;
  bool ntp_synced;
  std::string ntp_synced_to;
  std::optional<std::int64_t> ntp_seconds_since_last_sync;
};

/** @brief Outcome of a forced NTP sync. */
struct ForceSyncResult {
  bool success;
  std::string message;
  TimeStatus status;
};

/**
 * @brief Transport failure or non-2xx response.
 */
class HttpError : public std::runtime_error {
 public:
  /**
   * @param[in] what Description.
   * @param[in] status_code HTTP status, or 0 when the request never
   *            completed (connect failure, timeout).
   */
  HttpError(const std::string& what, long status_code)
      : std::runtime_error(what), status_code_(status_code) {}
  [[nodiscard]] long status_code() const { return status_code_; }

 private:
  long status_code_;
};

/** @brief The device firmware is older than the endpoint requires. */
class VersionError : public std::runtime_error {
 public:
  VersionError(const std::string& what, const std::string& min_version,
               const std::string& device_version)
      : std::runtime_error(what + " requires Sonar 3D-15 release " +
                           min_version + " or newer; device is " +
                           device_version) {}
};

/** @brief The device answered with a body the client cannot interpret. */
class ProtocolError : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

/** @brief set_time_manual() while the device is already NTP-synchronized. */
class TimeAlreadySynchronizedError : public HttpError {
 public:
  TimeAlreadySynchronizedError()
      : HttpError("system time is already synchronized", 409) {}
};

/** @brief force_sync_ntp() while another force-sync is running. */
class ForceSyncOngoingError : public HttpError {
 public:
  ForceSyncOngoingError() : HttpError("a force-sync is already ongoing", 409) {}
};

const char* to_string(AcousticsMode mode);
const char* to_string(Salinity salinity);
const char* to_string(UdpConfig::Mode mode);

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_TYPES_HPP

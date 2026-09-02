/// \file
/// Data types of the Sonar 3D-15 HTTP API.

#ifndef WATERLINKEDSONAR_HTTP_TYPES_HPP
#define WATERLINKEDSONAR_HTTP_TYPES_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace waterlinked::sonar {

/// Device identity.
struct About {
  std::string chipid;         ///< Chip ID, e.g. "0x12345678".
  int hardware_revision{0};   ///< Hardware revision, e.g. 6.
  bool is_ready{false};       ///< Whether the sonar has finished booting.
  int product_id{0};          ///< Product ID, e.g. 21045.
  std::string product_name;   ///< Product name, e.g. "Sonar 3D-15".
  std::string variant;        ///< Product variant; may be empty.
  std::string version;        ///< Full build string.
  std::string version_short;  ///< Release number, e.g. "1.8.0".
};

/// Health of one subsystem.
struct StatusEntry {
  std::string id;           ///< Status ID, e.g. "api-normal".
  std::string message;      ///< Human-readable description.
  std::string status;       ///< "ok", "warning" or "error".
  bool operational{false};  ///< Whether the subsystem works.
};

/// Health of the device subsystems.
struct Status {
  StatusEntry api;                  ///< Integration API.
  StatusEntry temperature;          ///< Internal temperature.
  StatusEntry systems_check;        ///< Internal processing.
  std::optional<StatusEntry> time;  ///< System clock; firmware 1.7.1 or newer.
};

/// Imaging mode, which sets the field of view and the image rate.
enum class AcousticsMode : std::uint8_t {
  LOW_FREQUENCY,   ///< 90x40 degrees at 5 Hz.
  HIGH_FREQUENCY,  ///< 40x40 degrees at 20 Hz.
};

/// Water salinity used by the automatic speed of sound.
enum class Salinity : std::uint8_t {
  SALT,   ///< Seawater.
  FRESH,  ///< Fresh water.
};

/// Imaging range limits in meters.
struct Range {
  double min{0.0};  ///< Nearest distance imaged.
  double max{0.0};  ///< Farthest distance imaged.
};

/// Destination of the sonar's UDP stream.
struct UdpConfig {
  /// Addressing of the stream.
  enum class Mode : std::uint8_t {
    MULTICAST,  ///< To the multicast group 224.0.0.96:4747.
    UNICAST,    ///< To the unicast destination.
    DISABLED,   ///< Not sent.
  };

  Mode mode{Mode::MULTICAST};                 ///< Where the stream is sent.
  std::string unicast_destination_ip;         ///< Used in UNICAST mode only.
  std::uint16_t unicast_destination_port{0};  ///< Used in UNICAST mode only.
};

/// State of the device clock.
struct TimeStatus {
  std::chrono::system_clock::time_point system_time;  ///< Device time.
  bool ntp_synced{false};     ///< Whether the clock is NTP-synchronized.
  std::string ntp_synced_to;  ///< Server of the latest sync.
  /// Age of the latest sync; empty when the device reports none.
  std::optional<std::int64_t> ntp_seconds_since_last_sync;
};

/// Outcome of a forced NTP sync.
struct ForceSyncResult {
  bool success{false};  ///< Whether the clock synced.
  std::string message;  ///< Human-readable outcome.
  TimeStatus status;    ///< Device clock after the attempt.
};

/// API name of \p mode: "low-frequency" or "high-frequency".
const char* to_string(AcousticsMode mode) noexcept;

/// API name of \p salinity: "salt" or "fresh".
const char* to_string(Salinity salinity) noexcept;

/// API name of \p mode: "multicast", "unicast" or "disabled".
const char* to_string(UdpConfig::Mode mode) noexcept;

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_HTTP_TYPES_HPP

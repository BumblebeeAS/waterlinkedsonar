/// \file
/// Exceptions thrown by SonarClient.

#ifndef WATERLINKEDSONAR_HTTP_ERRORS_HPP
#define WATERLINKEDSONAR_HTTP_ERRORS_HPP

#include <stdexcept>
#include <string>

namespace waterlinked::sonar {

/// Request failure or non-2xx HTTP response.
class HttpError : public std::runtime_error {
 public:
  /// \param what Description of the failed request.
  /// \param status_code HTTP status, or 0 when no response arrived.
  HttpError(const std::string& what, int status_code)
      : std::runtime_error(what), status_code_(status_code) {}

  /// HTTP status of the response, or 0 when no response arrived.
  [[nodiscard]] int status_code() const noexcept { return status_code_; }

 private:
  int status_code_;
};

/// The device firmware is older than the operation requires.
class VersionError : public std::runtime_error {
 public:
  /// \param what Operation that was refused.
  /// \param min_version Oldest firmware that supports \p what.
  /// \param device_version Firmware of the device.
  VersionError(const std::string& what, const std::string& min_version,
               const std::string& device_version)
      : std::runtime_error(what + " requires Sonar 3D-15 release " +
                           min_version + " or newer; device is " +
                           device_version) {}
};

/// The device answered with a body that does not match the API.
class ProtocolError : public std::runtime_error {
 public:
  /// \param what Description of the unexpected response.
  explicit ProtocolError(const std::string& what) : std::runtime_error(what) {}
};

/// SonarClient::set_time_manual() was called while the device clock is
/// NTP-synchronized (HTTP 409).
class TimeAlreadySynchronizedError : public HttpError {
 public:
  /// Sets status_code() to 409.
  TimeAlreadySynchronizedError()
      : HttpError("system time is already synchronized", 409) {}
};

/// SonarClient::force_sync_ntp() was called while another force-sync is
/// running (HTTP 409).
class ForceSyncOngoingError : public HttpError {
 public:
  /// Sets status_code() to 409.
  ForceSyncOngoingError() : HttpError("a force-sync is already ongoing", 409) {}
};

}  // namespace waterlinked::sonar

#endif  // WATERLINKEDSONAR_HTTP_ERRORS_HPP

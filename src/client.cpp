#include <array>
#include <cstdio>
#include <ctime>
#include <nlohmann/json.hpp>
#include <waterlinkedsonar/client.hpp>
#include <waterlinkedsonar/curl_transport.hpp>

#include "semver.hpp"

namespace waterlinked::sonar {

namespace {

using nlohmann::json;

constexpr const char* API_BASE = "/api/v1/integration";

// The first firmware release with the integration API.
constexpr const char* MIN_FIRMWARE = "1.5.1";

std::chrono::system_clock::time_point parse_rfc3339(const std::string& s) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int consumed = 0;
  // Field count is checked; the fixed-width digit fields cannot overflow int.
  // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day,
                  &hour, &minute, &second, &consumed) != 6) {
    throw ProtocolError("invalid RFC3339 timestamp: '" + s + "'");
  }
  auto pos = static_cast<std::size_t>(consumed);

  std::int64_t micros = 0;
  if (pos < s.size() && s[pos] == '.') {
    ++pos;
    std::int64_t scale = 100000;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
      if (scale > 0) {
        micros += (s[pos] - '0') * scale;
        scale /= 10;
      }
      ++pos;
    }
  }

  long offset_seconds = 0;
  if (pos < s.size() && (s[pos] == 'Z' || s[pos] == 'z')) {
    ++pos;
  } else if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
    int off_hour = 0;
    int off_min = 0;
    // Field count is checked; two-digit offset fields cannot overflow int.
    // NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion)
    if (std::sscanf(s.c_str() + pos, "%3d:%2d", &off_hour, &off_min) != 2) {
      throw ProtocolError("invalid RFC3339 timestamp: '" + s + "'");
    }
    offset_seconds =
        (off_hour * 3600L) + ((off_hour < 0 ? -off_min : off_min) * 60L);
    pos = s.size();
  }
  if (pos != s.size()) {
    throw ProtocolError("invalid RFC3339 timestamp: '" + s + "'");
  }

  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  const std::time_t utc = timegm(&tm) - offset_seconds;
  return std::chrono::system_clock::from_time_t(utc) +
         std::chrono::microseconds(micros);
}

std::string format_rfc3339_utc(std::chrono::system_clock::time_point tp) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(tp);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                          tp - std::chrono::system_clock::from_time_t(seconds))
                          .count();
  std::tm tm{};
  gmtime_r(&seconds, &tm);
  std::array<char, 40> buf{};
  std::snprintf(buf.data(), buf.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%06ldZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<long>(micros));
  return buf.data();
}

json parse_body(const std::string& path, const HttpTransport::Response& resp) {
  if (resp.status == 204 || resp.body.empty()) {
    return {};  // null
  }
  json parsed = json::parse(resp.body, nullptr, false);
  if (parsed.is_discarded()) {
    throw ProtocolError(path + std::string(": response is not valid JSON"));
  }
  return parsed;
}

StatusEntry parse_status_entry(const std::string& path, const json& j) {
  if (!j.is_object() || !j.value("id", json()).is_string() ||
      !j.value("message", json()).is_string() ||
      !j.value("status", json()).is_string() ||
      !j.value("operational", json()).is_boolean()) {
    throw ProtocolError(path + std::string(": unexpected status entry"));
  }
  return StatusEntry{j["id"], j["message"], j["status"], j["operational"]};
}

TimeStatus parse_time_status(const std::string& path, const json& j) {
  if (!j.is_object() || !j.value("system_time", json()).is_string() ||
      !j.value("ntp_synced", json()).is_boolean() ||
      !j.value("ntp_synced_to", json()).is_string()) {
    throw ProtocolError(path + std::string(": unexpected time status"));
  }
  TimeStatus ts;
  ts.system_time = parse_rfc3339(j["system_time"]);
  ts.ntp_synced = j["ntp_synced"];
  ts.ntp_synced_to = j["ntp_synced_to"];
  const auto& since = j.value("ntp_seconds_since_last_sync", json());
  if (since.is_number()) {
    ts.ntp_seconds_since_last_sync = since.get<std::int64_t>();
  }
  return ts;
}

}  // namespace

SonarClient::SonarClient(std::unique_ptr<HttpTransport> transport)
    : transport_(std::move(transport)) {
  firmware_version_ = about().version_short;
  if (internal::semver_is_less_than(firmware_version_, MIN_FIRMWARE)) {
    throw VersionError("SonarClient", MIN_FIRMWARE, firmware_version_);
  }
}

SonarClient::SonarClient(const std::string& ip, std::uint16_t port,
                         std::chrono::milliseconds timeout)
    : SonarClient(std::make_unique<CurlTransport>(ip, port, timeout)) {}

SonarClient::~SonarClient() = default;
SonarClient::SonarClient(SonarClient&&) noexcept = default;
SonarClient& SonarClient::operator=(SonarClient&&) noexcept = default;

void SonarClient::require_firmware(const char* what,
                                   const char* min_version) const {
  if (internal::semver_is_less_than(firmware_version_, min_version)) {
    throw VersionError(what, min_version, firmware_version_);
  }
}

namespace {

json get_json(HttpTransport& transport, const std::string& path) {
  const auto resp = transport.get(path);
  if (resp.status < 200 || resp.status >= 300) {
    throw HttpError("GET " + path + " returned " + std::to_string(resp.status),
                    resp.status);
  }
  return parse_body(path, resp);
}

json post_json(
    HttpTransport& transport, const std::string& path, const json& payload,
    std::optional<std::chrono::milliseconds> timeout_override = std::nullopt) {
  const auto resp = transport.post(path, payload.dump(), timeout_override);
  if (resp.status < 200 || resp.status >= 300) {
    throw HttpError("POST " + path + " returned " +
                        std::to_string(resp.status) +
                        (resp.body.empty() ? "" : ": " + resp.body),
                    resp.status);
  }
  return parse_body(path, resp);
}

}  // namespace

About SonarClient::about() {
  const std::string path = std::string(API_BASE) + "/about";
  const json j = get_json(*transport_, path);
  if (!j.is_object() || !j.value("chipid", json()).is_string() ||
      !j.value("hardware_revision", json()).is_number_integer() ||
      !j.value("is_ready", json()).is_boolean() ||
      !j.value("product_id", json()).is_number_integer() ||
      !j.value("product_name", json()).is_string() ||
      !j.value("variant", json()).is_string() ||
      !j.value("version", json()).is_string() ||
      !j.value("version_short", json()).is_string()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return About{j["chipid"],     j["hardware_revision"], j["is_ready"],
               j["product_id"], j["product_name"],      j["variant"],
               j["version"],    j["version_short"]};
}

Status SonarClient::status() {
  require_firmware("status", "1.7.0");
  const std::string path = std::string(API_BASE) + "/status";
  const json j = get_json(*transport_, path);
  if (!j.is_object()) {
    throw ProtocolError(path + ": unexpected response");
  }
  Status s{parse_status_entry(path, j.value("api", json())),
           parse_status_entry(path, j.value("temperature", json())),
           parse_status_entry(path, j.value("systems_check", json())),
           std::nullopt};
  if (!internal::semver_is_less_than(firmware_version_, "1.7.1")) {
    s.time = parse_status_entry(path, j.value("time", json()));
  }
  return s;
}

double SonarClient::temperature() {
  const std::string path = std::string(API_BASE) + "/temperature";
  const json j = get_json(*transport_, path);
  if (!j.is_number()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return j.get<double>();
}

bool SonarClient::acoustics_enabled() {
  const std::string path = std::string(API_BASE) + "/acoustics/enabled";
  const json j = get_json(*transport_, path);
  if (!j.is_boolean()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return j.get<bool>();
}

void SonarClient::set_acoustics_enabled(bool enabled) {
  post_json(*transport_, std::string(API_BASE) + "/acoustics/enabled", enabled);
}

Range SonarClient::range() {
  const std::string path = std::string(API_BASE) + "/acoustics/range";
  const json j = get_json(*transport_, path);
  if (!j.is_object() || !j.value("min", json()).is_number() ||
      !j.value("max", json()).is_number()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return Range{j["min"], j["max"]};
}

void SonarClient::set_range(Range range) {
  post_json(*transport_, std::string(API_BASE) + "/acoustics/range",
            json{{"min", range.min}, {"max", range.max}});
}

double SonarClient::speed_of_sound() {
  const std::string path = std::string(API_BASE) + "/acoustics/speed_of_sound";
  const json j = get_json(*transport_, path);
  if (!j.is_number()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return j.get<double>();
}

void SonarClient::set_speed_of_sound(double speed) {
  post_json(*transport_, std::string(API_BASE) + "/acoustics/speed_of_sound",
            speed);
}

AcousticsMode SonarClient::mode() {
  require_firmware("mode", "1.7.0");
  const std::string path = std::string(API_BASE) + "/acoustics/mode";
  const json j = get_json(*transport_, path);
  if (j == "low-frequency") {
    return AcousticsMode::LOW_FREQUENCY;
  }
  if (j == "high-frequency") {
    return AcousticsMode::HIGH_FREQUENCY;
  }
  throw ProtocolError(path + ": unexpected response");
}

void SonarClient::set_mode(AcousticsMode mode) {
  require_firmware("set_mode", "1.7.0");
  post_json(*transport_, std::string(API_BASE) + "/acoustics/mode",
            to_string(mode));
}

Salinity SonarClient::salinity() {
  require_firmware("salinity", "1.7.0");
  const std::string path = std::string(API_BASE) + "/acoustics/salinity";
  const json j = get_json(*transport_, path);
  if (j == "salt") {
    return Salinity::SALT;
  }
  if (j == "fresh") {
    return Salinity::FRESH;
  }
  throw ProtocolError(path + ": unexpected response");
}

void SonarClient::set_salinity(Salinity salinity) {
  require_firmware("set_salinity", "1.7.0");
  post_json(*transport_, std::string(API_BASE) + "/acoustics/salinity",
            to_string(salinity));
}

UdpConfig SonarClient::udp_config() {
  const std::string path = std::string(API_BASE) + "/udp";
  const json j = get_json(*transport_, path);
  if (!j.is_object() || !j.value("mode", json()).is_string() ||
      !j.value("unicast_destination_ip", json()).is_string() ||
      !j.value("unicast_destination_port", json()).is_number_integer()) {
    throw ProtocolError(path + ": unexpected response");
  }
  UdpConfig cfg;
  const std::string mode = j["mode"];
  if (mode == "multicast") {
    cfg.mode = UdpConfig::Mode::MULTICAST;
  } else if (mode == "unicast") {
    cfg.mode = UdpConfig::Mode::UNICAST;
  } else if (mode == "disabled") {
    cfg.mode = UdpConfig::Mode::DISABLED;
  } else {
    throw ProtocolError(path + ": unexpected UDP mode '" + mode + "'");
  }
  cfg.unicast_destination_ip = j["unicast_destination_ip"];
  cfg.unicast_destination_port =
      static_cast<std::uint16_t>(j["unicast_destination_port"].get<int>());
  return cfg;
}

void SonarClient::set_udp_config(const UdpConfig& config) {
  post_json(
      *transport_, std::string(API_BASE) + "/udp",
      json{{"mode", to_string(config.mode)},
           {"unicast_destination_ip", config.unicast_destination_ip},
           {"unicast_destination_port", config.unicast_destination_port}});
}

bool SonarClient::imu_batch_enabled() {
  require_firmware("imu_batch_enabled", "1.8.0");
  const std::string path = std::string(API_BASE) + "/output/imu-batch/enabled";
  const json j = get_json(*transport_, path);
  if (!j.is_boolean()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return j.get<bool>();
}

void SonarClient::set_imu_batch_enabled(bool enabled) {
  require_firmware("set_imu_batch_enabled", "1.8.0");
  post_json(*transport_, std::string(API_BASE) + "/output/imu-batch/enabled",
            enabled);
}

TimeStatus SonarClient::time_status() {
  require_firmware("time_status", "1.7.1");
  const std::string path = std::string(API_BASE) + "/time/status";
  return parse_time_status(path, get_json(*transport_, path));
}

void SonarClient::set_time_manual(std::chrono::system_clock::time_point now) {
  require_firmware("set_time_manual", "1.7.1");
  try {
    post_json(*transport_, std::string(API_BASE) + "/time/manual",
              json{{"now", format_rfc3339_utc(now)}});
  } catch (const HttpError& e) {
    if (e.status_code() == 409) {
      throw TimeAlreadySynchronizedError();
    }
    throw;
  }
}

std::string SonarClient::ntp_address() {
  require_firmware("ntp_address", "1.7.1");
  const std::string path = std::string(API_BASE) + "/time/ntp";
  const json j = get_json(*transport_, path);
  if (!j.is_object() || !j.value("ntp_address", json()).is_string()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return j["ntp_address"];
}

void SonarClient::set_ntp_address(const std::string& address) {
  require_firmware("set_ntp_address", "1.7.1");
  post_json(*transport_, std::string(API_BASE) + "/time/ntp",
            json{{"ntp_address", address}});
}

ForceSyncResult SonarClient::force_sync_ntp(std::chrono::seconds timeout) {
  require_firmware("force_sync_ntp", "1.7.1");
  const std::string path = std::string(API_BASE) + "/time/ntp/force-sync";
  json j;
  try {
    // The device holds the request open while it attempts the sync, so the
    // HTTP timeout must exceed the requested sync timeout.
    j = post_json(
        *transport_, path,
        json{{"timeout_seconds", static_cast<double>(timeout.count())}},
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout) +
            std::chrono::seconds(5));
  } catch (const HttpError& e) {
    if (e.status_code() == 409) {
      throw ForceSyncOngoingError();
    }
    throw;
  }
  if (!j.is_object() || !j.value("success", json()).is_boolean() ||
      !j.value("message", json()).is_string()) {
    throw ProtocolError(path + ": unexpected response");
  }
  return ForceSyncResult{j["success"], j["message"],
                         parse_time_status(path, j.value("status", json()))};
}

}  // namespace waterlinked::sonar
